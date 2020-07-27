/******************************************************************************/
/* ファイル名 : m46eapp_command_data.h                                        */
/* 機能概要   : コマンドデータ共通定義 ヘッダファイル                         */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2013.07.08 Y.Shibata 動的定義変更機能追加                     */
/*              2013.08.01 Y.Shibata  M46E-PR拡張機能                         */
/*              2013.08.21 H.Koganemaru M46E-PR機能拡張                       */
/*              2013.08.30 H.Koganemaru 動的定義変更機能追加                  */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2013.10.03 Y.Shibata  M46E-PR拡張機能                         */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2014.01.21 M.Iwatsubo M46E-PR外部連携機能追加                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_COMMAND_DATA_H__
#define __M46EAPP_COMMAND_DATA_H__

#include <stdbool.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <stdbool.h>

#include "m46eapp_config.h"
#include "m46eapp_mng_v4_route_data.h"
#include "m46eapp_mng_v6_route_data.h"

//! UNIXドメインソケット名
#define M46E_COMMAND_SOCK_NAME  "/m46e/%s/command"

//! コマンド変数
#define CMDOPT_NUM_MAX 1
#define CMDOPT_LEN_MAX 256

//! コマンドコード
enum m46e_command_code
{
    M46E_COMMAND_NONE,
    // 運用前の同期専用
    M46E_SETUP_FAILURE,        ///< 設定失敗
    M46E_CHILD_INIT_END,       ///< 子プロセス初期化完了
    M46E_NETDEV_MOVED,         ///< ネットワークデバイスの移動完了
    M46E_NETWORK_CONFIGURE,    ///< Stubネットワーク設定完了
    M46E_START_OPERATION,      ///< 運用開始指示
    // ここから運用中のコマンド
    M46E_PACKET_TOO_BIG,       ///< ICMPv6 too big 受信
    M46E_SHOW_CONF,            ///< config表示
    M46E_SHOW_STATISTIC,       ///< 統計情報表示
    M46E_SHOW_PMTU,            ///< Path MTU Discoveryテーブル表示
    M46E_EXEC_SHELL,           ///< シェル起動
    M46E_SHUTDOWN,             ///< シャットダウン指示
    M46E_RESTART,             ///< リスタート指示
    M46E_DEVICE_ADD,          ///< デバイス増設
    M46E_DEVICE_DEL,          ///< デバイス減設
    M46E_DEVICE_OPE_END,      ///< デバイス増減設完了
    M46E_PR_TABLE_GENERATE_FAILURE, ///< M46E-PRテーブル生成失敗
    M46E_ADD_PR_ENTRY,         ///< PR ENTRY 追加
    M46E_DEL_PR_ENTRY,         ///< PR ENTRY 削除
    M46E_DELALL_PR_ENTRY,      ///< PR ENTRY 全削除
    M46E_ENABLE_PR_ENTRY,      ///< PR ENTRY 活性化
    M46E_DISABLE_PR_ENTRY,     ///< PR ENTRY 非活性化
    M46E_SHOW_PR_ENTRY,        ///< PR ENTRY 表示
    M46E_LOAD_PR_COMMAND,      ///< PR-Commandファイル読み込み
    M46E_SET_DEBUG_LOG,        ///< 動的定義変更 デバッグログ出力設定
    M46E_SET_DEBUG_LOG_END,    ///< 動的定義変更 デバッグログ出力設定完了
    M46E_SET_PMTUD_EXPTIME,    ///< 動的定義変更 PMTU保持時間設定
    M46E_SET_PMTUD_MODE,       ///< 動的定義変更 PMTU動作モード設定
    M46E_SET_FORCE_FRAG,       ///< 動的定義変更 強制フラグメント設定
    M46E_SET_DEFAULT_GW,       ///< 動的定義変更 デフォルトGW設定
    M46E_SET_DEFAULT_GW_END,   ///< 動的定義変更 デフォルトGW設定完了
    M46E_SET_TUNNEL_MTU,       ///< 動的定義変更 トンネルデバイスMTU設定
    M46E_SET_TUNNEL_MTU_END,   ///< 動的定義変更 トンネルデバイスMTU設定完了
    M46E_SET_DEVICE_MTU,       ///< 動的定義変更 収容デバイスMTU設定
    M46E_SET_DEVICE_MTU_END,   ///< 動的定義変更 収容デバイスMTU設定完了
    M46E_EXEC_INET_CMD,        ///< 動的定義変更 Stub側コマンド実行要求
    M46E_EXEC_INET_CMD_END,    ///< 動的定義変更 Stub側コマンド実行要求完了
    M46E_THREAD_INIT_END,      ///< v4経路同期スレッド初期化完了
    M46E_SYNC_ROUTE,           ///< 経路同期要求
    M46E_SHOW_ROUTE,           ///< 経路情報表示
    M46E_COMMAND_MAX
};

