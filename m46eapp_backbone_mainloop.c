/******************************************************************************/
/* ファイル名 : m46eapp_backbone_mainloop.c                                   */
/* 機能概要   : Backboneネットワークメインループ関数 ソースファイル           */
/* 修正履歴   : 2011.08.09 T.Maeda 新規作成                                   */
/*              2013.07.08 Y.Shibata 動的定義変更機能追加                     */
/*              2013.08.21 H.Koganemaru M46E-PR機能拡張                       */
/*              2013.08.30 H.Koganemaru 動的定義変更機能追加                  */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2014.01.21 M.Iwatsubo M46E-PR外部連携機能追加                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <sched.h>
#include <alloca.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/signalfd.h>

#include "m46eapp.h"
#include "m46eapp_backbone_mainloop.h"
#include "m46eapp_log.h"
#include "m46eapp_socket.h"
#include "m46eapp_command.h"
#include "m46eapp_util.h"
#include "m46eapp_dynamic_setting.h"
#include "m46eapp_pr.h"
#include "m46eapp_sync_com_route.h"
#include "m46eapp_mng_v6_route.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static bool command_handler(int fd, struct m46e_handler_t* handler);
static bool signal_handler(int fd, struct m46e_handler_t* handler);
static bool sync_route_handler(int fd, struct m46e_handler_t* handler);

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク用のメインループ
//!
//! @param [in] handler M46Eハンドラ
//!
//! @retval 0      正常終了
//! @retval 0以外  異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_backbone_mainloop(struct m46e_handler_t* handler)
{
    int      max_fd;
    fd_set   fds;
    int      command_fd;
    char path[sizeof(((struct sockaddr_un*)0)->sun_path)] = {0};
    char* offset = &path[1];
    sprintf(offset, M46E_COMMAND_SOCK_NAME, handler->conf->general->plane_name);

    command_fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
    if(command_fd < 0){
        m46e_logging(LOG_ERR, "fail to create command socket : %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if(bind(command_fd, (struct sockaddr*)&addr, sizeof(addr))){
        m46e_logging(LOG_ERR, "fail to bind command socket : %s\n", strerror(errno));
        close(command_fd);
        return -1;
    }

    if(listen(command_fd, 100)){
        m46e_logging(LOG_ERR, "fail to listen command socket : %s\n", strerror(errno));
        close(command_fd);
        return -1;
    }

    // selector用のファイディスクリプタ設定
    // (待ち受けるディスクリプタの最大値+1)
    max_fd = -1;
    max_fd = max(max_fd, command_fd);
    max_fd = max(max_fd, handler->sync_route_sock[1]);
    max_fd = max(max_fd, handler->signalfd);
    max_fd++;

    DEBUG_LOG("backbone network mainloop start");
    while(1){
        FD_ZERO(&fds);
        FD_SET(command_fd, &fds);
        FD_SET(handler->sync_route_sock[1], &fds);
        FD_SET(handler->signalfd, &fds);

        // 受信待ち
        if(select(max_fd, &fds , NULL, NULL, NULL) < 0){
            if(errno == EINTR){
                m46e_logging(LOG_INFO, "Backbone netowrk mainloop receive signal\n");
                continue;
            }
            else{
                m46e_logging(LOG_ERR, "Backbone netowrk mainloop receive error : %s\n", strerror(errno));
                break;
            }
        }

        if(FD_ISSET(command_fd, &fds)){
            DEBUG_LOG("command receive\n");
            if(!command_handler(command_fd, handler)){
                // ハンドラの戻り値がfalseの場合はループを抜ける
                break;
            }
        }

        if(FD_ISSET(handler->sync_route_sock[1], &fds)){
            DEBUG_SYNC_LOG("command receive\n");
            if(!sync_route_handler(handler->sync_route_sock[1], handler)){
                // ハンドラの戻り値がfalseの場合はループを抜ける
                break;
            }
        }

        if(FD_ISSET(handler->signalfd, &fds)){
            DEBUG_LOG("signal receive\n");
            if(!signal_handler(handler->signalfd, handler)){
                // ハンドラの戻り値が0より大きい場合はループを抜ける
                break;
            }
        }
    }
    DEBUG_LOG("backbone network mainloop end");
    close(command_fd);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク用外部コマンドハンドラ
//!
//! 外部コマンドからの要求受信時に呼ばれるハンドラ。
//!
//! @param [in] fd      コマンドを受信したソケットのディスクリプタ
//! @param [in] handler M46Eハンドラ
//!
//! @retval true   メインループを継続する
//! @retval false  メインループを継続しない
///////////////////////////////////////////////////////////////////////////////
static bool command_handler(int fd, struct m46e_handler_t* handler)
{
    struct m46e_command_t command;
    int ret;
    int sock;

    sock = accept(fd, NULL, 0);
    if(sock <= 0){
        return true;
    }
    DEBUG_LOG("accept ok\n");

    if(fcntl(sock, F_SETFD, FD_CLOEXEC)){
        m46e_logging(LOG_ERR, "fail to set close-on-exec flag : %s\n", strerror(errno));
        close(sock);
        return true;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &opt, sizeof(opt))) {
        m46e_logging(LOG_ERR, "fail to set sockopt SO_PASSCRED : %s\n", strerror(errno));
        close(sock);
        return true;
    }

    ret = m46e_socket_recv_cred(sock, &command.code, &command.req, sizeof(command.req));
    DEBUG_LOG("command receive. code = %d,ret = %d\n", command.code, ret);

    switch(command.code){
    case M46E_EXEC_SHELL:
        if(ret <= 0){
            command.res.result = -ret;
            ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
            }
            break;
        }
        ret = m46e_command_send_request(handler, &command);
        if(ret <= 0){
            m46e_logging(LOG_ERR, "fail to send command to stub network : %s\n", strerror(-ret));
            command.res.result = -ret;
            command.res.exec_shell.fd = -1;
        }
        else{
            ret = m46e_command_recv_response(handler, &command);
            if(ret <= 0){
                m46e_logging(LOG_ERR, "fail to recv command from stub network : %s\n", strerror(-ret));
                command.res.result = -ret;
                command.res.exec_shell.fd = -1;
            }
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), command.res.exec_shell.fd);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.exec_shell.fd != -1){
            close(command.res.exec_shell.fd);
        }
        break;

    case M46E_SHOW_STATISTIC:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            switch(handler->conf->general->tunnel_mode){
            case M46E_TUNNEL_MODE_NORMAL:
                m46e_printf_statistics_info_normal(handler->stat_info, sock);
                break;
            case M46E_TUNNEL_MODE_AS:
                m46e_printf_statistics_info_as(handler->stat_info, sock);
                break;
            case M46E_TUNNEL_MODE_PR:
                m46e_printf_statistics_info_pr(handler->stat_info, sock);
                break;
            default:
                // ありえない
                break;
            }
        }
        break;

    case M46E_SHOW_CONF:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            m46e_config_dump(handler->conf, sock);
            // M46E-PRモードの場合はM46E-PR Tableを表示する
            if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR) {
                // Stub Network側にコマンドを転送
                command.code = M46E_SHOW_PR_ENTRY;
                command.req.pr_show.fd = sock;
                ret = m46e_command_send_request(handler, &command);
                if(ret < 0){
                    m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
                }
            }
        }
        break;

    case M46E_SHOW_PMTU:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            command.req.pmtu.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }
        break;

    case M46E_SET_DEBUG_LOG:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }

        if(command.res.result == 0){
            command.req.defgw.fd = sock;
            if(!m46eapp_backbone_set_debug_log(handler, &command, fd)){
                m46e_logging(LOG_ERR,"fail to set debug_log mode\n");
            }
        }

        break;

    case M46E_SET_PMTUD_EXPTIME:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else {
            if(!m46eapp_set_pmtud_expire(handler, &command, fd)){
                m46e_logging(LOG_ERR,"fail to set Path MTU Discovery expire_time \n");
            }
        }
        if(command.res.result == 0){
            command.req.pmtu_exptime.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }

        break;


    case M46E_SET_PMTUD_MODE:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else {
            if(!m46eapp_set_pmtud_type_bb(handler, &command, fd)){
                m46e_logging(LOG_ERR,"fail to set Path MTU Discovery type \n");
            }
        }
        if(command.res.result == 0){
            command.req.pmtu_mode.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }

        break;

    case M46E_SET_FORCE_FRAG:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else {
            if(!m46eapp_set_force_fragment(handler, &command, fd)){
                m46e_logging(LOG_ERR,"fail to set force fragment mode\n");
            }
        }
        if(command.res.result == 0){
            command.req.ffrag.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }

        break;

    case M46E_SET_DEFAULT_GW:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            command.req.defgw.fd = sock;
            if(!m46eapp_backbone_set_default_gw(handler, &command, fd)){
                m46e_logging(LOG_ERR,"fail to set default gateway\n");
            }
        }
        break;

    case M46E_SET_TUNNEL_MTU:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else {
            if(!m46eapp_backbone_set_tunnel_mtu(handler, &command, fd)){
                m46e_logging(LOG_ERR,"fail to set tunnel device mtu\n");
            }
        }
        if(command.res.result == 0){
            command.req.ffrag.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }

        break;

    case M46E_SET_DEVICE_MTU:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            command.req.devmtu.fd = sock;
            if(!m46eapp_backbone_set_device_mtu(handler, &command, fd)){
                m46e_logging(LOG_ERR,"fail to set pseudo device mtu\n");
            }
        }

        break;

    case M46E_EXEC_INET_CMD:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            command.req.inetcmd.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }

        break;

    case M46E_SHUTDOWN:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            kill(handler->stub_nw_pid, SIGTERM);
        }
        break;

    case M46E_RESTART:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            // リスタートフラグのセット
            m46eapp_set_flag_restart(true);
            // 子プロセスの終了させるために、シグナルを送信
            kill(handler->stub_nw_pid, SIGHUP);
        }
        break;

    case M46E_DEVICE_ADD:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }

        if(command.res.result == 0){
            command.req.dev_data.fd = sock;
            if (!m46eapp_backbone_add_device(handler, &command, sock)) {
                m46e_logging(LOG_WARNING, "fail to add device to external command\n");
            }
        }
        break;

    case M46E_DEVICE_DEL:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }

        if(command.res.result == 0){
            command.req.dev_data.fd = sock;
            if (!m46eapp_backbone_del_device(handler, &command, sock)) {
                m46e_logging(LOG_WARNING, "fail to delete device to external command\n");
            }
        }
        break;

    case M46E_ADD_PR_ENTRY:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else{
            // 動作モードがM46E-PRでない場合はConsoleにエラー出力して終了。
            if(handler->conf->general->tunnel_mode != M46E_TUNNEL_MODE_PR){
                m46e_pr_print_error(sock, M46E_PR_COMMAND_MODE_ERROR);
                break;
            }
        }
        if(command.res.result == 0){
            // Stub Network側にコマンドを転送
            command.req.pr_data.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }
        break;

    case M46E_DEL_PR_ENTRY:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else{
            // 動作モードがM46E-PRでない場合はConsoleにエラー出力して終了。
            if(handler->conf->general->tunnel_mode != M46E_TUNNEL_MODE_PR){
                m46e_pr_print_error(sock, M46E_PR_COMMAND_MODE_ERROR);
                break;
            }
        }
        if(command.res.result == 0){
            // Stub Network側にコマンドを転送
            command.req.pr_data.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }
        break;

    case M46E_DELALL_PR_ENTRY:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else{
            // 動作モードがM46E-PRでない場合はConsoleにエラー出力して終了。
            if(handler->conf->general->tunnel_mode != M46E_TUNNEL_MODE_PR){
                m46e_pr_print_error(sock, M46E_PR_COMMAND_MODE_ERROR);
                break;
            }
        }
        if(command.res.result == 0){
            // Stub Network側にコマンドを転送
            command.req.pr_data.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }
        break;

    case M46E_ENABLE_PR_ENTRY:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else{
            // 動作モードがM46E-PRでない場合はConsoleにエラー出力して終了。
            if(handler->conf->general->tunnel_mode != M46E_TUNNEL_MODE_PR){
                m46e_pr_print_error(sock, M46E_PR_COMMAND_MODE_ERROR);
                break;
            }
        }
        if(command.res.result == 0){
            // Stub Network側にコマンドを転送
            command.req.pr_data.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }
        break;

    case M46E_DISABLE_PR_ENTRY:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else{
            // 動作モードがM46E-PRでない場合はConsoleにエラー出力して終了。
            if(handler->conf->general->tunnel_mode != M46E_TUNNEL_MODE_PR){
                m46e_pr_print_error(sock, M46E_PR_COMMAND_MODE_ERROR);
                break;
            }
        }
        if(command.res.result == 0){
            // Stub Network側にコマンドを転送
            command.req.pr_data.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
        }
        break;

    case M46E_SHOW_PR_ENTRY:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        else{
            // 動作モードがM46E-PRでない場合はConsoleにエラー出力して終了。
            if(handler->conf->general->tunnel_mode != M46E_TUNNEL_MODE_PR){
                m46e_pr_print_error(sock, M46E_PR_COMMAND_MODE_ERROR);
                break;
            }
        }
        if(command.res.result == 0){
            // Stub Network側にコマンドを転送
            command.req.pr_show.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }

        }
        break;
    case M46E_SHOW_ROUTE: // 経路情報表示要求
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = m46e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send response to external command : %s\n", strerror(-ret));
        }
        if(command.res.result == 0){
            command.req.show_route.fd = sock;
            ret = m46e_command_send_request(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request to stub network : %s\n", strerror(-ret));
            }
            else {
                ret = m46e_command_recv_response(handler, &command);
                if(ret <= 0){
                    m46e_logging(LOG_ERR, "fail to recv command from stub network : %s\n", strerror(-ret));
                } else {
                    if (handler->conf->general->route_sync) {
                        m46e_route_print_v6table(handler, sock);
                    }
                    else {
                        DEBUG_SYNC_LOG("m46e_route_print_v6table skip\n");
                    }
                }
            }
        }
        break;

    default:
        break;
    }

    close(sock);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク用シグナルハンドラ
