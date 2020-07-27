/******************************************************************************/
/* ファイル名 : m46eapp_mng_v4_route_data.h                                   */
/* 機能概要   : v4経路管理データ ヘッダファイル                               */
/* 修正履歴   : 2013.06.06 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_MNG_V4_ROUTE_DATA_H__
#define __M46EAPP_MNG_V4_ROUTE_DATA_H__

#include <stdbool.h>
#include <netinet/in.h>
#include "m46eapp_list.h"

///////////////////////////////////////////////////////////////////////////////
//! IPv4 経路情報(エントリー情報)
///////////////////////////////////////////////////////////////////////////////
struct m46e_v4_route_info_t
{
    int             type;           ///< 経路タイプ
    struct in_addr  in_dst;         ///< 行き先アドレス
    int             mask;           ///< 行き先サブネットマスク
    struct in_addr  in_gw;          ///< ゲートウェイ
    struct in_addr  in_src;         ///< 送信元アドレス
    int             out_if_index;   ///< 出力インタフェースのindex
    int             priority;       ///< 優先度
    bool            sync;           ///< true: 情報元が経路同期要求を表す
                                    ///< false:情報元がrtnetlinkを表す

};
typedef struct m46e_v4_route_info_t m46e_v4_route_info_t;

///////////////////////////////////////////////////////////////////////////////
//! IPv4 経路情報 テーブル
///////////////////////////////////////////////////////////////////////////////
struct v4_route_info_table_t
{
    pthread_mutex_t         mutex;          ///< IPv4 経路情報 テーブルmutex
    int                     shm_id;         ///< 共有メモリID
    int                     max;            ///< エントリー最大数
    int                     num;            ///< エントリー数
    int                     tunnel_dev_idx; ///< トンネルデバイスのインデックス
    m46e_list              device_list;    ///< デバイスのインデックスのリスト
    int                     t_shm_id;       ///< IPv4 経路情報 mutex
    m46e_v4_route_info_t*  table;          ///< IPv4 経路情報

};
typedef struct v4_route_info_table_t v4_route_info_table_t;

#endif // __M46EAPP_MNG_V4_ROUTE_DATA_H__

