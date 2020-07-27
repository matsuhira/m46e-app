/******************************************************************************/
/* ファイル名 : m46ectl_command.h                                             */
/* 機能概要   : 内部コマンドクラス ヘッダファイル                             */
/* 修正履歴   : 2013.07.03 Y.Shibata 新規作成                                 */
/*              2013.09.12 H.Koganemaru 動的定義変更機能追加                  */
/*              2013.10.03 Y.Shibata  M46E-PR拡張機能                         */
/*              2014.01.21 M.Iwatsubo M46E-PR外部連携機能                     */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46ECTL_COMMAND_H__
#define __M46ECTL_COMMAND_H__

#include <stdbool.h>
#include "m46eapp_command_data.h"

////////////////////////////////////////////////////////////////////////////////
// デファイン定義
////////////////////////////////////////////////////////////////////////////////
//! キー名
#define OPT_PHYSICAL_DEV    "physical_dev"
#define OPT_DEV_NAME        "name"
#define OPT_IPV4_ADDRESS    "ipv4_address"
#define OPT_IPV4_GATEWAY    "ipv4_gateway"
#define OPT_MTU             "mtu"
#define OPT_HWADDR          "hwaddr"

//! コマンドラインの１行あたりの最大文字数
#define OPT_LINE_MAX            256

//! MTU の最小値
#define OPT_DEVICE_MTU_MIN      548

//! MTU の最大値
#define OPT_DEVICE_MTU_MAX      65521

//! サブネットマスク の最小値
#define OPT_IPV4_NETMASK_MIN    1
#define OPT_IPV6_PREFIX_MIN 1

//! サブネットマスク の最大値
#define OPT_IPV4_NETMASK_MAX    32
#define OPT_IPV6_PREFIX_MAX 128

//! PMTUモードの最小値
#define OPT_PMTUD_MODE_MIN 0

//! PMTUモードの最大値
#define OPT_PMTUD_MODE_MAX 2

//! PMTUタイマの最小値
#define OPT_PMTUD_EXPIRE_TIME_MIN 301

//! PMTUタイマの最大値
#define OPT_PMTUD_EXPIRE_TIME_MAX 65535

//! トンネルデバイスMTU長の最小値
#define OPT_TUNNEL_MTU_MIN 1280

//! トンネルデバイスMTU長の最大値
#define OPT_TUNNEL_MTU_MAX 65521

//! IPv6 Prefix長最小値
#define CONFIG_IPV6_PREFIX_MIN 1

//! IPv6 Prefix長最大値
#define CONFIG_IPV6_PREFIX_MAX 128

//! KEY VALUE行判定用正規表現 (KEY=VALUE 形式)
#define OPT_REGEX "(\\S+)=(\\S+)"

//! 設定ファイルを読込む場合の１行あたりの最大文字数
#define OPT_LINE_MAX 256

//! デバイス操作コマンド引数
#define DYNAMIC_OPE_ARGS_NUM_MAX    6
#define DYNAMIC_OPE_DEVICE_MIN_ARGS 6
#define DYNAMIC_OPE_DEVICE_MAX_ARGS 11

//! debugログモード設定コマンド引数
#define DYNAMIC_OPE_DBGLOG_ARGS 6

//! Path MTU Discovery モード値設定コマンド引数
#define DYNAMIC_OPE_PMTUMD_ARGS 6

//! Path MTU Discovery Timer値設定コマンド引数
#define DYNAMIC_OPE_PMTUTM_ARGS 6

//! 強制フラグメントモード設定コマンド引数
#define DYNAMIC_OPE_FFRAG_ARGS 6

//! デフォルトゲートウェイ設定コマンド引数
#define DYNAMIC_OPE_DEFGW_ARGS 6

//! トンネルデバイスMTU設定コマンド引数
#define DYNAMIC_OPE_TUNMTU_ARGS 6

//! 収容デバイスMTU設定コマンド引数
#define DYNAMIC_OPE_DEVMTU_ARGS 7

//! Stub側汎用コマンド実行コマンド引数
#define DYNAMIC_OPE_EXEC_CMD_ARGS 6

//! M46E-PR Entry追加コマンド引数
#define ADD_PR_OPE_MIN_ARGS 7
#define ADD_PR_OPE_MAX_ARGS 8

//! M46E-PR Entry削除コマンド引数
#define DEL_PR_OPE_ARGS 6

//! M46E-PR Entry全削除コマンド引数
#define DELALL_PR_OPE_ARGS 5

//! M46E-PR Entry活性化コマンド引数
#define ENABLE_PR_OPE_ARGS 6

//! M46E-PR Entry非活性化コマンド引数
#define DISABLE_PR_OPE_ARGS 6

//! M46E-PR Entry表示コマンド引数
#define SHOW_PR_OPE_ARGS 5

//! Config情報表示コマンド引数
#define SHOW_CONF_OPE_ARGS 5

//! 統計情報表示コマンド引数
#define SHOW_STAT_OPE_ARGS 5

//! Path MTU Discovery Table表示コマンド引数
#define SHOW_PMTU_OPE_ARGS 5

//! M46E-PR Entry一括設定ファイル読込コマンド引数
#define OPE_NUM_LOAD_PR 6

#define CMD_IPV4_NETMASK_MIN 0
#define CMD_IPV4_NETMASK_MAX 32

// 設定ファイルのbool値
#define OPT_BOOL_ON  "on"
#define OPT_BOOL_OFF "off"
#define OPT_BOOL_ENABLE  "enable"
#define OPT_BOOL_DISABLE "disable"
#define OPT_BOOL_NONE    ""

#define DELIMITER   " \t"

///////////////////////////////////////////////////////////////////////////////
//! KEY/VALUE格納用構造体
///////////////////////////////////////////////////////////////////////////////
typedef struct _opt_keyvalue_t {
    char key[OPT_LINE_MAX];   ///< KEYの値
    char value[OPT_LINE_MAX]; ///< VALUEの値
} opt_keyvalue_t;


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
bool m46e_command_device_set_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_judge_name( char* line);
bool m46e_command_dbglog_set_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_pmtumd_set_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_pmtutm_set_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_ffrag_set_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_defgw_set_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_tunmtu_set_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_devmtu_set_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_exec_cmd_inet_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_add_pr_entry_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_del_pr_entry_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_enable_pr_entry_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_disable_pr_entry_option(int num, char* opt[], struct m46e_command_t* command);
bool m46e_command_load_pr(char* filename, struct m46e_command_t* command, char* name);
bool m46e_command_pr_send(struct m46e_command_t* command, char* name);
bool m46e_command_parse_pr_file(char* line, int* num, char* cmd_opt[]);

#endif // __M46ECTL_COMMAND_H__