//!
//! シグナル受信時に呼ばれるハンドラ。
//!
//! @param [in] fd      シグナルを受信したディスクリプタ
//! @param [in] handler M46Eハンドラ
//!
//! @retval true   メインループを継続する
//! @retval false  メインループを継続しない
///////////////////////////////////////////////////////////////////////////////
static bool signal_handler(int fd, struct m46e_handler_t* handler)
{
    struct signalfd_siginfo siginfo;
    int                     ret;

    ret = read(fd, &siginfo, sizeof(siginfo));
    if (ret < 0) {
        m46e_logging(LOG_ERR, "failed to read signal info\n");
        return true;
    }

    if (ret != sizeof(siginfo)) {
        m46e_logging(LOG_ERR, "unexpected siginfo size\n");
        return true;
    }

    if (siginfo.ssi_signo != SIGCHLD) {
        kill(handler->stub_nw_pid, siginfo.ssi_signo);
        m46e_logging(LOG_INFO, "forwarded signal %d to pid %d\n", siginfo.ssi_signo, handler->stub_nw_pid);
        return true;
    }

    if (siginfo.ssi_code == CLD_STOPPED || siginfo.ssi_code == CLD_CONTINUED) {
        m46e_logging(LOG_INFO, "stub network process was stopped/continued\n");
        return true;
    }

    if (siginfo.ssi_pid != handler->stub_nw_pid) {
        DEBUG_LOG("recv SIGCHLD, but PID is not stub network process. ignore...\n");
        return true;
    }

    DEBUG_LOG("stub network process exited\n");
    return false;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク用経路同期要求ハンドラ
//!
//! Stubネットワークからの経路同期要求受信時に呼ばれるハンドラ。
//!
//! @param [in] fd      コマンドを受信したソケットのディスクリプタ
//! @param [in] handler M46Eハンドラ
//!
//! @retval true   メインループを継続する
//! @retval false  メインループを継続しない
///////////////////////////////////////////////////////////////////////////////
static bool sync_route_handler(int fd, struct m46e_handler_t* handler)
{
    struct m46e_command_t command;
    int ret;
    int sock;


    ret = m46e_socket_recv(fd, &command.code, &command.req, sizeof(command.req), &sock);

    if(ret > 0){
        DEBUG_SYNC_LOG("read ok size = %d\n", ret);
    }
    else{
        m46e_logging(LOG_WARNING, "fail to receive command : %s\n", strerror(-ret));
        return true;
    }

    bool result;
    switch(command.code){
    case M46E_SYNC_ROUTE:
        if(ret > 0){
            DEBUG_SYNC_LOG("backbone コマンド受信\n");
            if (handler->conf->general->route_sync) {
                m46e_rtsync_set_route(handler, &command.req.info_route);
            }
            else {
                DEBUG_SYNC_LOG("m46e_rtsync_set_route skip\n");
            }
        }
        result = true;
        break;

    default:
        // なにもしない
        m46e_logging(LOG_WARNING, "unknown command code(%d) ignore...\n", command.code);
        result = true;
        break;
    }

    return result;
}

