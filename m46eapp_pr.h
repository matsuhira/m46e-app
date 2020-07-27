/******************************************************************************/
/* ファイル名 : m46eapp_pr.h                                                  */
/* 機能概要   : M46E Prefix Resolutionヘッダファイル                          */
/* 修正履歴   : 2013.08.01 Y.Shibata   新規作成                               */
/* 修正履歴   : 2013.08.22 H.KoganemaruM46E-PR機能拡張                        */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_PR_H__
#define __M46EAPP_PR_H__

#include <stdbool.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <pthread.h>


#include "m46eapp_list.h"
#include "m46eapp_pr_struct.h"
#include "m46eapp_command.h"

//! M46E PR Table 最大エントリー数
#define PR_MAX_ENTRY_NUM    4096

//! CIDR2(プレフィックス)をサブネットマスク(xxx.xxx.xxx.xxx)へ変換
#define PR_CIDR2SUBNETMASK(cidr, mask) mask.s_addr = (cidr == 0 ? 0 : htonl(0xFFFFFFFF << (32 - cidr)))

// M46E prefix + PlaneID判定
#define IS_EQUAL_M46E_PREFIX(a, b) \
        (((__const uint32_t *) (a))[0] == ((__const uint32_t *) (b))[0]     \
         && ((__const uint32_t *) (a))[1] == ((__const uint32_t *) (b))[1]  \
         && ((__const uint32_t *) (a))[2] == ((__const uint32_t *) (b))[2])

// M46E-AS prefix + PlaneID判定
#define IS_EQUAL_M46E_AS_PREFIX(a, b) \
        (((__const uint32_t *) (a))[0] == ((__const uint32_t *) (b))[0]     \
         && ((__const uint32_t *) (a))[1] == ((__const uint32_t *) (b))[1]  \
         && ((__const uint16_t *) (a))[4] == ((__const uint16_t *) (b))[4])

// M46E-PR prefix + PlaneID判定
#define IS_EQUAL_M46E_PR_PREFIX(a, b) IS_EQUAL_M46E_PREFIX(a, b)

//M46E-PRコマンドエラーコード
enum m46e_pr_command_error_code
{
    M46E_PR_COMMAND_NONE,
    M46E_PR_COMMAND_MODE_ERROR,      ///<動作モードエラー
    M46E_PR_COMMAND_EXEC_FAILURE,    ///<コマンド実行エラー
    M46E_PR_COMMAND_ENTRY_FOUND,     ///<エントリ登録有り
    M46E_PR_COMMAND_ENTRY_NOTFOUND,  ///<エントリ登録無し
    M46E_PR_COMMAND_MAX
};

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
m46e_pr_table_t* m46e_pr_init_pr_table(struct m46e_handler_t* handler);
void m46e_pr_destruct_pr_table(m46e_pr_table_t* pr_handler);
bool m46e_pr_add_config_entry(m46e_pr_config_table_t* table, m46e_pr_config_entry_t* entry);
bool m46e_pr_del_config_entry(m46e_pr_config_table_t* table, struct in_addr* addr, int mask);
bool m46e_pr_set_config_enable(m46e_pr_config_table_t* table, struct in_addr* addr, int mask, bool enable);
m46e_pr_config_entry_t* m46e_search_pr_config_table( m46e_pr_config_table_t* table, struct in_addr* addr, int cidr);

void m46e_pr_config_table_dump(const m46e_pr_config_table_t* table);

bool m46e_pr_add_entry(m46e_pr_table_t* table, m46e_pr_entry_t* entry);
bool m46e_pr_del_entry(m46e_pr_table_t* table, struct in_addr* addr, int mask);
bool m46e_pr_set_enable(m46e_pr_table_t* table, struct in_addr* addr, int mask, bool enable);
m46e_pr_entry_t* m46e_search_pr_table(m46e_pr_table_t* table, struct in_addr* addr, int cidr);
void m46e_pr_table_dump(const m46e_pr_table_t* table);

m46e_pr_entry_t* m46e_pr_entry_search_stub(m46e_pr_table_t* table, struct in_addr* addr);
bool m46e_pr_prefix_check( m46e_pr_table_t* table, struct in6_addr* addr);

bool m46e_pr_plane_prefix(struct in6_addr* inaddr, int cidr, char* plane_id, struct in6_addr* outaddr);
m46e_pr_entry_t* m46e_pr_conf2entry(struct m46e_handler_t* handler, m46e_pr_config_entry_t* conf);
m46e_pr_entry_t* m46e_pr_command2entry( struct m46e_handler_t* handler, struct m46e_pr_entry_command_data* data);
m46e_pr_config_entry_t* m46e_pr_command2conf(struct m46e_handler_t* handler, struct m46e_pr_entry_command_data* data);

bool m46eapp_pr_check_network_addr(struct in_addr *addr, int cidr);
bool m46eapp_pr_convert_network_addr(struct in_addr *inaddr, int cidr, struct in_addr *outaddr);

bool m46e_pr_add_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req);
bool m46e_pr_del_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req);
bool m46e_pr_delall_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req);
bool m46e_pr_enable_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req);
bool m46e_pr_disable_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req);
void m46e_pr_show_entry_pr_table(m46e_pr_table_t* pr_handler, int fd, char *plane_id);
void m46e_pr_print_error(int fd, enum m46e_pr_command_error_code error_code);

#endif // __M46EAPP_PR_H__

