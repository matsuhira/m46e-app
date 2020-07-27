/******************************************************************************/
/* ファイル名 : m46eapp_mng_com_route.c                                       */
/* 機能概要   : 経路管理 共通ソースファイル                                   */
/* 修正履歴   : 2013.12.02 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
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
#include "m46eapp_rtnetlink.h"
#include "m46eapp_mng_com_route.h"
#include "m46eapp_mng_v4_route.h"
#include "m46eapp_mng_v6_route.h"
#include "m46eapp_sync_com_route.h"
#include "m46eapp_sync_v4_route.h"
#include "m46eapp_sync_v6_route.h"


///////////////////////////////////////////////////////////////////////////////
//! @brief 経路管理  v4/v6の同一経路数の取得関数
//!
//! v4/V6経路表から引数と同一の経路数を返す。
//! 「宛先アドレス」、「サブネットマスク」が同じ場合、
//! 同一アドレスと判定する。
//! ※注意：「ゲートウェイ」は含まない。
//!
//! @param [in] family  プロトコルファミリー(AF_INET or AF_INET6)
//! @param [in] route   検索するv4、またはV6経路テーブル
//! @param [in] entry   検索するv4、またはV6経路情報
//!
//! @return count  同一経路数
///////////////////////////////////////////////////////////////////////////////
int m46e_get_route_number(int family, void* route, void* entry)
{
    int i = 0;
    int count = 0;

    DEBUG_SYNC_LOG("m46e_get_route_number start.\n");

    // 引数チェック
    if ((route == NULL) || (entry == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_get_route_number).");
        return 0;
    }

    // IPv4
    if( family == AF_INET ){
        v4_route_info_table_t*          v4_route;
        struct m46e_v4_route_info_t*   v4_entry;

        v4_route = (v4_route_info_table_t*)route;
        v4_entry = (struct m46e_v4_route_info_t*)entry;

        // テーブル検索
        for (i = 0; i < v4_route->num; i++) {

            // 同一経路チェック
            if ((v4_route->table[i].in_dst.s_addr == v4_entry->in_dst.s_addr) &&
                    (v4_route->table[i].mask == v4_entry->mask)){
                count++;
                DEBUG_SYNC_LOG("hit %d.\n", i);
            }
        }
    }
    // IPv6
    else if (family == AF_INET6) {
        v6_route_info_table_t*          v6_route;
        struct m46e_v6_route_info_t*   v6_entry;

        v6_route = (v6_route_info_table_t*)route;
        v6_entry = (struct m46e_v6_route_info_t*)entry;
        // テーブル検索
        for (i = 0; i < v6_route->num; i++) {

            // 同一経路チェック
            if ((v6_route->table[i].in_dst.s6_addr32[0] == v6_entry->in_dst.s6_addr32[0]) &&
                    (v6_route->table[i].in_dst.s6_addr32[1] == v6_entry->in_dst.s6_addr32[1]) &&
                    (v6_route->table[i].in_dst.s6_addr32[2] == v6_entry->in_dst.s6_addr32[2]) &&
                    (v6_route->table[i].in_dst.s6_addr32[3] == v6_entry->in_dst.s6_addr32[3]) &&
                    (v6_route->table[i].mask == v6_entry->mask)){
                count++;
                DEBUG_SYNC_LOG("hit %d.\n", i);
            }
        }
    }

    DEBUG_SYNC_LOG("m46e_get_route_number end. count is %d.\n", count);

    return count;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief 経路管理  v4/v6の経路検索関数
//!
//! v4/V6経路表から引数と同一経路を検索し、そのインデックスを返す。
//! 「宛先アドレス」、「サブネットマスク」、「ゲートウェアドレス」が同じ場合、
//! 同一アドレスと判定する。
//!
//! @param [in] family  プロトコルファミリー(AF_INET or AF_INET6)
//! @param [in] route   検索するv4、またはV6経路テーブル
//! @param [in] entry   検索するv4、またはV6経路情報
//!
//! @return 0以上   検索成功（検索したテーブルindex）
//!         -1      検索失敗（テーブルに指定経路なし）
///////////////////////////////////////////////////////////////////////////////
int m46e_search_route(int family, void* route, void* entry)
{
    int i;
    int index = -1;

    DEBUG_SYNC_LOG("m46e_search_route start \n");

    // 引数チェック
    if ((route == NULL) || (entry == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_search_route).");
        return -1;
    }

    // IPv4
    if( family == AF_INET ){
        v4_route_info_table_t*          v4_route;
        struct m46e_v4_route_info_t*   v4_entry;

        v4_route = (v4_route_info_table_t*)route;
        v4_entry = (struct m46e_v4_route_info_t*)entry;

        // テーブル検索
        for (i = 0; i < v4_route->num; i++) {

            // データの存在チェック
            if ((v4_route->table[i].in_dst.s_addr == v4_entry->in_dst.s_addr) &&
                    (v4_route->table[i].in_gw.s_addr == v4_entry->in_gw.s_addr) &&
                    (v4_route->table[i].mask == v4_entry->mask)){
                DEBUG_SYNC_LOG("hit %d.\n", i);
                index = i;
                break;
            }
        }
    }
    // IPv6
    else if (family == AF_INET6) {
        v6_route_info_table_t*          v6_route;
        struct m46e_v6_route_info_t*   v6_entry;

        v6_route = (v6_route_info_table_t*)route;
        v6_entry = (struct m46e_v6_route_info_t*)entry;

        // テーブル検索
        for (i = 0; i < v6_route->num; i++) {

            // データの存在チェック
            if ((v6_route->table[i].in_dst.s6_addr32[0] == v6_entry->in_dst.s6_addr32[0]) &&
                    (v6_route->table[i].in_dst.s6_addr32[1] == v6_entry->in_dst.s6_addr32[1]) &&
                    (v6_route->table[i].in_dst.s6_addr32[2] == v6_entry->in_dst.s6_addr32[2]) &&
                    (v6_route->table[i].in_dst.s6_addr32[3] == v6_entry->in_dst.s6_addr32[3]) &&
                    (v6_route->table[i].in_gw.s6_addr32[0] == v6_entry->in_gw.s6_addr32[0]) &&
                    (v6_route->table[i].in_gw.s6_addr32[1] == v6_entry->in_gw.s6_addr32[1]) &&
                    (v6_route->table[i].in_gw.s6_addr32[2] == v6_entry->in_gw.s6_addr32[2]) &&
                    (v6_route->table[i].in_gw.s6_addr32[3] == v6_entry->in_gw.s6_addr32[3]) &&
                    (v6_route->table[i].mask == v6_entry->mask)){
                DEBUG_SYNC_LOG("hit %d.\n", i);
                index = i;
                break;
            }
        }
    }

    DEBUG_SYNC_LOG("m46e_search_route end (index = %d)\n", index);

    return index;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 経路管理  v4/v6の経路追加関数
//!
//! v4/V6経路テーブルにv4/V6経路を追加する
//!
//! @param [in] family  プロトコルファミリー(AF_INET or AF_INET6)
//! @param [in] route   追加するv4、またはV6経路テーブル
//! @param [in] entry   追加するv4、またはV6経路情報
//!
//! @return 0以上   追加成功（追加したテーブルindex）
//!         -1      追加失敗（テーブルに空きなし）
///////////////////////////////////////////////////////////////////////////////
int m46e_add_route(int family, void* route, void* entry)
{
    int num = -1;

    DEBUG_SYNC_LOG("m46e_add_route start \n");

    // 引数チェック
    if ((route == NULL) || (entry == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_add_route).");
        return -1;
    }

    // IPv4
    if( family == AF_INET ){
        v4_route_info_table_t*          v4_route = (v4_route_info_table_t*)route;
        struct m46e_v4_route_info_t*   v4_entry = (struct m46e_v4_route_info_t*)entry;

        if (v4_route->num < v4_route->max) {
            // 排他開始
            DEBUG_SYNC_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
            pthread_mutex_lock(&v4_route->mutex);

            // 最後尾に追加
            v4_route->table[v4_route->num] = *v4_entry;

            // 要素数のインクリメント
            v4_route->num++;
            DEBUG_SYNC_LOG("v4 table num = %d", v4_route->num);

            // 排他解除
            pthread_mutex_unlock(&v4_route->mutex);
            DEBUG_SYNC_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

            num = v4_route->num - 1;
        } else {
            m46e_logging(LOG_INFO, "v4 routing table is enough. num = %d\n", v4_route->num);
        }
    }
    // IPv6
    else if (family == AF_INET6) {
        v6_route_info_table_t*          v6_route = (v6_route_info_table_t*)route;
        struct m46e_v6_route_info_t*   v6_entry = (struct m46e_v6_route_info_t*)entry;

        if (v6_route->num < v6_route->max) {

            // 排他開始
            DEBUG_SYNC_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
            pthread_mutex_lock(&v6_route->mutex);

            // 最後尾に追加
            v6_route->table[v6_route->num] = *v6_entry;

            // 要素数のインクリメント
            v6_route->num++;
            DEBUG_SYNC_LOG("v6 table num = %d", v6_route->num);

            // 排他解除
            pthread_mutex_unlock(&v6_route->mutex);
            DEBUG_SYNC_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

            num = v6_route->num - 1;
        } else {
            m46e_logging(LOG_INFO, "v6 routing table is enough. num = %d\n", v6_route->num);
        }
    }

    DEBUG_SYNC_LOG("m46e_add_route end \n");
    return num;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 経路管理  v4/v6の経路削除関数
//!
//! v4/V6経路テーブルにv4/V6経路を削除する
//!
//! @param [in] family  プロトコルファミリー(AF_INET or AF_INET6)
//! @param [in] route   削除するv4、またはV6経路テーブル
//! @param [in] entry   削除するv4、またはV6経路情報
//!
//! @return 0以上   削除成功（削除したテーブルindex）
//!         -1      削除失敗（テーブルになし）
///////////////////////////////////////////////////////////////////////////////
int m46e_del_route(int family, void* route, void* entry)
{
    int i;
    int result = -1;

    DEBUG_SYNC_LOG("m46e_del_route start \n");

    // 引数チェック
    if ((route == NULL) || (entry == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_del_route).");
        return -1;
    }

    // IPv4
    if( family == AF_INET ){

        v4_route_info_table_t*          v4_route;
        v4_route = (v4_route_info_table_t*)route;

        // 排他開始
        DEBUG_SYNC_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&v4_route->mutex);

        // テーブル検索
        result = m46e_search_route(family, route, entry);
        if (result != -1) {

            // 削除した要素以降を詰める
            for (i = result; i < v4_route->num - 1; i++) {
                v4_route->table[i] = v4_route->table[i + 1];
            }

            // 最後の要素をクリア
            memset(&v4_route->table[v4_route->num], 0, sizeof(struct m46e_v4_route_info_t));

            // 要素数のディクリメント
            v4_route->num--;
            DEBUG_SYNC_LOG("v4 table num = %d", v4_route->num);
        }

        // 排他解除
        pthread_mutex_unlock(&v4_route->mutex);
        DEBUG_SYNC_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    }
    // IPv6
    else if (family == AF_INET6) {
        v6_route_info_table_t*          v6_route;
        v6_route = (v6_route_info_table_t*)route;

        // 排他開始
        DEBUG_SYNC_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&v6_route->mutex);

        // テーブル検索
        result = m46e_search_route(family, route, entry);
        if (result != -1) {

            // 削除した要素以降を詰める
            for (i = result; i < v6_route->num - 1; i++) {
                v6_route->table[i] = v6_route->table[i + 1];
            }

            // 最後の要素をクリア
            memset(&v6_route->table[v6_route->num], 0, sizeof(struct m46e_v6_route_info_t));

            // 要素数のディクリメント
            v6_route->num--;
            DEBUG_SYNC_LOG("v6 table num = %d", v6_route->num);
        }

        // 排他解除
        pthread_mutex_unlock(&v6_route->mutex);
        DEBUG_SYNC_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());
    }

    DEBUG_SYNC_LOG("m46e_del_route end (index = %d)\n", result);
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 経路管理  v4の管理デバイスの経路削除関数
//!
//! v4経路テーブルから、管理デバイスの経路を削除する
//!
//! @param [in/out] handler アプリケーションハンドラー
//! @param [in]     devidx  管理デバイスのインデックス
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_del_route_by_device(struct m46e_handler_t* handler, int devidx)
{
    int                     i, j = 0;
    v4_route_info_table_t*  v4_route = NULL;
    m46e_v4_route_info_t   info;


    DEBUG_SYNC_LOG("m46e_del_route_by_device start \n");

    // 引数チェック
    if (handler == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_del_route_by_device).");
        return ;
    }

    v4_route = handler->v4_route_info;

    // 排他開始
    DEBUG_SYNC_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&v4_route->mutex);

    // テーブル検索
    for (i = 0; i < v4_route->num; i++) {

        // データの存在チェック
        if (v4_route->table[i].out_if_index == devidx){
            DEBUG_SYNC_LOG("hit %d.\n", i);

            info = v4_route->table[i];

            // 削除した要素以降を詰める
            for (j = i; j < v4_route->num - 1; j++) {
                v4_route->table[j] = v4_route->table[j + 1];
            }

            // 最後の要素をクリア
            memset(&v4_route->table[v4_route->num], 0, sizeof(struct m46e_v4_route_info_t));

            // 要素数のディクリメント
            v4_route->num--;
            i--;
            DEBUG_SYNC_LOG("v4 table num = %d", v4_route->num);

            // 経路同期（削除）
            m46e_sync_route(AF_INET, RTSYNC_ROUTE_DEL, handler, (void*)v4_route, (void*)&info);
        }
    }

    // 排他解除
    pthread_mutex_unlock(&v4_route->mutex);
    DEBUG_SYNC_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    DEBUG_SYNC_LOG("m46e_del_route_by_device end\n");

    return ;
}


//////////////////////////////////////////////////////////////////////////////
//! @brief 経路表情報設定関数
//!
//! AttributeのTLV情報を経路情報設定用構造体に設定する
//!
//! @param [in]  family         ファミリ(AF_INET/AF_INET6)
//! @param [in]  rtm            受信した経路情報のpayloadアドレス
//! @param [in]  tb             Attribute type毎のポインタ配列
//! @param [out] data           v4またはv6の経路情報
//!
//! @return  0  成功
//!         -1  失敗
///////////////////////////////////////////////////////////////////////////////
int m46e_set_route_info(
        int family,
        struct rtmsg  *rtm,
        struct rtattr **tb,
        void *route_info
)
{

    // 引数チェック
    if ((rtm == NULL) || (tb == NULL) || (route_info == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_set_route_info).");
        return -1;
    }

    // IPv4
    if( family == AF_INET ) {

        ////////////////////////////////////////////////////////////
        // AttributeのTLV情報を経路情報設定用構造体に設定する
        ////////////////////////////////////////////////////////////
        struct m46e_v4_route_info_t* route_info4 =
                        (struct m46e_v4_route_info_t*)route_info;

        route_info4->type  = rtm->rtm_type;
        route_info4->mask  = rtm->rtm_dst_len;

        /* RTA_OIF */
        if( tb[RTA_OIF] ){
            route_info4->out_if_index = *(int*)RTA_DATA(tb[RTA_OIF]);
        }

        /* RTA_DST */
        if( tb[RTA_DST] ){
            memcpy(&route_info4->in_dst , RTA_DATA(tb[RTA_DST]), 4);
        }
        else {
            // anyを設定
            route_info4->in_dst.s_addr = 0;
        }

        if( tb[RTA_PREFSRC] ){
            memcpy(&route_info4->in_src , RTA_DATA(tb[RTA_PREFSRC]), 4);
        }
        else {
            // anyを設定
            route_info4->in_src.s_addr = 0;
        }

        /* RTA_GATEWAY */
        if( tb[RTA_GATEWAY] ){
            memcpy(&route_info4->in_gw , RTA_DATA(tb[RTA_GATEWAY]), 4);
        }
        else {
            // anyを設定
            route_info4->in_gw.s_addr = 0;
        }

        /* RTA_PRIORITY */
        if( tb[RTA_PRIORITY] ){
            route_info4->priority= *(int*)RTA_DATA(tb[RTA_PRIORITY]);
        }
        else {
            // anyを設定
            route_info4->priority = 0;
        }
    }
    // IPv6
    else if(family == AF_INET6 ){
        ////////////////////////////////////////////////////////////
        // AttributeのTLV情報を経路情報設定用構造体に設定する
        ////////////////////////////////////////////////////////////
        struct m46e_v6_route_info_t* route_info6 =
                        (struct m46e_v6_route_info_t*)route_info;

        route_info6->type  = rtm->rtm_type;
        route_info6->mask  = rtm->rtm_dst_len;

        /* RTA_OIF */
        if( tb[RTA_OIF] ){
            route_info6->out_if_index = *(int*)RTA_DATA(tb[RTA_OIF]);
        }

        /* RTA_DST */
        if( tb[RTA_DST] ){
            memcpy(&route_info6->in_dst , RTA_DATA(tb[RTA_DST]), sizeof(route_info6->in_dst));
        }
        else {
            route_info6->in_dst = in6addr_any;
        }

        /* RTA_SRC */
        if( tb[RTA_PREFSRC] ){
            memcpy(&route_info6->in_src , RTA_DATA(tb[RTA_PREFSRC]), sizeof(route_info6->in_src));
        }
        else {
            route_info6->in_src = in6addr_any;
        }

        /* RTA_GATEWAY */
        if( tb[RTA_GATEWAY] ){
            memcpy(&route_info6->in_gw , RTA_DATA(tb[RTA_GATEWAY]), sizeof(route_info6->in_gw));
        }
        else {
            route_info6->in_gw = in6addr_any;
        }

        /* RTA_PRIORITY */
        if( tb[RTA_PRIORITY] ){
            route_info6->priority = *(int*)RTA_DATA(tb[RTA_PRIORITY]);
        }
        else {
            route_info6->priority = 0;
        }
    } else {
       return -1;
    }

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
//! @brief 経路表情報更新関数
//!
//! RTNETLINKからの経路表変更通知受信時に呼ばれる
//!   追加通知の場合：経路表（内部）へ通知された経路情報を登録する
//!   削除通知の場合：経路表（内部）の通知された経路情報を削除する
//!
//! ※処理対照となる経路情報はUNICASTのみとする
//!
//! @param [in]  type           オプションタイプ(RTM_NEWROUTE/RTM_DELROUTE)
//! @param [in]  family         ファミリ(AF_INET/AF_INET6)
//! @param [in]  rtm            受信した経路情報のpayloadアドレス
//! @param [in]  tb             Attribute type毎のポインタ配列
//! @param [out] data           M46E アプリケーションハンドラー
//!
//! @return  0  成功
//!         -1  失敗
///////////////////////////////////////////////////////////////////////////////
int m46e_update_route_info(
        int type,
        int family,
        struct rtmsg  *rtm,
        struct rtattr **tb,
        void *data
)
{
    struct m46e_handler_t* handler;            /* M46E アプリケーションハンドラー */
    struct ifreq            ifr;                /* インタフェース名                */
    int                     fd;                 /* インタフェース名取得用FD         */
    int                     ret = 0;            /* 関数の戻り値用                  */

    DEBUG_SYNC_LOG("rtnetlink set route info start.\n");

    // 引数チェック
    if ((rtm == NULL) || (tb == NULL) || (data == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_update_route_info).");
        return -1;
    }

    // 初期化
    handler = (struct m46e_handler_t*)data;
    memset(&ifr, 0, sizeof(ifr));

    // IPv4
    if( family == AF_INET ) {

        DEBUG_SYNC_LOG("IPv4 route \n");

        // ゲートウェイまたはダイレクトな経路で、
        // メインのテーブルのみ対象
        if ((rtm->rtm_type == RTN_UNICAST) &&
            (rtm->rtm_table == RT_TABLE_MAIN)) {

            v4_route_info_table_t*          v4_route;
            struct m46e_v4_route_info_t    route_info;

            v4_route = (v4_route_info_table_t*)handler->v4_route_info;
            memset(&route_info, 0, sizeof(route_info));

            ////////////////////////////////////////////////////////////
            // AttributeのTLV情報を経路情報設定用構造体に設定する
            ////////////////////////////////////////////////////////////
            m46e_set_route_info(family, rtm, tb, (void*)&route_info);

            /* RTA_OIF */
            if( tb[RTA_OIF] ){
                // ether名を取得
                fd = socket( PF_INET, SOCK_DGRAM, 0 );
                ifr.ifr_ifindex = route_info.out_if_index;
                ioctl(fd, SIOCGIFNAME, &ifr);
                close(fd);
            }

            ///////////////////////////////////////////////////////////////////////
            // 受信した経路情報が管理するデバイスと一致しているかチェック
            ///////////////////////////////////////////////////////////////////////
            int flg = 0;
            struct m46e_list* iter;
            m46e_list_for_each(iter, &v4_route->device_list){
                int* index = iter->data;
                if (*index == route_info.out_if_index) {
                    flg = 1;
                    break;
                }
            }

            if (flg == 0) {
                // トンネルデバイスも処理対象
                if (v4_route->tunnel_dev_idx == route_info.out_if_index) {
                    DEBUG_SYNC_LOG("tunnel device\n");
                    flg = 1;
                }
            }

            // 管理するデバイスと一致しない場合は、処理終了。
            if (flg == 0) {
                DEBUG_SYNC_LOG("%s device is unmatch.\n", ifr.ifr_name);
                return -1;
            }

            ///////////////////////////////////////////////////////////////////////
            // 経路情報の追加
            ///////////////////////////////////////////////////////////////////////
            if (type == RTM_NEWROUTE) {

                DEBUG_SYNC_LOG("RTM_NEWROUTE route \n");

                // 既に同一ルートが登録されているかチェック
                if (m46e_search_route(AF_INET, (void*)v4_route, (void*)&route_info) == -1) {

                    // 内部テーブルへ経路を追加する。
                    // rtnetlinkで受信し、登録するエントリーには、falseを設定
                    route_info.sync = false;
                    ret = m46e_add_route(AF_INET, (void*)v4_route, (void *)&route_info);
                    if (ret == -1) {
                        return ret;
                    }

                    // トンネルデバイス以外で、管理するデバイス向けの経路経路の場合、経路同期する。
                    // トンネルデバイス向けの経路は、経路同期しない
                    if (v4_route->tunnel_dev_idx != route_info.out_if_index) {

                        // Backbone側へ経路同期要求を送信
                        m46e_sync_route(AF_INET, RTSYNC_ROUTE_ADD, handler, (void*)v4_route, (void*)&route_info);
                    }
                }
                else {
                    // 既に、同一ルートが登録されている。
                    char addr[INET_ADDRSTRLEN] = {0};
                    m46e_logging(LOG_INFO, "m46e v4 route is already exists\n");
                    m46e_logging(LOG_INFO, "in_dst      = %s\n",
                                inet_ntop(AF_INET, &route_info.in_dst, addr, sizeof(addr)));
                    m46e_logging(LOG_INFO, "netmask     = %d\n", route_info.mask);
                    m46e_logging(LOG_INFO, "device name = %s\n", ifr.ifr_name);
                }
            }
            ///////////////////////////////////////////////////////////////////////
            // 経路情報の削除
            ///////////////////////////////////////////////////////////////////////
            else if (type == RTM_DELROUTE) {
                DEBUG_SYNC_LOG("RTM_DELROUTE route \n");

                if (m46e_search_route(AF_INET, (void*)v4_route, (void*)&route_info) != -1) {

                    // 内部テーブルから経路を削除する。
                    // rtnetlinkで受信したエントリーには、falseを設定
                    route_info.sync = false;
                    m46e_del_route(AF_INET, (void*)v4_route, (void *)&route_info);

                    // トンネルデバイス向けの経路は、経路同期しない
                    if (v4_route->tunnel_dev_idx != route_info.out_if_index) {
                        // Backbone側へ経路同期処理
                        m46e_sync_route(AF_INET, RTSYNC_ROUTE_DEL, handler, (void*)v4_route, (void*)&route_info);
                    }
                }
                else {
                    char addr[INET_ADDRSTRLEN] = {0};
                    m46e_logging(LOG_INFO, "m46e v4 route is not exists\n");
                    m46e_logging(LOG_INFO, "in_dst      = %s\n",
                                inet_ntop(AF_INET, &route_info.in_dst, addr, sizeof(addr)));
                    m46e_logging(LOG_INFO, "netmask     = %d\n", route_info.mask);
                    m46e_logging(LOG_INFO, "device name = %s\n", ifr.ifr_name);
                }
            }
            else {
                DEBUG_SYNC_LOG("type(%d) is outside.\n", type);
                ret = -1;
            }
        }
        else {
            DEBUG_SYNC_LOG("rtm_type(%d) or rtm_table(%d) is outside.\n", rtm->rtm_type, rtm->rtm_table);
            ret = -1;
        }
    }
    // IPv6
    else if(family == AF_INET6 ){
        DEBUG_SYNC_LOG("IPv6 route \n");

        // ゲートウェイまたはダイレクトな経路で、
        // メインのテーブルのみ対象
        if ((rtm->rtm_type == RTN_UNICAST) &&
                (rtm->rtm_table == RT_TABLE_MAIN)) {

            v6_route_info_table_t*          v6_route;
            struct m46e_v6_route_info_t    route_info6;

            v6_route = (v6_route_info_table_t*)handler->v6_route_info;
            memset(&route_info6, 0, sizeof(route_info6));

            ////////////////////////////////////////////////////////////
            // AttributeのTLV情報を経路情報設定用構造体に設定する
            ////////////////////////////////////////////////////////////
            m46e_set_route_info(family, rtm, tb, (void*)&route_info6);

            /* RTA_OIF */
            if( tb[RTA_OIF] ){
                // ether名を取得
                fd = socket( PF_INET, SOCK_DGRAM, 0 );
                ifr.ifr_ifindex = route_info6.out_if_index;
                ioctl(fd, SIOCGIFNAME, &ifr);
                close(fd);
            }

            ///////////////////////////////////////////////////////////////////////
            // M46Eプレフィックスのアドレスのみ処理対象
            ///////////////////////////////////////////////////////////////////////
            if (!m46e_prefix_check(handler, &route_info6.in_dst)) {
                DEBUG_SYNC_LOG("IPv6 address has not m46e prefix\n");
                return -1;
             }

            ///////////////////////////////////////////////////////////////////////
            // 経路情報の追加
            ///////////////////////////////////////////////////////////////////////
            if (type == RTM_NEWROUTE) {
                DEBUG_SYNC_LOG("RTM_NEWROUTE route \n");

                if (m46e_search_route(AF_INET6, (void*)v6_route, (void*)&route_info6) == -1) {

                    // 内部テーブルへ経路を追加する。
                    // rtnetlinkで受信し、登録するエントリーには、falseを設定
                    route_info6.sync = false;
                    ret = m46e_add_route(AF_INET6, (void*)v6_route, (void *)&route_info6);
                    if (ret == -1) {
                        return ret;
                    }

                    // トンネルデバイス向けの経路は、経路同期しない
                    if (v6_route->tunnel_dev_idx != route_info6.out_if_index) {
                        // Backbone側へ経路同期処理
                        m46e_sync_route(AF_INET6, RTSYNC_ROUTE_ADD, handler, (void*)v6_route, (void*)&route_info6);
                    }
                    else {
                        DEBUG_SYNC_LOG("Skip sync route.\n");
                    }
                }
                else {
                    char addr[INET6_ADDRSTRLEN] = {0};
                    m46e_logging(LOG_INFO, "m46e v6 route is already exists\n");
                    m46e_logging(LOG_INFO, "in_dst      = %s\n",
                            inet_ntop(AF_INET6, &route_info6.in_dst, addr, sizeof(addr)));
                    m46e_logging(LOG_INFO, "netmask     = %d\n", route_info6.mask);
                    m46e_logging(LOG_INFO, "device name = %s\n", ifr.ifr_name);
                }
            }
            ///////////////////////////////////////////////////////////////////////
            // 経路情報の削除
            ///////////////////////////////////////////////////////////////////////
            else if (type == RTM_DELROUTE) {
                DEBUG_SYNC_LOG("RTM_DELROUTE route \n");

                if (m46e_search_route(AF_INET6, (void*)v6_route, (void*)&route_info6) != -1) {

                    // 内部テーブルから経路を削除する。
                    // rtnetlinkで受信したエントリーには、falseを設定
                    route_info6.sync = false;
                    m46e_del_route(AF_INET6, (void*)v6_route, (void *)&route_info6);

                    // トンネルデバイス向けの経路は、経路同期しない
                    if (v6_route->tunnel_dev_idx != route_info6.out_if_index) {
                        // Backbone側へ経路同期処理
                        m46e_sync_route(AF_INET6, RTSYNC_ROUTE_DEL, handler, (void*)v6_route, (void*)&route_info6);
                    }
                    else {
                        DEBUG_SYNC_LOG("Skip sync route.\n");
                    }
                }
                else {
                    char addr[INET6_ADDRSTRLEN] = {0};
                    m46e_logging(LOG_INFO, "m46e v6 route is not exists\n");
                    m46e_logging(LOG_INFO, "in_dst      = %s\n",
                            inet_ntop(AF_INET6, &route_info6.in_dst, addr, sizeof(addr)));
                    m46e_logging(LOG_INFO, "netmask     = %d\n", route_info6.mask);
                    m46e_logging(LOG_INFO, "device name = %s\n", ifr.ifr_name);
                }
            }
            else {
                DEBUG_SYNC_LOG("type(%d) is outside.\n", type);
                ret = -1;
            }
        }
        else {
            DEBUG_SYNC_LOG("rtm_type(%d) or rtm_table(%d) is outside.\n", rtm->rtm_type, rtm->rtm_table);
            ret = -1;
        }
    } else {
        DEBUG_SYNC_LOG("family(%d) is outside.\n", family);
        ret = -1;
    }

    DEBUG_SYNC_LOG("rtnetlink set route info end.\n");
    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 経路表情報取得関数
//!
//! RTNETLINKを使用して、ルーティングテーブル内の経路情報を取得し、
//! 内部IPv4、またはIPv6経路表に設定する。
//!
//! @param [in]         family      プロトコルファミリー(AF_INET or AF_INET6)
//! @param [in, out]    handler     M46E アプリケーションハンドラー
//!
//! @return RESULT_OK  正常終了
//! @return RESULT_NG  異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_get_route_entry(int family, void* handler)
{

    char*               req;                /* Request netlink message buffer    */
    struct nlmsghdr*    nlmsg;              /* Netlink message header address    */
    struct rtmsg*       rtm;                /* Netlink Rtmsg address             */
    int                 sock_fd;            /* Netlink socket descriptor        */
    struct sockaddr_nl  local;              /* Local netlink socket address     */
    uint32_t            seq;                /* Sequence number(for netlink msg) */
    int                 ret;                /* return value                     */
    int                 errcd;              /* error code                       */


    // 引数チェック
    if (handler == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_get_route_entry).");
        return -1;
    }

    req = malloc(NETLINK_SNDBUF); /* 16kbyte */
    if( req == NULL ){
        m46e_logging(LOG_ERR, "recv buf malloc ng. errno=%d\n",errno);
        return RESULT_SYSCALL_NG;
    }

    /* netlink message address set */
    nlmsg = (struct nlmsghdr*)req;
    rtm = (struct rtmsg*)(req + NLMSG_HDRLEN);

    /* ------------------------------------ */
    /* Netlink socket open                  */
    /* ------------------------------------ */
    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);

    if(ret != RESULT_OK){
        // メモリの解放
        free(req);

        /* socket open error */
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return ret;
    }

    /* ------------------------------------ */
    /* Set netlink message                  */
    /* ------------------------------------ */

    /* struct nlmsghdr */
    nlmsg->nlmsg_len    = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlmsg->nlmsg_type   = RTM_GETROUTE;
    nlmsg->nlmsg_flags  = NLM_F_REQUEST | NLM_F_DUMP;
    nlmsg->nlmsg_seq    = seq;             /* sequence no */
    nlmsg->nlmsg_pid    = 0;               /* To kernel   */

    /* struct rtmsg */
    rtm->rtm_family     = family;
    rtm->rtm_dst_len    = 0;
    rtm->rtm_src_len    = 0;
    rtm->rtm_tos        = 0;
    rtm->rtm_table      = RT_TABLE_MAIN;
    ////////////////////////////////////////////////////////////////////////////
    // ■rtm_protocol
    // RTPROT_UNSPEC    不明
    // RTPROT_REDIRECT  ICMPリダイレクトによる（現在は用いられない）
    // RTPROT_KERNEL    カーネルによる
    // RTPROT_BOOT      ブート時
    // RTPROT_STATIC    管理者による
    ////////////////////////////////////////////////////////////////////////////
    rtm->rtm_protocol   = RTPROT_UNSPEC;
    ////////////////////////////////////////////////////////////////////////////
    // ■rtm_scope
    // RT_SCOPE_UNIVERCE グローバルな経路
    // RT_SCOPE_SITE    ローカルな自律システムにおける内部経路
    // RT_SCOPE_LINK    このLINK上の経路
    // RT_SCOPE_HOST    ローカルホスト上の経路
    // RT_SCOPE_NOWHERE 行き先存在しない経路
    ////////////////////////////////////////////////////////////////////////////
    rtm->rtm_scope      = RT_SCOPE_UNIVERSE;
    ////////////////////////////////////////////////////////////////////////////
    // ■rtm_type
    // RTN_UNSPEC       未知の経路
    // RTN_UNICAST      ゲートウェイまたはダイレクトな経路
    // RTN_LOCAL        ローカルインターフェースの経路
    // RTN_BROADCAST    ローカルなブロードキャスト経路（ブロードキャストとして送信）
    // RTN_ANYCAST      ローカルなブロードキャスト経路（ユニキャストとして送信）
    // RTN_MULTICAST    マルチキャスト経路
    // RTN_BLACKHOLE    パケットを捨てる経路
    // RTN_UNREACHABLE  到達できない行き先
    // RTN_PROHIBIT     パケットを拒否する経路
    // RTN_THROW        経路検索を別のテーブルで継続
    // RTN_NAT          ネットワークアドレスの変換ルール
    // RTN_XRESOLVE     外部レゾルバを参照
    ////////////////////////////////////////////////////////////////////////////
    rtm->rtm_type       = RTN_UNSPEC;
    ////////////////////////////////////////////////////////////////////////////
    // ■rtm_flags
    // RT_F_UNIVERCE    経路が変更されるとrtnetlinkを通してユーザに通知
    // RT_F_CLONED      経路は他の経路によって複製された
    // RT_F_EQUALIZE    マルチキャストイコライザー（実装されていない）
    ////////////////////////////////////////////////////////////////////////////
    rtm->rtm_flags      = 0;   // 0は定義されていない

    /* ---------------------------------------------------- */
    /* Netlink message send                                 */
    /* ---------------------------------------------------- */
    ret = m46e_netlink_send(sock_fd, seq, nlmsg, &errcd);

    if(ret != RESULT_OK){
        // メモリの解放
        free(req);

        m46e_netlink_close(sock_fd);

        /* Netlink message send error */
        m46e_logging(LOG_ERR, "err netlink message send\n");
        return ret;
    }

    /* ---------------------------------------------------- */
    /* Netlink message Route entry recieve                  */
    /* ---------------------------------------------------- */
    ret = m46e_netlink_recv(sock_fd, &local, seq, &errcd,
                      m46e_rtnl_parse_rcv, handler);

    if(ret != RESULT_OK){
        // メモリの解放
        free(req);

        m46e_netlink_close(sock_fd);

        /* Netlink message ack recv error */
        m46e_logging(LOG_ERR, "err netlink message send\n");
        return ret;
    }

    m46e_netlink_close(sock_fd);

    // メモリの解放
    free(req);

    return RESULT_OK;
}

