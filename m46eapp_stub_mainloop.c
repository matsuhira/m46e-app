/******************************************************************************/
/* ファイル名 : m46eapp_stub_mainloop.c                                       */
/* 機能概要   : Stubネットワークメインループ関数 ソースファイル               */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2013.07.08 Y.Shibata 動的定義変更機能追加                     */
/*              2013.08.21 H.Koganemaru M46E-PR機能拡張                       */
/*              2013.08.30 H.Koganemaru 動的定義変更機能追加                  */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2014.01.21 M.Iwatsubo M46E-PR外部連携機能追加                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <sched.h>
#include <alloca.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <paths.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include <sys/mount.h>
#include <sys/signalfd.h>

#include "m46eapp.h"
#include "m46eapp_stub_mainloop.h"
#include "m46eapp_log.h"
#include "m46eapp_socket.h"
#include "m46eapp_command.h"
#include "m46eapp_util.h"
#include "m46eapp_dynamic_setting.h"
#include "m46eapp_pr.h"
#include "m46eapp_sync_com_route.h"
#include "m46eapp_mng_v4_route.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static bool signal_handler(int fd, struct m46e_handler_t* handler);
static bool command_handler(int fd, struct m46e_handler_t* handler);
static bool command_exec_shell(struct m46e_handler_t* handler, struct m46e_command_t* command);
static bool sync_route_handler(int fd, struct m46e_handler_t* handler);

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク用のメインループ
//!
//! @param [in] handler M46Eハンドラ
//!
//! @retval 0      正常終了
//! @retval 0以外  異常終了
///////////////////////////////////////////////////////////////////////////////
void m46e_stub_mainloop(struct m46e_handler_t* handler)
{
    fd_set fds;
    int    max_fd;

    // selector用のファイディスクリプタ設定
    // (待ち受けるディスクリプタの最大値+1)
    max_fd = -1;
    max_fd = max(max_fd, handler->comm_sock[0]);
    max_fd = max(max_fd, handler->sync_route_sock[0]);
    max_fd = max(max_fd, handler->signalfd);
    max_fd++;

    DEBUG_LOG("stub network mainloop start\n");
    // mainloop
    while(1){
        FD_ZERO(&fds);
        FD_SET(handler->comm_sock[0], &fds);
        FD_SET(handler->sync_route_sock[0], &fds);
        FD_SET(handler->signalfd, &fds);

        // 受信待ち
        if(select(max_fd, &fds , NULL, NULL, NULL) < 0){
            if(errno == EINTR){
                m46e_logging(LOG_INFO, "Stub netowrk mainloop receive signal\n");
                continue;
            }
            else{
                m46e_logging(LOG_ERR, "Stub netowrk mainloop receive error : %s\n", strerror(errno));
                break;
            }
        }

        if(FD_ISSET(handler->comm_sock[0], &fds)){
            DEBUG_LOG("command receive");
            if(!command_handler(handler->comm_sock[0], handler)){
                // ハンドラの戻り値がfalseの場合はループを抜ける
                break;
            }
        }

        if(FD_ISSET(handler->sync_route_sock[0], &fds)){
            DEBUG_SYNC_LOG("command receive\n");
            if(!sync_route_handler(handler->sync_route_sock[0], handler)){
                // ハンドラの戻り値がfalseの場合はループを抜ける
                break;
            }
        }

        if(FD_ISSET(handler->signalfd, &fds)){
            DEBUG_LOG("signal receive");
            if(!signal_handler(handler->signalfd, handler)){
                // ハンドラの戻り値がfalseの場合はループを抜ける
                break;
            }
        }
    }
    DEBUG_LOG("stub network mainloop end.\n");

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク用シグナルハンドラ
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
    bool                    result;
    pid_t                   pid;

    ret = read(fd, &siginfo, sizeof(siginfo));
    if (ret < 0) {
        m46e_logging(LOG_ERR, "failed to read signal info\n");
        return true;
    }

    if (ret != sizeof(siginfo)) {
        m46e_logging(LOG_ERR, "unexpected siginfo size\n");
        return true;
    }

    switch(siginfo.ssi_signo){
    case SIGCHLD:
        DEBUG_LOG("signal %d catch. waiting for child process.\n", siginfo.ssi_signo);
        do{
            pid = waitpid(-1, &ret, WNOHANG);
            DEBUG_LOG("child process end. pid=%d, status=%d\n", pid, ret);
        } while(pid > 0);

        result = true;
        break;

    case SIGINT:
    case SIGTERM:
    case SIGQUIT:
    case SIGHUP:
        DEBUG_LOG("signal %d catch. finish process.\n", siginfo.ssi_signo);
        result = false;
        break;

    default:
        DEBUG_LOG("signal %d catch. ignore...\n", siginfo.ssi_signo);
        result = true;
        break;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク用内部コマンドハンドラ
//!
//! 親プロセス(Backboneネットワーク側)からの要求受信時に呼ばれるハンドラ。
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

    int ret = m46e_command_recv_request(handler, &command);
    if(ret > 0){
        DEBUG_LOG("read ok size = %d\n", ret);
    }
    else{
        m46e_logging(LOG_WARNING, "fail to receive command : %s\n", strerror(-ret));
        return true;
    }

    bool result;
    switch(command.code){
    case M46E_EXEC_SHELL:  // シェル起動
        result = command_exec_shell(handler, &command);
        break;

    case M46E_PACKET_TOO_BIG: // IPv6 Packet Too Big 受信
        // Path MTU管理テーブルに登録
        m46e_path_mtu_set(
            handler->pmtud_handler,
            &command.req.too_big.dst_addr,
            command.req.too_big.mtu
        );
        _D_(m46e_pmtu_print_table(handler->pmtud_handler, STDOUT_FILENO);)
        result = true;
        break;

    case M46E_SHOW_PMTU: // Path MTU 管理テーブル表示要求
        m46e_pmtu_print_table(handler->pmtud_handler, command.req.pmtu.fd);
        close(command.req.pmtu.fd);
        result = true;
        break;

    case M46E_DEVICE_ADD:
        if (m46eapp_stub_add_device(handler, &command, command.req.dev_data.fd)) {
            // 親プロセスにデバイス増減設完了(正常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_DEVICE_OPE_END, 0);
        } else {
            // 親プロセスにデバイス増減設完了(異常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_DEVICE_OPE_END, -1);
        }
        close(command.req.dev_data.fd);
        result = true;
        break;

    case M46E_DEVICE_DEL:
        if (m46eapp_stub_del_device(handler, &command, command.req.dev_data.fd)) {
            // 親プロセスにデバイス増減設完了(正常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_DEVICE_OPE_END, 0);
        } else {
            // 親プロセスにデバイス増減設完了(異常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_DEVICE_OPE_END, -1);
        }
        close(command.req.dev_data.fd);
        result = true;
        break;

    case M46E_ADD_PR_ENTRY: // PR ENTRY 追加要求
        if(!m46e_pr_add_entry_pr_table(handler, &command.req)) {
            // エントリ登録失敗
            m46e_logging(LOG_ERR,"fail to add M46E-PR Entry to M46E-PR Table\n");
        }
        close(command.req.pr_data.fd);
        result = true;
        break;

    case M46E_DEL_PR_ENTRY: // PR ENTRY 削除要求
        if(!m46e_pr_del_entry_pr_table(handler, &command.req)) {
            // エントリ削除失敗
            m46e_logging(LOG_ERR,"fail to del M46E-PR Entry to M46E-PR Table\n");
        }
        close(command.req.pr_data.fd);
        result = true;
        break;

    case M46E_DELALL_PR_ENTRY: // PR ENTRY 全削除要求
        if(!m46e_pr_delall_entry_pr_table(handler, &command.req)) {
            // エントリ削除失敗
            m46e_logging(LOG_ERR,"fail to del all M46E-PR Entry to M46E-PR Table\n");
        }
        close(command.req.pr_data.fd);
        result = true;
        break;

    case M46E_ENABLE_PR_ENTRY: // PR ENTRY 活性化要求
        if(!m46e_pr_enable_entry_pr_table(handler, &command.req)) {
            // エントリ活性化失敗
            m46e_logging(LOG_ERR,"fail to enable M46E-PR Entry\n");
        }
        close(command.req.pr_data.fd);
        result = true;
        break;

    case M46E_DISABLE_PR_ENTRY: // PR ENTRY 非活性化要求
        if(!m46e_pr_disable_entry_pr_table(handler, &command.req)) {
            // エントリ非活性化失敗
            m46e_logging(LOG_ERR,"fail to disable M46E-PR Entry\n");
        }
        close(command.req.pr_data.fd);
        result = true;
        break;

    case M46E_SHOW_PR_ENTRY: // PR ENTRY 表示要求
        m46e_pr_show_entry_pr_table(handler->pr_handler, command.req.pr_show.fd, handler->conf->general->plane_id);
        close(command.req.pr_show.fd);
        result = true;
        break;

    case M46E_SET_DEBUG_LOG: // デバッグログ出力設定 要求
        if(m46eapp_stub_set_debug_log(handler, &command, command.req.defgw.fd)){
            // 親プロセスにデバッグログモード設定完了(正常)を通知
            //m46e_logging(LOG_ERR,"succeed to set debug_log mode\n");
            m46e_command_sync_parent_with_result(handler, M46E_SET_DEBUG_LOG_END, 0);
        } else {
            // 親プロセスにデバッグログモード設定完了(異常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_SET_DEBUG_LOG_END, -1);
            m46e_logging(LOG_ERR,"fail to set debug_log mode\n");
        }
        close(command.req.dlog.fd);
        result = true;
        break;

    case M46E_SET_PMTUD_EXPTIME: // PMTU保持時間設定 要求
        if(!m46eapp_set_pmtud_expire(handler, &command, fd)){
            m46e_logging(LOG_ERR,"fail to set Path MTU Discovery expire_time\n");
        }
        close(command.req.pmtu_exptime.fd);
        result = true;
        break;

    case M46E_SET_PMTUD_MODE: // PMTU動作モード設定 要求
        if(!m46eapp_set_pmtud_type_stub(handler, &command, fd)){
            m46e_logging(LOG_ERR,"fail to set Path MTU Discovery type\n");
        }
        close(command.req.pmtu_mode.fd);
        result = true;
        break;

    case M46E_SET_FORCE_FRAG: // 強制フラグメント設定 要求
        if(!m46eapp_set_force_fragment(handler, &command, fd)){
            m46e_logging(LOG_ERR,"fail to set force fragment mode\n");
        }
        close(command.req.ffrag.fd);
        result = true;
        break;

    case M46E_SET_DEFAULT_GW: // デフォルトGW設定 要求
        if(m46eapp_stub_set_default_gw(handler, &command, command.req.defgw.fd)){
            // 親プロセスにデフォルトGW設定完了(正常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_SET_DEFAULT_GW_END, 0);
        } else {
            // 親プロセスにデフォルトGW設定完了(異常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_SET_DEFAULT_GW_END, -1);
            m46e_logging(LOG_ERR,"fail to set default gateway\n");
        }
        close(command.req.defgw.fd);
        result = true;
        break;

    case M46E_SET_TUNNEL_MTU: // トンネルデバイスMTU設定 要求
        if(!m46eapp_stub_set_tunnel_mtu(handler, &command, command.req.tunmtu.fd)){
            m46e_logging(LOG_ERR,"fail to set tunnel device mtumtu\n");
        }
        close(command.req.tunmtu.fd);
        result = true;
        break;

    case M46E_SET_DEVICE_MTU: // 収容デバイスMTU設定 要求
        if(m46eapp_stub_set_device_mtu(handler, &command, command.req.devmtu.fd)){
            // 親プロセスにデフォルトGW設定完了(正常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_SET_DEVICE_MTU_END, 0);
        } else {
            // 親プロセスにデフォルトGW設定完了(異常)を通知
            m46e_command_sync_parent_with_result(handler, M46E_SET_DEVICE_MTU_END, -1);
            m46e_logging(LOG_ERR,"fail to set pseudo device mtu\n");
        }
        close(command.req.devmtu.fd);
        result = true;
        break;

    case M46E_EXEC_INET_CMD: // Stub Network側コマンド実行 要求
        if(!m46eapp_stub_exec_cmd(handler, &command)){
            m46e_logging(LOG_ERR,"fail to exec requested command\n");
        }
        close(command.req.inetcmd.fd);
        result = true;

        break;

    case M46E_SHOW_ROUTE: // 経路情報表示要求
        if (handler->conf->general->route_sync) {
            m46e_route_print_v4table(handler, command.req.show_route.fd);
        }
        else {
            DEBUG_SYNC_LOG("m46e_route_print_v4table skip\n");
        }
        ret = m46e_command_send_response(handler, &command);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send command response : %s\n", strerror(-ret));
        }
        close(command.req.show_route.fd);
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

///////////////////////////////////////////////////////////////////////////////
//! @brief シェル起動処理
//!
//! 親プロセス(Backboneネットワーク側)からの要求を受けてシェルを起動する。
//! 起動したシェルの標準入出力は本関数内でオープンした擬似端末(pty)の
//! slaveディスクリプタとし、master側のディスクリプタを外部コマンド側に
//! コマンドの応答として引き渡す。
//!
//! @param [in] handler   M46Eハンドラ
//! @param [out] command  シェル起動結果、およびmasterディスクリプタの格納先
//!
//! @retval true   メインループを継続する
//! @retval false  メインループを継続しない
///////////////////////////////////////////////////////////////////////////////
static bool command_exec_shell(
    struct m46e_handler_t* handler,
    struct m46e_command_t* command
)
{
    int master;
    int slave;
    int ret;

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if(master < 0){
        m46e_logging(LOG_ERR, "fail to open master pty device : %s\n", strerror(errno));
        command->res.result = errno;
        command->res.exec_shell.fd = -1;
        ret = m46e_command_send_response(handler, command);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send command response : %s\n", strerror(-ret));
        }
        return true;
    }
    fcntl(master, F_SETFD, FD_CLOEXEC);

    ret = grantpt(master);
    if(ret != 0){
        m46e_logging(LOG_ERR, "fail to grant master pty device : %s\n", strerror(errno));
        command->res.result = errno;
        command->res.exec_shell.fd = -1;
        ret = m46e_command_send_response(handler, command);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send command response : %s\n", strerror(-ret));
        }
        close(master);
        return true;
    }

    ret = unlockpt(master);
    if(ret != 0){
        m46e_logging(LOG_ERR, "fail to unlock master pty device : %s\n", strerror(errno));
        command->res.result = errno;
        command->res.exec_shell.fd = -1;
        ret = m46e_command_send_response(handler, command);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send command response : %s\n", strerror(-ret));
        }
        close(master);
        return true;
    }

    pid_t pid;
    if((pid = fork()) < 0){
        m46e_logging(LOG_ERR, "fail to fork process : %s\n", strerror(errno));
        command->res.result = errno;
        command->res.exec_shell.fd = -1;
        ret = m46e_command_send_response(handler, command);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send command response : %s\n", strerror(-ret));
        }
        close(master);
        return true;
    }
    else if(pid > 0){
        // 親プロセス側 (masterデバイスを返信)
        command->res.result = 0;
        command->res.exec_shell.fd = master;
        ret = m46e_command_send_response(handler, command);
        if(ret < 0){
            m46e_logging(LOG_WARNING, "fail to send command response : %s\n", strerror(-ret));
        }
        close(master);
    }
    else{
        // 子プロセス側 (シェル起動)
        setsid();

        // シグナルマスクを元に戻す
        if (pthread_sigmask(SIG_SETMASK, &handler->oldsigmask, NULL)) {
            m46e_logging(LOG_WARNING, "failed to set sigprocmask");
        }
        close(handler->signalfd);

        ret = open(ptsname(master), O_RDWR);
        if(ret < 0){
            m46e_logging(LOG_ERR, "fail to open slave pty device : %s\n", strerror(errno));
            close(master);
            _exit(errno);
        }
        slave = ret;

        close(master);

        ioctl(0, TIOCSCTTY, 1);

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);

        close(slave);

        // 環境変数からシェルの実行ファイル名を取得
        char* shell = getenv("SHELL");
        if(shell == NULL){
            shell = _PATH_BSHELL;
        }

        // シェル起動
        execl(shell, shell, "-i", NULL);

        // ここには来ないはず
        m46e_logging(LOG_ERR, "fail to exec shell: %s\n", strerror(errno));
        _exit(errno);
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク用経路同期要求ハンドラ
//!
//! Backboneネットワークからの経路同期要求受信時に呼ばれるハンドラ。
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
            DEBUG_SYNC_LOG("stub コマンド受信\n");
            if (handler->conf->general->route_sync) {
                ret = m46e_rtsync_set_route(handler, &command.req.info_route);
            }
            else {
                DEBUG_SYNC_LOG("m46e_sync_route skip\n");
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

