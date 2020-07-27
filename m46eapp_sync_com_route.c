/******************************************************************************/
/* ファイル名 : m46eapp_sync_com_route.c                                      */
/* 機能概要   : 経路同期 共通ソースファイル                                   */
/* 修正履歴   : 2013.06.06  Y.Shibata 新規作成                                */
/*              2016.04.15  H.Koganemaru 名称変更に伴う修正                   */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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
#include "m46eapp_command.h"
#include "m46eapp_rtnetlink.h"
#include "m46eapp_mng_com_route.h"
#include "m46eapp_mng_v4_route.h"
#include "m46eapp_mng_v6_route.h"
#include "m46eapp_sync_com_route.h"
#include "m46eapp_sync_v4_route.h"
#include "m46eapp_sync_v6_route.h"


////////////////////////////////////////////////////////////////////////////////
// 内部定数定義
////////////////////////////////////////////////////////////////////////////////
// (管理用)v6経路情報の登録可能経路数
// SubnetMask変換用差分
#define SUBNET_MARGIN_M46E 96
#define SUBNET_MARGIN_AS 80
// Plefixサイズ
#define PREFIX_LEN_M46E 96
#define PREFIX_LEN_AS 80

//! IPv4のサブネットマスクの最大値
#define IPV4_NETMASK_MAX 32
//! IPv6のサブネットマスクの最大値
#define IPV6_NETMASK_MAX 128

// SAe6Tユニキャストアドレスのプレフィックス判定
#define IS_EQUAL_SAE6T_PREFIX(a, b) \
        (((__const uint32_t *) (a))[0] == ((__const uint32_t *) (b))[0]     \
         && ((__const uint32_t *) (a))[1] == ((__const uint32_t *) (b))[1]  \
         && ((__const uint32_t *) (a))[2] == ((__const uint32_t *) (b))[2])

#define IS_EQUAL_SAE6T_AS_PREFIX(a, b) \
        (((__const uint32_t *) (a))[0] == ((__const uint32_t *) (b))[0]     \
         && ((__const uint32_t *) (a))[1] == ((__const uint32_t *) (b))[1]  \
         && ((__const uint16_t *) (a))[4] == ((__const uint16_t *) (b))[4])

