/******************************************************************************/
/* ファイル名 : m46eapp_mng_v6_route_data.h                                   */
/* 機能概要   : v6経路同期データ ヘッダファイル                               */
/* 修正履歴   : 2013.07.19 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_MNG_V6_ROUTE_DATA_H__
#define __M46EAPP_MNG_V6_ROUTE_DATA_H__

#include <stdbool.h>
#include <netinet/in.h>

///////////////////////////////////////////////////////////////////////////////
//! IPv6 経路情報(エントリー情報)
///////////////////////////////////////////////////////////////////////////////
struct m46e_v6_route_info_t
{
    int             type;           ///< 経路タイプ
    struct in6_addr in_dst;         ///< 行き先アドレス
    int             mask;           ///< 行き先サブネットマスク
    struct in6_addr in_gw;          ///< ゲートウェイ
    struct in6_addr in_src;         ///< 送信元アドレス
    int             out_if_index;   ///< 出力インタフェースのindex
    int             priority;       ///< 優先度
    bool            sync;           ///< 経路同期によって追加されたことを表す

};
typedef struct m46e_v6_route_info_t m46e_v6_route_info_t;

///////////////////////////////////////////////////////////////////////////////
//! IPv6 経路情報 テーブル
///////////////////////////////////////////////////////////////////////////////
struct v6_route_info_table_t
{
    pthread_mutex_t         mutex;          ///< mutex
    int                     shm_id;         ///< 共有メモリID
    int                     max;            ///< エントリー最大数
    int                     num;            ///< エントリー数
    int                     tunnel_dev_idx; ///< トンネルデバイスのインデックス
    int                     t_shm_id;       ///< 経路情報テーブルの共有メモリID
    m46e_v6_route_info_t*  table;          ///< IPv6 経路情報テーブル

};
typedef struct v6_route_info_table_t v6_route_info_table_t;

#endif // __M46EAPP_MNG_V6_ROUTE_DATA_H__

