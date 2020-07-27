/******************************************************************************/
/* ファイル名 : m46eapp.h                                                     */
/* 機能概要   : M46E共通ヘッダファイル                                        */
/* 修正履歴   : 2012.07.12 T.Maeda 新規作成                                   */
/*              2013.08.01 Y.Shibata  M46E-PR拡張機能                         */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_H__
#define __M46EAPP_H__

#include <unistd.h>
#include <netinet/in.h>

#include "m46eapp_config.h"
#include "m46eapp_statistics.h"
#include "m46eapp_pmtudisc.h"
#include "m46eapp_pr_struct.h"
#include "m46eapp_mng_v4_route_data.h"
#include "m46eapp_mng_v6_route_data.h"


////////////////////////////////////////////////////////////////////////////////
// 外部マクロ定義
////////////////////////////////////////////////////////////////////////////////
//!IPv6のMTU最小値
#define IPV6_MIN_MTU    1280
//!IPv6のプレフィックス最大値
#define IPV6_PREFIX_MAX 128
//!IPv4のプレフィックス最大値
#define IPV4_PREFIX_MAX 32
//!ポート番号のbit長の最大値
#define PORT_BIT_MAX    16

////////////////////////////////////////////////////////////////////////////////
// 外部構造体定義
////////////////////////////////////////////////////////////////////////////////
//! M46Eアプリケーションハンドラ
struct m46e_handler_t
{
    m46e_config_t*     conf;               ///< 設定情報
    m46e_statistics_t* stat_info;          ///< 統計情報
    m46e_pmtud_t*      pmtud_handler;      ///< Path MTU情報管理
    m46e_pr_table_t*   pr_handler;         ///< M46E-PR情報管理
    struct in6_addr     unicast_prefix;     ///< M46E ユニキャストプレフィックス
    struct in6_addr     src_addr_unicast_prefix;     ///< M46E-PR ユニキャストプレフィックス(送信元アドレス用)
    struct in6_addr     multicast_prefix;   ///< M46E マルチキャストプレフィックス
    pid_t               stub_nw_pid;        ///< StubネットワークのプロセスID
    int                 comm_sock[2];       ///< 内部コマンド用ソケットディスクリプタ
    int                 signalfd;           ///< シグナル受信用ディスクリプタ
    sigset_t            oldsigmask;         ///< プロセス起動時のシグナルマスク
    int                 sync_route_sock[2]; ///< 経路同期用ソケットディスクリプタ
    v4_route_info_table_t* v4_route_info;   ///< IPv4 経路情報
    v6_route_info_table_t* v6_route_info;   ///< IPv6 経路情報
};

#endif // __M46EAPP_H__