///////////////////////////////////////////////////////////////////////////////
//! @brief V4経路変換関数
//!
//! V4の経路情報をV6の経路情報へ変換する
//!
//! @param [in]  route_v4   V4経路情報
//!        [OUT] route_v6   V6経路情報
//!
//! @return OK:0   変換処理正常終了
//! @return NG:-1  変換処理異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_change_route_v4_to_v6(
        struct m46e_handler_t* handler,
        struct m46e_v4_route_info_t* route_v4,
        struct m46e_v6_route_info_t* route_v6
)
{
        int mode = -1;

    // 引数チェック
    if ((handler == NULL) || (route_v6 == NULL) || (route_v4 == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_change_route_v6_to_v4).");
        return -1;
    }

    mode = handler->conf->general->tunnel_mode;
    // prefixのコピー
    memcpy(&route_v6->in_dst, &handler->unicast_prefix, sizeof(route_v6->in_dst));

    // -------------------------------------------------------------
    // 経路情報の変換
    //   ・経路同期による追加（true/false）
    //       trueを設定
    //   ・タイプ（追加/削除）
    //       V4のタイプをそのまま設定
    //   ・ether番号
    //       V6用仮想デバイスの番号を設定
    //   ・Destnation Address
    //       M46E : 下位32bitにV4アドレスを設定
    //       AS    : 80bitから32bitにV4アドレスを設定
    //               下位16bit（port番号）に0を設定
    //   ・SubnetMask
    //       M46E : V4で指定されたSubnetMaskに96足した値を設定
    //       AS    : V4で指定されたSubnetMaskに80足した値を設定
    //   ・Gateway
    //       in6addr_any（::）を設定
    //   ・Source Address
    //       in6addr_any（::）を設定
    //   ・metric
    //       0を設定
    // -------------------------------------------------------------
    route_v6->sync = true;

    if (mode == M46E_TUNNEL_MODE_NORMAL) {
        route_v6->in_dst.s6_addr32[3] = route_v4->in_dst.s_addr;

        route_v6->mask = route_v4->mask + SUBNET_MARGIN_M46E;
    }
    // M46E-ASの場合は、インターネット側のみ変換
    else if (mode == M46E_TUNNEL_MODE_AS) {
        memcpy(&route_v6->in_dst.s6_addr16[5], &route_v4->in_dst.s_addr, sizeof(route_v4->in_dst.s_addr));
        route_v6->in_dst.s6_addr16[7] = 0;

        route_v6->mask = route_v4->mask + SUBNET_MARGIN_AS;
    }
    else {
        m46e_logging(LOG_ERR, "no sync mode\n");
        return -1;
    }

    route_v6->type = route_v4->type;
    route_v6->out_if_index = handler->v6_route_info->tunnel_dev_idx;
    route_v6->in_gw = in6addr_any;
    route_v6->in_src = in6addr_any;
    route_v6->priority = 0;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Prefixチェック処理関数
//!
//! Backbone側から受信したパケットのM46Eプレフィックスをチェックする。
//!
//! @param [in,out] handler     SAe6Tハンドラ
//! @param [in]     ipi6_addr   送信元アドレス
//!
//! @retval true  Prefixが自planeと同じ
//! @retval false Prefixが自planeと異なる
///////////////////////////////////////////////////////////////////////////////
bool m46e_prefix_check(
        struct m46e_handler_t* handler,
        struct in6_addr* ipi6_addr)
{
    // 引数チェック
    if ((handler == NULL) || (ipi6_addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_prefix_check).");
        return false;
    }

    // 送信元がUNSPECIFIEDは破棄
    if (IN6_IS_ADDR_UNSPECIFIED(ipi6_addr)) {
        DEBUG_SYNC_LOG("check error unspecified address.\n");
        return false;
    }

    // 送信元がLOOPBACKは破棄
    if (IN6_IS_ADDR_LOOPBACK(ipi6_addr)) {
        DEBUG_SYNC_LOG("check error loopback address.\n");
        return false;
    }

    bool ret = false;
    if (handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_NORMAL ) {
        DEBUG_SYNC_LOG("tunnel mode normal.\n");
        ret = IS_EQUAL_SAE6T_PREFIX(ipi6_addr, &handler->unicast_prefix);

    } else {
        DEBUG_SYNC_LOG("tunnel mode AS.\n");
        ret = IS_EQUAL_SAE6T_AS_PREFIX(ipi6_addr, &handler->unicast_prefix);
    }
    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 経路同期処理関数
//!
//! v4/v6の経路同期要求を送信する。
//!
//! @param [in]     family      ファミリ(AF_INET/AF_INET6)
//! @param [in]     mode        経路同期処理モード
//! @param [in]     handler     アプリケーションハンドラー
//! @param [in]     route       経路同期管理テーブル
//! @param [in]     info        経路情報
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_sync_route(
        int family,
        int mode,
        struct m46e_handler_t* handler,
        void* route,
        void* info
)
{
    DEBUG_SYNC_LOG("m46e_sync_route start\n");

    struct m46e_command_t command;
    int count = 0;
    int ret;

    // パラメータチェック
    if (mode < RTSYNC_ROUTE_ADD || mode > RTSYNC_ROUTE_DEL) {
        m46e_logging(LOG_ERR, "error rtsync mode : %d\n", mode);
        return false;
    }

    // 経路同期要求送信処理
    if (family == AF_INET) {
        DEBUG_SYNC_LOG("V4 →  V6\n");

        // v4(Stub)側からの経路同期要求送信処理
        if (handler->conf->general->route_sync) {

            v4_route_info_table_t*          v4_route = (v4_route_info_table_t*)route;
            struct m46e_v4_route_info_t*   route_info = (struct m46e_v4_route_info_t*)info;

            // 経路の削除の場合、
            // GWのみ異なり、宛先とサブネットマスクが同じ経路がある場合は、
            // まだ、経路があるため、経路同期要求を送信しない。
            // すべての経路が削除された場合に、経路同期要求を送信する。
            if (mode == RTSYNC_ROUTE_DEL) {
                count = m46e_get_route_number(AF_INET, (void*)v4_route, (void*)route_info);
                if (count >= 1) {
                    DEBUG_SYNC_LOG("RTSYNC_ROUTE_DEL: Same v4 route num is %d.\n", count);
                    return false;
                }
            }

            // Backbone側へ経路同期要求の送信
            command.code = M46E_SYNC_ROUTE;
            command.req.info_route.type = mode;
            command.req.info_route.family = family;
            command.req.info_route.v4_route_info = *route_info;
            ret = m46e_send_sync_route_request_from_stub(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request : %s\n", strerror(-ret));
                return false;
            }
        }
        else {
            DEBUG_SYNC_LOG("m46e_sync_route skip\n");
        }
    }
    else if (family == AF_INET6) {
        DEBUG_SYNC_LOG("V6 →  V4\n");

        if (handler->conf->general->route_sync) {

            v6_route_info_table_t*          v6_route = (v6_route_info_table_t*)route;
            struct m46e_v6_route_info_t*   route_info6 = (struct m46e_v6_route_info_t*)info;

            if (mode == RTSYNC_ROUTE_DEL) {
                count = m46e_get_route_number(AF_INET6, (void*)v6_route, (void*)route_info6);
                if (count >= 1) {
                    DEBUG_SYNC_LOG("RTSYNC_ROUTE_DEL: Same v6 route num is %d\n", count);
                    return false;
                }
            }

            // Backbone側へ経路同期要求の送信
            command.code = M46E_SYNC_ROUTE;
            command.req.info_route.type = mode;
            command.req.info_route.family = family;
            command.req.info_route.v6_route_info = *route_info6;
            ret = m46e_send_sync_route_request_from_bb(handler, &command);
            if(ret < 0){
                m46e_logging(LOG_WARNING, "fail to send request : %s\n", strerror(-ret));
                return false;
            }
        }
        else {
            DEBUG_SYNC_LOG("m46e_sync_route skip\n");
        }
    }
    else {
        m46e_logging(LOG_ERR, "error rtsync family : %d\n", family);
        return false;
    }

    DEBUG_SYNC_LOG("m46e_sync_route end\n");
    return true;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief V6経路変換関数
//!
//! V6の経路情報をV4の経路情報へ変換する
//!
//! @param [in]  route_v6   V6経路情報
//!        [OUT] route_v4   V4経路情報
//!
//! @return OK:0   変換処理正常終了
//! @return NG:-1  変換処理異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_change_route_v6_to_v4(
        struct m46e_handler_t* handler,
        struct m46e_v6_route_info_t* route_v6,
        struct m46e_v4_route_info_t* route_v4
)
{
    int mode = -1;

    // 引数チェック
    if ((handler == NULL) || (route_v6 == NULL) || (route_v4 == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_change_route_v6_to_v4).");
        return -1;
    }

    mode = handler->conf->general->tunnel_mode;

    // -------------------------------------------------------------
    // 経路情報の変換
    //   ・経路同期による追加（true/false）
    //       trueを設定
    //   ・タイプ（追加/削除）
    //       V6のタイプをそのまま設定
    //   ・ether番号
    //       V4用仮想デバイスの番号を設定
    //   ・Destnation Address
    //       M46E : 下位32bitからV4アドレスを設定
    //       AS    : 80bitから32bitを取得しV4アドレスに設定
    //   ・SubnetMask
    //       M46E : V4で指定されたSubnetMaskに96引いた値を設定
    //       AS    : V4で指定されたSubnetMaskに80引いた値を設定
    //   ・Gateway
    //       0（0.0.0.0）を設定
    //   ・Source Address
    //       0（0.0.0.0）を設定
    //   ・metric
    //       0を設定
    // -------------------------------------------------------------
    route_v4->sync = true;

    if (mode == M46E_TUNNEL_MODE_NORMAL) {
        route_v4->in_dst.s_addr = route_v6->in_dst.s6_addr32[3];

        if (route_v4->in_dst.s_addr == 0) {
            // 変換後のV4アドレスが0の場合はエラー
            m46e_logging(LOG_ERR, "error  ipv4 address is 0\n");
            return -1;
        }

        route_v4->mask = route_v6->mask - SUBNET_MARGIN_M46E;
    }
    // M46E-ASの場合は、データセンタ側のみ変換
    else if (mode == M46E_TUNNEL_MODE_AS) {
        if((route_v6->mask <  SUBNET_MARGIN_AS) ||
           (route_v6->mask > (SUBNET_MARGIN_AS + IPV4_NETMASK_MAX))){
            // ネットマスクが不正なのでエラー
            m46e_logging(LOG_ERR, "error  netmask(%d) is out of range\n", route_v6->mask);
            return -1;
        }

        memcpy(&route_v4->in_dst.s_addr, &route_v6->in_dst.s6_addr16[5], sizeof(route_v4->in_dst.s_addr));

        if (route_v4->in_dst.s_addr == 0) {
            // 変換後のV4アドレスが0の場合はエラー
            m46e_logging(LOG_ERR, "error  ipv4 address is 0\n");
            return -1;
        }

        route_v4->mask = route_v6->mask - SUBNET_MARGIN_AS;
    }
    else {
        m46e_logging(LOG_ERR, "no sync mode\n");
        return -1;
    }

    route_v4->type = route_v6->type;
    route_v4->out_if_index = handler->v4_route_info->tunnel_dev_idx;
    route_v4->in_gw.s_addr = 0;
    route_v4->in_src.s_addr = 0;
    route_v4->priority = 0;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 経路同期関数
//!
//! V4⇒V6、V6⇒V4への経路の追加/削除の同期処理を行なう
//!
//! @param [in] mode   0(追加:RTSYNC_ROUTE_ADD)/1(削除:RTSYNC_ROUTE_DEL)
//!             family AF_INET/AF_INET6
//!             info   経路情報（V4の経路変更された場合はV4経路情報）
//!                            （V6の経路変更された場合はV6経路情報）
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
int m46e_rtsync_set_route(struct m46e_handler_t* handler, struct m46e_route_sync_request_t* request)
{
    int mode;
    int family;
    int ret = -1;

    DEBUG_SYNC_LOG("m46e_rtsync_set_route start.\n");

    // 引数チェック
    if ((handler == NULL) || (request == NULL) ) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_rtsync_set_route).");
        return -1;
    }

    // 初期化
    mode    = request->type;
    family  = request->family;

    // 変換後、経路表へ設定
    if (family == AF_INET) {
        DEBUG_SYNC_LOG("V4 →  V6\n");

        struct m46e_v4_route_info_t*   info =
            (struct m46e_v4_route_info_t*)&request->v4_route_info;
        struct m46e_v6_route_info_t    v6_info;
        memset(&v6_info, 0, sizeof(struct m46e_v6_route_info_t));

        // -------------------------------------------------------------
        // 経路情報の変換（V4 → V6）
        // -------------------------------------------------------------
        ret = m46e_change_route_v4_to_v6(handler, info, &v6_info);
        if (ret == 0) {

            _DS_(m46e_print_route6(STDOUT_FILENO, &v6_info);)

            // -------------------------------------------------------------
            // 経路(V6)のチェック
            // -------------------------------------------------------------
            // 既に登録済みの経路の場合は、追加しない
            ret = m46e_search_route(AF_INET6, (void*)handler->v6_route_info, (void*)&v6_info);
            if (mode == RTSYNC_ROUTE_ADD) {
                if (ret < 0) {
                    // 内部テーブルへ経路を追加する。
                    m46e_add_route(AF_INET6, (void*)handler->v6_route_info, (void *)&v6_info);
                }
                else {
                    m46e_logging(LOG_INFO, "This v6 route is already exists.\n");
                    return -1;
                }
            }
            // 削除対象であるが、経路が存在しない場合は削除しない
            else if (mode == RTSYNC_ROUTE_DEL) {
                if (ret >= 0 ) {
                    // 内部テーブルから経路を削除する。
                    m46e_del_route(AF_INET6, (void*)handler->v6_route_info, (void *)&v6_info);
                }
                else {
                    m46e_logging(LOG_ERR, "error (del) non v6 address\n");
                    return -1;
                }
            }

            // -------------------------------------------------------------
            // カーネルの経路情報の追加（V6）
            // -------------------------------------------------------------
            ret = m46e_ctrl_route_entry_sync(mode, AF_INET6, (void *)&v6_info);
        }
    }
    else if (family == AF_INET6) {
        DEBUG_SYNC_LOG("V6 →  V4\n");

        struct m46e_v6_route_info_t*   info =
                    (struct m46e_v6_route_info_t*)&request->v6_route_info;

        struct m46e_v4_route_info_t v4_info;
        memset(&v4_info, 0, sizeof(struct m46e_v4_route_info_t));

        // -------------------------------------------------------------
        // 経路情報の変換（V6 → V4）
        // -------------------------------------------------------------
        ret = m46e_change_route_v6_to_v4(handler, info, &v4_info);
        if (ret == 0) {

            _DS_(m46e_print_route(STDOUT_FILENO, &v4_info);)

            // -------------------------------------------------------------
            // 経路(V4)のチェック
            // -------------------------------------------------------------
            ret = m46e_search_route(AF_INET, (void*)handler->v4_route_info, (void*)&v4_info);
            if (mode == RTSYNC_ROUTE_ADD) {
                if (ret < 0) {
                    // 内部テーブルへ経路を追加する。
                    m46e_add_route(AF_INET, (void*)handler->v4_route_info, (void *)&v4_info);
                }
                else {
                    m46e_logging(LOG_INFO, "This v4 route is already exists.\n");
                    return -1;
                }
            }
            // 削除対象であるが、経路が存在しない場合は削除しない
            else if (mode == RTSYNC_ROUTE_DEL) {
                if (ret >= 0 ) {
                    // 内部テーブルから経路を削除する。
                    m46e_del_route(AF_INET, (void*)handler->v4_route_info, (void *)&v4_info);
                }
                else {
                    m46e_logging(LOG_ERR, "error (del) non v4 address\n");
                    return -1;
                }
            }

            // -------------------------------------------------------------
            // カーネルの経路情報の追加（V4）
            // -------------------------------------------------------------
            ret = m46e_ctrl_route_entry_sync(mode, AF_INET, (void *)&v4_info);
        }
    }
    else {
        m46e_logging(LOG_ERR, "error rtsync family : %d\n", family);
        return ret;
    }

    DEBUG_SYNC_LOG("m46e_rtsync_set_route end.\n");
    return ret;
}


