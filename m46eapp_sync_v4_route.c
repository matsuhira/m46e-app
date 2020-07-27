/******************************************************************************/
/* ファイル名 : m46eapp_sync_v4_route.c                                       */
/* 機能概要   : v4経路同期 ソースファイル                                     */
/* 修正履歴   : 2013.06.06 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/shm.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "m46eapp.h"
#include "m46eapp_log.h"
#include "m46eapp_netlink.h"
#include "m46eapp_list.h"
#include "m46eapp_rtnetlink.h"
#include "m46eapp_mng_com_route.h"
#include "m46eapp_sync_com_route.h"
#include "m46eapp_sync_v4_route.h"


///////////////////////////////////////////////////////////////////////////////
//! @brief スレッド終了時処理関数
//!
//! RTNETLINK用ソケットのクローズを行う
//!
//! @param [in] arg   RTNETLINK用ソケットディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_rtnetlink_close_sock(void* arg)
{
    DEBUG_SYNC_LOG("rtnetlink thread close sock \n");

    close(*((int*)arg));

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス情報設定関数
//!
//! 設定ファイルの内容からデバイス情報を作成する
//!
//! @param [in/out]     handler  M46E アプリケーションハンドラー
//!
//! @return なし
//
///////////////////////////////////////////////////////////////////////////////
void setInterfaceInfo(struct m46e_handler_t* handler)
{
    DEBUG_SYNC_LOG("setInterfaceInfo start \n");

    // 引数チェック
    if (handler == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(setInterfaceInfo).");
        return;
    }

    // ローカル変数宣言
    m46e_config_t* conf = handler->conf;

    if (conf->tunnel->ipv4.type != M46E_DEVICE_TYPE_TUNNEL_IPV4) {
        DEBUG_SYNC_LOG("skip interface info because of other mode\n");
        return;
    }

    // トンネルデバイスのインデックスを取得
    handler->v4_route_info->tunnel_dev_idx = conf->tunnel->ipv4.ifindex;

    // デバイスリストの初期化
    m46e_list_init(&handler->v4_route_info->device_list);

    // デバイスリストのインデックスを取得
    struct m46e_list* iter;
    m46e_list* node;
    int* index;

    m46e_list_for_each(iter, &conf->device_list){
        m46e_device_t* device = iter->data;

        DEBUG_SYNC_LOG("set device %s\n", device->name);

        switch(device->type) {
        case M46E_DEVICE_TYPE_MACVLAN: // macvlanの場合
            node = malloc(sizeof(m46e_list));
            if(node == NULL){
                m46e_logging(LOG_ERR, "fail to malloc for device index list.");
                return;
            }

            index = malloc(sizeof(int));
            if(index == NULL){
                m46e_logging(LOG_ERR, "fail to malloc for device index node.");
                free(node);
                return;
            }
            *index = device->ifindex;

            // デバイスリストへ追加
            m46e_list_init(node);
            m46e_list_add_data(node, index);
            m46e_list_add_tail(&handler->v4_route_info->device_list, node);
            break;

        case M46E_DEVICE_TYPE_VETH: // vethの場合
            break;

        case M46E_DEVICE_TYPE_PHYSICAL: // physicalの場合
            break;

        default:
            // なにもしない
            break;
        }
    }

    // デバッグ用：取得したデバイス情報の表示
    DEBUG_SYNC_LOG("tunnel index   = %d\n", handler->v4_route_info->tunnel_dev_idx);
    m46e_list_for_each(iter, &handler->v4_route_info->device_list){
        int* index = iter->data;
        if(index != NULL){
            DEBUG_SYNC_LOG("device index   = %d\n", *index);
        }
    }

    DEBUG_SYNC_LOG("setInterfaceInfo end \n");
    return;

}


///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス情報全削除関数
//!
//! v4経路同期テーブルのデバイス情報を全て削除する。
//!
//! @param [in/out]     handler  M46E アプリケーションハンドラー
//!
//! @return なし
//
///////////////////////////////////////////////////////////////////////////////
void delAllInterfaceInfo(struct m46e_handler_t* handler)
{
    DEBUG_SYNC_LOG("delAllInterfaceInfo start \n");

    // 引数チェック
    if (handler == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(setInterfaceInfo).");
        return;
    }

    // ローカル変数宣言
    m46e_config_t* conf = handler->conf;

    if (conf->tunnel->ipv4.type != M46E_DEVICE_TYPE_TUNNEL_IPV4) {
        DEBUG_SYNC_LOG("skip interface info because of other mode\n");
        return;
    }

    // デバイスリストの削除
    while(!m46e_list_empty(&handler->v4_route_info->device_list)){
        m46e_list* node = handler->v4_route_info->device_list.next;
        int* device = node->data;
        free(device);
        m46e_list_del(node);
        free(node);
    }

    DEBUG_SYNC_LOG("delAllInterfaceInfo end \n");
    return;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス情報追加関数
//!
//! デバイス情報を新規に追加する。
//!
//! @param [in/out]     handler  M46E アプリケーションハンドラー
//! @param [in]         entry   追加するデバイスインデックス
//!
//! @return true        追加成功
//!         false       追加失敗
///////////////////////////////////////////////////////////////////////////////
bool addInterfaceInfo(struct m46e_handler_t* handler, int devidx)
{
    struct m46e_list* iter = NULL;
    m46e_list* node        = NULL;
    int* index              = NULL;

    DEBUG_SYNC_LOG("addInterfaceInfo start \n");

    // 引数チェック
    if (handler == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(setInterfaceInfo).");
        return false;
    }

    node = malloc(sizeof(m46e_list));
    if(node == NULL){
        m46e_logging(LOG_ERR, "fail to malloc for device index list.");
        return false;
    }

    index = malloc(sizeof(int));
    if(index == NULL){
        m46e_logging(LOG_ERR, "fail to malloc for device index node.");
        free(node);
        return false;
    }
    *index = devidx;

    // デバイスリストへ追加
    m46e_list_init(node);
    m46e_list_add_data(node, index);
    m46e_list_add_tail(&handler->v4_route_info->device_list, node);

    // デバッグ用：取得したデバイス情報の表示
    m46e_list_for_each(iter, &handler->v4_route_info->device_list){
        int* tmp = iter->data;
        if(tmp != NULL){
            DEBUG_SYNC_LOG("device index   = %d\n", *tmp);
        }
    }

    DEBUG_SYNC_LOG("addInterfaceInfo end \n");
    return true;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス情報削除関数
//!
//! デバイス情報を削除する。
//!
//! @param [in/out]     handler  M46E アプリケーションハンドラー
//! @param [in]         entry   削除するデバイスインデックス
//!
//! @return true        削除成功
//!         false       削除失敗
///////////////////////////////////////////////////////////////////////////////
bool delInterfaceInfo(struct m46e_handler_t* handler, int devidx)
{
    struct m46e_list*  iter = NULL;

    DEBUG_SYNC_LOG("delInterfaceInfo start \n");

    // 引数チェック
    if (handler == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(setInterfaceInfo).");
        return false;
    }

    // デバイス設定リスト削除処理
    m46e_list_for_each(iter, &handler->v4_route_info->device_list) {
        int* tmp = iter->data;

        // デバイスインデックスが一致するエントリーを検索
        if (*tmp == devidx) {
            DEBUG_SYNC_LOG("match device index = %d.\n", *tmp);

            // 一致したエントリーを削除
            free(tmp);
            m46e_list_del(iter);
            free(iter);

            break;
        }
    }

    // 検索に失敗した場合、ログを残す
    if (iter == &handler->v4_route_info->device_list) {
        m46e_logging(LOG_INFO, "Don't match index = %d\n", devidx);
    }

    // 削除対象の管理デバイスを含む経路を削除
    m46e_del_route_by_device(handler, devidx);

    // デバイス設定リスト削除処理

    DEBUG_SYNC_LOG("delInterfaceInfo end \n");
    return true;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク IPv4経路同期メインループ関数
//!
//! ・Stub側で管理しているデバイス情報取得処理を起動する。
//! ・IPv4経路表（内部）の初期化処理を起動する。
//! ・RTNETLINKから、カーネルのルーティング情報を取得し、
//!   IPv4経路表（内部）の更新処理を起動する。
//!
//! @param [in] handler  M46E アプリケーションハンドラー
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void rtnetlink_rcv_v4_route_thread(struct m46e_handler_t* handler)
{

    struct sockaddr_nl local;                   /* Local netlink socket address     */
    uint32_t seq                        = 0;    /* Sequence number(for netlink msg) */
    int ret                             = 0;    /* return value                     */
    int errcd                           = 0;    /* error code                       */
    unsigned long group                 = 0;    /* Netlink group                    */
    int sock_fd                         = 0;    /* Netlink socket fd                */

    m46e_logging(LOG_INFO, "IPv4 sync route thread Start\n");

    // デバイス情報の取得
    setInterfaceInfo(handler);

    //////////////////////////////////////////////////////////////////////
    // 初期設定
    // ・rtnetlink用ソケットオープン
    // ・スレッド終了時処理の登録
    //////////////////////////////////////////////////////////////////////
    group = RTMGRP_LINK | RTMGRP_IPV4_ROUTE | RTMGRP_IPV4_IFADDR;

    ret = m46e_netlink_open(group, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        /* socket open error */
        m46e_logging(LOG_ERR, "IPv4 Netlink socket error errcd=%d\n", errcd);
        return;
    }

    // スレッド終了時処理登録
    pthread_cleanup_push(m46e_rtnetlink_close_sock, (void*)&sock_fd);

    //////////////////////////////////////////////////////////////////////
    // v4経路同期スレッド初期化完了の送信
    //////////////////////////////////////////////////////////////////////
    m46e_command_sync_parent(handler, M46E_THREAD_INIT_END);

    //////////////////////////////////////////////////////////////////////
    // 経路表（内部）の初期設定
    // rtnetlinkを使用し、カーネルのルーティングテーブルの経路情報を取得し、
    // 内部のIPv4経路表に設定する
    //////////////////////////////////////////////////////////////////////
    if (m46e_get_route_entry(AF_INET, handler) != RESULT_OK) {
        // エラーの場合は、再取得する
        m46e_get_route_entry(AF_INET, handler);
    }

    //////////////////////////////////////////////////////////////////////
    // 経路表変更通知受信待ち
    // rtnetlinkを使用し、カーネルのルーティングテーブルに対する
    // 変更通知を受信し、 内部のIPv4経路表の更新を行う。
    //////////////////////////////////////////////////////////////////////
    while(1) {
        ret = m46e_netlink_multicast_recv(sock_fd, &local, seq, &errcd,
                      m46e_rtnl_parse_rcv, handler);
    }

    pthread_cleanup_pop(1);
    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク用 IPv4経路同期スレッド
//!
//! IPv4経路同期のメインループを呼ぶ。
//!
//! @param [in] arg M46Eハンドラ
//!
//! @return NULL固定
///////////////////////////////////////////////////////////////////////////////
void* m46e_sync_route_stub_thread(void* arg)
{
    // ローカル変数宣言
    struct m46e_handler_t* handler;

    // 引数チェック
    if(arg == NULL){
        pthread_exit(NULL);
    }

    // ローカル変数初期化
    handler = (struct m46e_handler_t*)arg;

    // メインループ開始
    rtnetlink_rcv_v4_route_thread(handler);

    pthread_exit(NULL);

    return NULL;
}

