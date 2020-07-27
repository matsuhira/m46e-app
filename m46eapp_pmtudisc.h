/******************************************************************************/
/* ファイル名 : m46eapp_pmtudisc.h                                            */
/* 機能概要   : Path MTU Discoveryヘッダファイル                              */
/* 修正履歴   : 2012.03.06 S.Yoshikawa 新規作成                               */
/*              2012.08.08 T.Maeda Phase4向けに全面改版                       */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_PMTUDISC_H__
#define __M46EAPP_PMTUDISC_H__

#include "m46eapp_config.h"

struct in6_addr;

////////////////////////////////////////////////////////////////////////////////
// 構造体
////////////////////////////////////////////////////////////////////////////////
typedef struct m46e_pmtud_t m46e_pmtud_t;

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
// Path MTU Discovery初期化関数
m46e_pmtud_t* m46e_init_pmtud(m46e_config_pmtud_t* config, int default_mtu);

// Path MTU Discovery終了関数
void m46e_end_pmtud(m46e_pmtud_t* pmtud_handler);

// Path MTU Discoveryp再起動関数
m46e_pmtud_t*  m46e_restart_pmtud(m46e_pmtud_t* pmtud_handler, int default_mtu, int type);

// Path MTU Discovery関数
void m46e_path_mtu_set(m46e_pmtud_t* pmtud_handler, struct in6_addr* dst, int pmtu);

// PMTU値取得関数
int  m46e_path_mtu_get(m46e_pmtud_t* pmtud_handler, const struct in6_addr* v6daddr);

// PMTUログ出力関数
void m46e_pmtu_print_table(m46e_pmtud_t* pmtud_handler, int fd);

#endif // __M46EAPP_PMTUDISC_H__