//! 経路同期テーブル表示要求データ
struct m46e_show_route_data
{
    int fd; ///< 表示データ書き込み先のファイルディスクリプタ
};

//! 経路同期要求
struct m46e_route_sync_request_t
{
    int     type;                                       ///< コマンドタイプ
    int     family;                                     ///< プロトコルファミリー
    union {
        struct m46e_v4_route_info_t v4_route_info;     ///< IPv4 経路情報
        struct m46e_v6_route_info_t v6_route_info;     ///< IPv6 経路情報
    };
};

//! ICMPv6 too big 受信データ
struct m46e_packet_too_big_data
{
    struct in6_addr dst_addr; ///< 送信先IPv6アドレス
    int             mtu;      ///< too bigパケット内のMTU
};

//! Path MTU Discoveryテーブル表示要求データ
struct m46e_show_pmtu_data
{
    int fd; ///< 表示データ書き込み先のファイルディスクリプタ
};

//! 対応する物理デバイス名
typedef struct _physical_name_t {
    bool                is_set;                     ///< 値が設定されているかどうかのフラグ
    char                name[IFNAMSIZ];             ///< 対応する物理デバイス名
} physical_name_t;

//! 仮想デバイス名
typedef struct _virtual_name_t {
    bool                is_set;                     ///< 値が設定されているかどうかのフラグ
    char                name[IFNAMSIZ];             ///< 仮想デバイス名
} virtual_name_t;

//! デバイスに設定するIPv4アドレス
typedef struct _ipv4_address_t {
    bool                is_set;                     ///< 値が設定されているかどうかのフラグ
    struct in_addr      address;                    ///< デバイスに設定するIPv4アドレス
} ipv4_address_t;

//! デバイスに設定するIPv4サブネットマスク
typedef struct _ipv4_netmask_t {
    bool                is_set;                     ///< 値が設定されているかどうかのフラグ
    int                 netmask;                    ///< デバイスに設定するIPv4サブネットマスク
} ipv4_netmask_t;

//! デフォルトゲートウェイアドレス
typedef struct _ipv4_gateway_t {
    bool                is_set;                     ///< 値が設定されているかどうかのフラグ
    struct in_addr      gateway;                    ///< デフォルトゲートウェイアドレス
} ipv4_gateway_t;

//! デバイスに設定するハードウェアアドレス
typedef struct _hwaddr_t {
    bool                is_set;                     ///< 値が設定されているかどうかのフラグ
    struct ether_addr   hwaddr;                     ///< デバイスに設定するMACアドレス
} hwaddr_t;

//! デバイスに設定するMTU長
typedef struct _mtu_t {
    bool                is_set;                     ///< 値が設定されているかどうかのフラグ
    int                 mtu;                        ///< デバイスに設定するMTU長
} mtu_t;

//! デバイス増減設要求データ
struct m46e_device_data
{
    physical_name_t     s_physical;                 ///< 対応する物理デバイス名
    virtual_name_t      s_virtual;                  ///< 仮想デバイス名
    ipv4_address_t      s_v4address;                ///< デバイスに設定するIPv4アドレス
    ipv4_netmask_t      s_v4netmask;                ///< デバイスに設定するIPv4サブネットマスク
    ipv4_gateway_t      s_v4gateway;                ///< デフォルトゲートウェイアドレス
    hwaddr_t            s_hwaddr;                   ///< デバイスに設定するMACアドレス
    mtu_t               s_mtu;                      ///< デバイスに設定するMTU長
    int                 ifindex;                    ///< デバイスのインデックス番号
    int                 fd;                         ///< 表示データ書き込み先のファイルディスクリプタ
};

//! M46E-PR Table 受信データ
struct m46e_pr_entry_command_data
{
    bool                    enable;                 ///< エントリーか有効/無効かを表すフラグ
    struct in_addr          v4addr;                 ///< IPv4アドレス
    int                     v4cidr;                 ///< IPv4のCIDR
    struct in6_addr         pr_prefix;              ///< M46E-PR address prefix用のIPv6アドレス(表示用)
    int                     v6cidr;                 ///< M46E-PR address prefixのサブネットマスク長(表示用)
    int                     fd;                     ///< 書き込み先のファイルディスクリプタ
};

//! M46E-PR Table 表示要求データ
struct m46e_show_pr_table
{
    int fd;                                         ///< 表示データ書き込み先のファイルディスクリプタ
};

