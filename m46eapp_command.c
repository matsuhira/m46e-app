/******************************************************************************/
/* ファイル名 : m46eapp_command.c                                             */
/* 機能概要   : 内部コマンドクラス ソースファイル                             */
/* 修正履歴   : 2011.08.09 T.Maeda 新規作成                                   */
/*              2013.07.08 Y.Shibata 動的定義変更機能追加                     */
/*              2013.08.21 H.Koganemaru M46E-PR機能拡張                       */
/*              2013.08.30 H.Koganemaru 動的定義変更機能追加                  */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2013.01.21 M.Iwatsubo M46E-PR外部連携機能追加                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/fcntl.h>

#include "m46eapp.h"
#include "m46eapp_command.h"
#include "m46eapp_socket.h"
#include "m46eapp_log.h"

///////////////////////////////////////////////////////////////////////////////
//! @brief コマンドクラス初期化
//!
//! コマンド送受信用のソケットペアを生成する。
//!
//! @param [in,out] handler M46Eハンドラ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_init(struct m46e_handler_t* handler)
{
    if(handler == NULL){
        return false;
    }

    if(socketpair(AF_LOCAL, SOCK_DGRAM, 0, handler->comm_sock)){
        m46e_logging(LOG_ERR, "fail to create communication socket : %s", strerror(errno));
        return false;
    }

    fcntl(handler->comm_sock[0], F_SETFD, FD_CLOEXEC);
    fcntl(handler->comm_sock[1], F_SETFD, FD_CLOEXEC);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 親プロセスコマンドクラス初期化
//!
//! 子プロセス用のコマンド送受信用のソケットをcloseする。
//!
//! @param [in,out] handler M46Eハンドラ
//!
//! @return  なし
///////////////////////////////////////////////////////////////////////////////
void m46e_command_init_parent(struct m46e_handler_t* handler)
{
    if(handler == NULL){
        return;
    }

    if(handler->comm_sock[0] > 0){
        close(handler->comm_sock[0]);
        handler->comm_sock[0] = -1;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 子プロセスコマンドクラス初期化
//!
//! 親プロセス用のコマンド送受信用のソケットをcloseする。
//!
//! @param [in,out] handler M46Eハンドラ
//!
//! @return  なし
///////////////////////////////////////////////////////////////////////////////
void m46e_command_init_child(struct m46e_handler_t* handler)
{
    if(handler == NULL){
        return;
    }

    if(handler->comm_sock[1] > 0){
        close(handler->comm_sock[1]);
        handler->comm_sock[1] = -1;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 同期待ち関数(親プロセス)
//!
//! 引数で指定したコマンドコードを受信するまで待機する。
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] code    期待するコマンドコード
//!
//! @retval  true   引数で指定したコマンドコードを受信した
//! @retval  false  受信エラー or 引数で指定したコマンドコード以外を受信した
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_wait_parent(struct m46e_handler_t* handler, enum m46e_command_code code)
{
    enum m46e_command_code command;
    if((m46e_socket_recv(handler->comm_sock[0], &command, NULL, 0, NULL) > 0) && (command == code)){
        return true;
    }
    else{
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 同期待ち関数(子プロセス)
//!
//! 引数で指定したコマンドコードを受信するまで待機する。
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] code    期待するコマンドコード
//!
//! @retval  true   引数で指定したコマンドコードを受信した
//! @retval  false  受信エラー or 引数で指定したコマンドコード以外を受信した
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_wait_child(struct m46e_handler_t* handler, enum m46e_command_code code)
{
    enum m46e_command_code command;
    if((m46e_socket_recv(handler->comm_sock[1], &command, NULL, 0, NULL) > 0) && (command == code)){
        return true;
    }
    else{
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 同期待ち関数(子プロセス)
//!
//! 引数で指定したコマンドコードを受信するまで待機し、
//! コマンド応答結果を戻り値で返す。
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] code    期待するコマンドコード
//!
//! @retval  0      コマンド応答結果が正常
//! @retval  0以外  受信エラー or 引数で指定したコマンドコード以外を受信した or コマンド応答結果が異常
///////////////////////////////////////////////////////////////////////////////
int m46e_command_wait_child_with_result(struct m46e_handler_t* handler, enum m46e_command_code code)
{
    int ret = 0;
    enum m46e_command_code command;
    if((m46e_socket_recv(handler->comm_sock[1], &command, &ret, sizeof(int), NULL) > 0) && (command == code)){
        return ret;
    }
    else{
        return -1;
    }
}
///////////////////////////////////////////////////////////////////////////////
//! @brief 同期解除関数(親プロセス)
//!
//! 引数で指定したコマンドコードを送信して親プロセスの待機を解除する。
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] code    送信するコマンドコード
//! @param [in] ret     送信するコマンド応答
//!
//! @retval  true   送信成功
//! @retval  false  送信失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_sync_parent_with_result(struct m46e_handler_t* handler, enum m46e_command_code code, int ret)
{
    if(m46e_socket_send(handler->comm_sock[0], code, &ret, sizeof(int), -1) > 0){
        return true;
    }
    else{
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 同期解除関数(親プロセス)
//!
//! 引数で指定したコマンドコードを送信して親プロセスの待機を解除する。
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] code    送信するコマンドコード
//!
//! @retval  true   送信成功
//! @retval  false  送信失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_sync_parent(struct m46e_handler_t* handler, enum m46e_command_code code)
{
    if(m46e_socket_send(handler->comm_sock[0], code, NULL, 0, -1) > 0){
        return true;
    }
    else{
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 同期解除関数(子プロセス)
//!
//! 引数で指定したコマンドコードを送信して親プロセスの待機を解除する。
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] code    送信するコマンドコード
//!
//! @retval  true   送信成功
//! @retval  false  送信失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_sync_child(struct m46e_handler_t* handler, enum m46e_command_code code)
{
    if(m46e_socket_send(handler->comm_sock[1], code, NULL, 0, -1) > 0){
        return true;
    }
    else{
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 要求メッセージ送信関数
//!
//! 親プロセスから子プロセスへコマンド要求を送信する
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] command 送信するコマンド要求
//!
//! @retval  true   送信成功
//! @retval  false  送信失敗
///////////////////////////////////////////////////////////////////////////////
int m46e_command_send_request(struct m46e_handler_t* handler, struct m46e_command_t* command)
{
    int fd;

    switch(command->code){
    case M46E_SHOW_PMTU:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pmtu.fd;
        break;
    case M46E_DEVICE_ADD:
    case M46E_DEVICE_DEL:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.dev_data.fd;
        break;
    case M46E_ADD_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pr_data.fd;
        break;
    case M46E_DEL_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pr_data.fd;
        break;
    case M46E_DELALL_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pr_data.fd;
        break;
    case M46E_ENABLE_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pr_data.fd;
        break;
    case M46E_DISABLE_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pr_data.fd;
        break;
    case M46E_SHOW_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pr_show.fd;
        break;
    case M46E_SET_DEBUG_LOG:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.dlog.fd;
        break;
    case M46E_SET_PMTUD_EXPTIME:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pmtu_exptime.fd;
        break;
    case M46E_SET_PMTUD_MODE:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.pmtu_mode.fd;
        break;
    case M46E_SET_FORCE_FRAG:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.ffrag.fd;
        break;
    case M46E_SET_DEFAULT_GW:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.defgw.fd;
        break;
    case M46E_SET_TUNNEL_MTU:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.tunmtu.fd;
        break;
    case M46E_SET_DEVICE_MTU:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.devmtu.fd;
        break;
    case M46E_EXEC_INET_CMD:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.inetcmd.fd;
        break;
    case M46E_SHOW_ROUTE:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->req.show_route.fd;
        break;
    default:
        fd = -1;
        break;
    }

    return m46e_socket_send(handler->comm_sock[1], command->code, &command->req, sizeof(command->req), fd);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 応答メッセージ送信関数
//!
//! 子プロセスから親プロセスへコマンド応答を送信する
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] command 送信するコマンド応答
//!
//! @retval  true   送信成功
//! @retval  false  送信失敗
///////////////////////////////////////////////////////////////////////////////
int m46e_command_send_response(struct m46e_handler_t* handler, struct m46e_command_t* command)
{
    int fd;

    switch(command->code){
    case M46E_EXEC_SHELL:
        // 書き込み先のファイルディスクリプタ設定
        fd = command->res.exec_shell.fd;
        break;
    default:
        fd = -1;
        break;
    }

    return m46e_socket_send(handler->comm_sock[0], command->code, &command->res, sizeof(command->res), fd);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 要求メッセージ受信関数
//!
//! 親プロセスから子プロセスへ送信されたコマンド要求を受信する
//!
//! @param [in]  handler  M46Eハンドラ
//! @param [out] command  受信したコマンド要求格納先
//!
//! @retval  0以上  受信したデータバイト数
//! @retval  0未満  エラーコード(-errno)
///////////////////////////////////////////////////////////////////////////////
int m46e_command_recv_request(struct m46e_handler_t* handler, struct m46e_command_t* command)
{
    int fd;

    int res = m46e_socket_recv(handler->comm_sock[0], &command->code, &command->req, sizeof(command->req), &fd);
    switch(command->code){
    case M46E_SHOW_PMTU:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pmtu.fd = fd;
        break;
    case M46E_DEVICE_ADD:
    case M46E_DEVICE_DEL:
        // 書き込み先のファイルディスクリプタ設定
        command->req.dev_data.fd = fd;
        break;
    case M46E_ADD_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pr_data.fd = fd;
        break;
    case M46E_DEL_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pr_data.fd = fd;
        break;
    case M46E_DELALL_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pr_data.fd = fd;
        break;
    case M46E_ENABLE_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pr_data.fd = fd;
        break;
    case M46E_DISABLE_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pr_data.fd = fd;
        break;
    case M46E_SHOW_PR_ENTRY:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pr_show.fd = fd;
        break;
    case M46E_SET_DEBUG_LOG:
        // 書き込み先のファイルディスクリプタ設定
        command->req.dlog.fd = fd;
        break;
    case M46E_SET_PMTUD_EXPTIME:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pmtu_exptime.fd = fd;
        break;
    case M46E_SET_PMTUD_MODE:
        // 書き込み先のファイルディスクリプタ設定
        command->req.pmtu_mode.fd = fd;
        break;
    case M46E_SET_FORCE_FRAG:
        // 書き込み先のファイルディスクリプタ設定
        command->req.ffrag.fd = fd;
        break;
    case M46E_SET_DEFAULT_GW:
        // 書き込み先のファイルディスクリプタ設定
        command->req.defgw.fd = fd;
        break;
    case M46E_SET_TUNNEL_MTU:
        // 書き込み先のファイルディスクリプタ設定
        command->req.tunmtu.fd = fd;
        break;
    case M46E_SET_DEVICE_MTU:
        // 書き込み先のファイルディスクリプタ設定
        command->req.devmtu.fd = fd;
        break;
    case M46E_EXEC_INET_CMD:
        // 書き込み先のファイルディスクリプタ設定
        command->req.inetcmd.fd = fd;
        break;
    case M46E_SHOW_ROUTE:
        // 書き込み先のファイルディスクリプタ設定
        command->req.show_route.fd = fd;
        break;
    default:
        // なにもしない
        break;
    }

    return res;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 応答メッセージ受信関数
//!
//! 子プロセスから親プロセスへ送信されたコマンド応答を受信する
//!
//! @param [in]  handler  M46Eハンドラ
//! @param [out] command  受信したコマンド応答格納先
//!
//! @retval  0以上  受信したデータバイト数
//! @retval  0未満  エラーコード(-errno)
///////////////////////////////////////////////////////////////////////////////
int m46e_command_recv_response(struct m46e_handler_t* handler, struct m46e_command_t* command)
{
    int fd;

    int res = m46e_socket_recv(handler->comm_sock[1], &command->code, &command->res, sizeof(command->res), &fd);
    switch(command->code){
    case M46E_EXEC_SHELL:
        // 書き込み先のファイルディスクリプタ設定
        command->res.exec_shell.fd = fd;
        break;
    default:
        // なにもしない
        break;
    }

    return res;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 経路同期コマンドクラス初期化
//!
//! 経路同期コマンド送受信用のソケットペアを生成する。
//!
//! @param [in,out] handler M46Eハンドラ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_sync_route_command_init(struct m46e_handler_t* handler)
{

    // 引数チェック
    if( handler == NULL ) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_sync_route_command_init).");
        return false;
    }

    if(socketpair(AF_LOCAL, SOCK_DGRAM, 0, handler->sync_route_sock)){
        m46e_logging(LOG_ERR, "fail to create communication socket : %s", strerror(errno));
        return false;
    }

    fcntl(handler->sync_route_sock[0], F_SETFD, FD_CLOEXEC);
    fcntl(handler->sync_route_sock[1], F_SETFD, FD_CLOEXEC);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 子プロセス経路同期コマンドクラス初期化
//!
//! 親プロセス用の経路同期コマンド送受信用のソケットをcloseする。
//!
//! @param [in,out] handler M46Eハンドラ
//!
//! @return  なし
///////////////////////////////////////////////////////////////////////////////
void m46e_sync_route_command_init_child(struct m46e_handler_t* handler)
{
    if(handler == NULL){
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_sync_route_command_init_child).");
        return;
    }

    if(handler->sync_route_sock[1] > 0){
        close(handler->sync_route_sock[1]);
        handler->sync_route_sock[1] = -1;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 要求メッセージ送信関数(子プロセスで使用)
//!
//! 子プロセスから親プロセスへ経路同期要求を送信する
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] command 送信するコマンド要求
//!
//! @retval  0以上  送信バイト数
//! @retval  0未満  エラーコード(-errno)
///////////////////////////////////////////////////////////////////////////////
int m46e_send_sync_route_request_from_stub(
        struct m46e_handler_t* handler,
        struct m46e_command_t* command
)
{
    int fd;

    // 引数チェック
    if ( (handler == NULL) || (command == NULL) ) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_send_sync_route_request_from_stub).");
        return -1;
    }

    switch(command->code){
    default:
        fd = -1;
        break;
    }

    return m46e_socket_send(
            handler->sync_route_sock[0],
            command->code,
            &command->req,
            sizeof(command->req),
            fd);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 親プロセス経路同期コマンドクラス初期化
//!
//! 子プロセス用の経路同期コマンド送受信用のソケットをcloseする。
//!
//! @param [in,out] handler M46Eハンドラ
//!
//! @return  なし
///////////////////////////////////////////////////////////////////////////////
void m46e_sync_route_command_init_parent(struct m46e_handler_t* handler)
{
    // 引数チェック
    if (handler == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_sync_route_command_init_parent).");
        return;
    }

    if(handler->sync_route_sock[0] > 0){
        close(handler->sync_route_sock[0]);
        handler->sync_route_sock[0] = -1;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 要求メッセージ送信関数(親プロセスで使用)
//!
//! 親プロセスから子プロセスへ経路同期要求を送信する
//!
//! @param [in] handler M46Eハンドラ
//! @param [in] command 送信するコマンド要求
//!
//! @retval  0以上  送信バイト数
//! @retval  0未満  エラーコード(-errno)
///////////////////////////////////////////////////////////////////////////////
int m46e_send_sync_route_request_from_bb(
        struct m46e_handler_t* handler,
        struct m46e_command_t* command
)
{
    int fd;

    // 引数チェック
    if ( (handler == NULL) || (command == NULL) ) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_send_sync_route_request_from_bb).");
        return -1;
    }

    switch(command->code){
    default:
        fd = -1;
        break;
    }

    return m46e_socket_send(
            handler->sync_route_sock[1],
            command->code,
            &command->req,
            sizeof(command->req),
            fd);
}

