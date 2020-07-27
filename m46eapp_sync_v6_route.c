/******************************************************************************/
/* ファイル名 : m46eapp_sync_v6_route.c                                       */
/* 機能概要   : v6経路同期 ソースファイル                                     */
/* 修正履歴   : 2013.07.19 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
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
#include <netinet/in.h>

#include "m46eapp.h"
#include "m46eapp_log.h"
#include "m46eapp_netlink.h"
#include "m46eapp_rtnetlink.h"
#include "m46eapp_mng_com_route.h"
#include "m46eapp_sync_com_route.h"
#include "m46eapp_sync_v6_route.h"

///////////////////////////////////////////////////////////////////////////////
//! @brief スレッド終了時処理関数
//!
//! RTNETLINK用ソケットのクローズを行う
//!
//! @param [in] sock_id   RTNETLINK用ソケットID
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void rtnetlink_close_sock(void* arg)
{
    DEBUG_SYNC_LOG("rtnetlink thread close sock \n");

    close(*((int*)arg));

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backnoneネットワーク 経路同期メインループ関数
//!
//! RTNETLINKを使用して、経路表（内部）の初期化、仮想テーブルの変更通知処理を行なう
//!
//! @param [in] param   設定ファイル情報アドレス
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void rtnetlink_rcv_v6_route_thread(struct m46e_handler_t* handler)
{

    struct sockaddr_nl local;                   /* Local netlink socket address     */
    uint32_t seq                        = 0;    /* Sequence number(for netlink msg) */
    int ret                             = 0;    /* return value                     */
    int errcd                           = 0;    /* error code                       */
    unsigned long group                 = 0;    /* Netlink group                    */
    int sock_fd                         = 0;    /* Netlink socket fd                */
    v6_route_info_table_t *route_info   = NULL; /* IPv6 routing info                */

    m46e_logging(LOG_INFO, "rtnetlink v6 route Thread Start\n");

    route_info = handler->v6_route_info;

    // 仮想デバイスのindex設定
    route_info->tunnel_dev_idx = handler->conf->tunnel->ipv6.ifindex;

    //////////////////////////////////////////////////////////////////////
    // 初期設定
    // ・rtnetlink用ソケットオープン
    // ・スレッド終了時処理の登録
    //////////////////////////////////////////////////////////////////////
    group = RTMGRP_LINK | RTMGRP_IPV6_ROUTE | RTMGRP_IPV6_IFADDR;

    ret = m46e_netlink_open(group, &sock_fd, &local, &seq, &errcd);

    if(ret != RESULT_OK){
        /* socket open error */
        m46e_logging(LOG_ERR, "IPv6 Netlink socket error errcd=%d\n", errcd);
        return;
    }

    // スレッド終了時処理登録
    pthread_cleanup_push(rtnetlink_close_sock, (void*)&sock_fd);

    // 子プロセスからの経路同期初期化完了通知待ち
    if(!m46e_command_wait_child(handler, M46E_THREAD_INIT_END)){
        m46e_logging(LOG_ERR, "status error (waiting for M46E_THREAD_INIT_END)\n");
        return;
    }

    //////////////////////////////////////////////////////////////////////
    // 経路表（内部）の初期設定
    // ・rtnetlinkを使用し、仮想テーブルの経路情報を取得し、設定する
    //////////////////////////////////////////////////////////////////////
    if (m46e_get_route_entry(AF_INET6, handler) != RESULT_OK) {
        // エラーの場合は、再送する
        m46e_get_route_entry(AF_INET6, handler);
    }

    //////////////////////////////////////////////////////////////////////
    // 経路表変更通知受信待ち
    // ・rtnetlinkを使用し、仮想テーブルに対する変更通知を受信し、
    //   経路表（内部）の設定を行う
    //////////////////////////////////////////////////////////////////////
    while(1){
        ret = m46e_netlink_multicast_recv(sock_fd, &local, seq, &errcd,
                      m46e_rtnl_parse_rcv, handler);
    }

    pthread_cleanup_pop(1);
    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク用 経路同期スレッド
//!
//! IPv6経路同期のメインループを呼ぶ。
//!
//! @param [in] arg M46Eハンドラ
//!
//! @return NULL固定
///////////////////////////////////////////////////////////////////////////////
void* m46e_sync_route_backbone_thread(void* arg)
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
    rtnetlink_rcv_v6_route_thread(handler);

    pthread_exit(NULL);

    return NULL;
}