//! デバッグログ出力設定 受信データ
struct m46e_set_debuglog_data
{
    bool mode;                                      ///< デバッグログ出力設定
    int  fd;                                        ///< 表示データ書き込み先のファイルディスクリプタ
};

//! 強制フラグメント設定 受信データ
struct m46e_set_force_fragment_data
{
    bool mode;                                      ///< 強制フラグメント設定
    int  fd;                                        ///< 表示データ書き込み先のファイルディスクリプタ
};

//! Path MTU Discovery情報保持タイプ設定 受信データ
struct m46e_set_pmtud_type_data
{
    m46e_pmtud_type type;                          ///< PathMTU情報保持タイプ
    int fd;                                         ///< 表示データ書き込み先のファイルディスクリプタ
};

//! Path MTU Discovery情報保持時間設定 受信データ

struct m46e_set_pmtud_exptime_data
{
    int exptime;                                    ///< PathMTU情報保持時間
    int fd;                                         ///< 表示データ書き込み先のファイルディスクリプタ
};

//! デフォルトゲートウェイ設定 受信データ
struct m46e_set_default_gw_data
{
    bool mode;                                      ///< デフォルトゲートウェイ設定
    int  fd;                                        ///< 表示データ書き込み先のファイルディスクリプタ
};

//! トンネルMTU設定 受信データ
struct m46e_set_tunnel_mtu_data
{
    int mtu;                                        ///< トンネルデバイスMTU
    int fd;                                         ///< 表示データ書き込み先のファイルディスクリプタ
};

//! デバイスMTU設定 受信データ
struct m46e_set_device_mtu_data
{
    char name[IFNAMSIZ];                            ///< 仮想デバイス名
    int  mtu;                                       ///< トンネルデバイスMTU
    int  fd;                                        ///< 表示データ書き込み先のファイルディスクリプタ
};

//! StubNetwork側実行コマンド 受信データ
struct m46e_exec_cmd_inet_data
{
    char opt[CMDOPT_LEN_MAX];                       ///< コマンドオプション最大長
    int  num;                                       ///< コマンドオプション数
    int  fd;                                        ///< 表示データ書き込み先のファイルディスクリプタ
};


//! 要求データ構造体
struct m46e_command_request_data
{
    union {
        struct m46e_packet_too_big_data    too_big;       ///< too big 受信データ
        struct m46e_show_pmtu_data         pmtu;          ///< Path MTU 表示データ
        struct m46e_device_data            dev_data;      ///< デバイス増減説データ
        struct m46e_pr_entry_command_data  pr_data;       ///< M46E-PR コマンドデータ
        struct m46e_show_pr_table          pr_show;       ///< M46E-PR 表示データ
        struct m46e_set_debuglog_data       dlog;         ///< デバッグログ設定コマンドデータ
        struct m46e_set_force_fragment_data ffrag;        ///< 強制フラグメント設定コマンドデータ
        struct m46e_set_pmtud_type_data     pmtu_mode;    ///< PTMU情報保持タイプ設定データ
        struct m46e_set_pmtud_exptime_data  pmtu_exptime; ///< PMTU情報保持時間設定データ
        struct m46e_set_default_gw_data     defgw;        ///< デフォルトゲートウェイ設定データ
        struct m46e_set_tunnel_mtu_data     tunmtu;       ///< トンネルMTU設定データ
        struct m46e_set_device_mtu_data     devmtu;       ///< デバイスMTU設定データ
        struct m46e_exec_cmd_inet_data      inetcmd;      ///< StubNetwork実行コマンドデータ
        struct m46e_route_sync_request_t   info_route;    ///< 経路同期要求データ
        struct m46e_show_route_data        show_route;    ///< 経路同期テーブル 表示データ
    };
};

//! シェル起動応答データ
struct m46e_exec_shell_data
{
    int fd; ///< ptyデバイスのファイルディスクリプタ
};

//! Backbone側コマンド実行要応答データ
struct m46e_exec_cmd_data
{
    int fd; ///< ptyデバイスのファイルディスクリプタ
};


//! 応答データ構造体
struct m46e_command_response_data
{
    int result;                                   ///< 処理結果
    union {
        struct m46e_exec_shell_data exec_shell;  ///< シェル起動応答データ
        struct m46e_exec_cmd_data exec_cmd;      ///< コマンド実行応答データ
    };
};

//! コマンドデータ構造体
struct m46e_command_t
{
    enum   m46e_command_code          code;  ///< コマンドコード
    struct m46e_command_request_data  req;   ///< 要求データ
    struct m46e_command_response_data res;   ///< 応答データ
};
typedef struct m46e_command_t m46e_command_t;

#endif // __M46EAPP_COMMAND_DATA_H__

