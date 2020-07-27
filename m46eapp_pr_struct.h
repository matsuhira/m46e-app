/******************************************************************************/
/* ファイル名 : m46eapp_pr_struct.h                                           */
/* 機能概要   : M46E Prefix Resolution 構造体定義ヘッダファイル               */
/* 修正履歴   : 2013.08.01 Y.Shibata   新規作成                               */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_PR_STRUCT_H__
#define __M46EAPP_PR_STRUCT_H__

#include <stdbool.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <pthread.h>


#include "m46eapp_list.h"

////////////////////////////////////////////////////////////////////////////////
// 構造体
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//! M46E-PR Entry 構造体
///////////////////////////////////////////////////////////////////////////////
typedef struct _m46e_pr_entry_t
{
    bool                    enable;             ///< エントリーか有効(true)/無効(false)かを表すフラグ
    struct in_addr          v4addr;             ///< 送信先のIPv4ネットワークアドレス（ホスト部のアドレスは0）
    struct in_addr          v4mask;             ///< xxx.xxx.xxx.xxx形式のIPv4サブネットマスク
    int                     v4cidr;             ///< CIDR形式でのIPv4サブネットマスク
    struct in6_addr         pr_prefix_planeid;  ///< M46E-PR address prefixのIPv6アドレス+Plane ID
    struct in6_addr         pr_prefix;          ///< M46E-PR address prefixのIPv6アドレス(表示用)
    int                     v6cidr;             ///< M46E-PR address prefixのサブネットマスク長+IPv4サブネットマスク長(表示用)
} m46e_pr_entry_t;

///////////////////////////////////////////////////////////////////////////////
//! M46E-PR Table 構造体
///////////////////////////////////////////////////////////////////////////////
typedef struct _m46e_pr_table_t
{
    pthread_mutex_t         mutex;          ///< 排他用のmutex
    int                     num;            ///< M46E-PR Entry 数
    m46e_list              entry_list;     ///< M46E-PR Entry list
} m46e_pr_table_t;

#endif // __M46EAPP_PR_STRUCT_H__

