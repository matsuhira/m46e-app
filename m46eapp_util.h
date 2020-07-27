/******************************************************************************/
/* ファイル名 : m46eapp_util.h                                                */
/* 機能概要   : 共通関数 ヘッダファイル                                       */
/* 修正履歴   : 2011.12.20 T.Maeda 新規作成                                   */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#ifndef __M46EAPP_UTIL_H__
#define __M46EAPP_UTIL_H__

#include <stdbool.h>
#include <sys/uio.h>

struct in_addr;
struct in6_addr;

////////////////////////////////////////////////////////////////////////////////
// 外部マクロ定義
////////////////////////////////////////////////////////////////////////////////
//! 値の大きい方を返すマクロ
#define max(a, b) ((a) > (b) ? (a) : (b))
//! 値の小さい方を返すマクロ
#define min(a, b) ((a) < (b) ? (a) : (b))


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
bool m46e_util_get_ipv4_addr(const char* dev, struct in_addr* addr);
bool m46e_util_get_ipv4_mask(const char* dev, struct in_addr* mask);
bool m46e_util_get_ipv6_addr(const char* dev, struct in6_addr* addr);
bool m46e_util_is_broadcast_mac(const unsigned char* mac_addr);
unsigned short m46e_util_checksum(unsigned short *buf, int size);
unsigned short m46e_util_checksumv(struct iovec vec[], int vec_size);

#endif // __M46EAPP_UTIL_H__
