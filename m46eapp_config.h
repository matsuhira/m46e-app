/******************************************************************************/
/* ファイル名 : m46eapp_config.h                                              */
/* 機能概要   : 設定情報管理 ヘッダファイル                                   */
/* 修正履歴   : 2011.12.20 T.Maeda 新規作成                                   */
/*              2012.07.11 T.Maeda Phase4向けに全面改版                       */
/*              2013.07.10 K.Nakamura 強制フラグメント機能 追加               */
/*              2013.08.01 Y.Shibata  M46E-PR拡張機能                         */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#ifndef __M46EAPP_CONFIG_H__
#define __M46EAPP_CONFIG_H__

#include <stdbool.h>
#include "m46eapp_list.h"

struct in_addr;
struct in6_addr;

///////////////////////////////////////////////////////////////////////////////
//! 動作モード種別
///////////////////////////////////////////////////////////////////////////////
enum m46e_tunnel_mode
{
    M46E_TUNNEL_MODE_NORMAL = 0,   ///< 通常モード
    M46E_TUNNEL_MODE_AS     = 1,   ///< M46E-ASモード
    M46E_TUNNEL_MODE_PR     = 2,   ///< M46E-PRモード
    M46E_TUNNEL_MODE_NONE   = -1,  ///< モードなし
};
typedef enum m46e_tunnel_mode m46e_tunnel_mode;

///////////////////////////////////////////////////////////////////////////////
//! 共通設定
///////////////////////////////////////////////////////////////////////////////
struct m46e_config_general_t
{
    char*                plane_name;          ///< plane識別名
    char*                plane_id;            ///< IPv4 network plane ID
    struct in6_addr*     unicast_prefix;      ///< M46E unicast prefix 
    int                  unicast_prefixlen;   ///< M46E unicast prefix長
    struct in6_addr*     src_addr_unicast_prefix;      ///< M46E-PR 送信元アドレスのunicast prefix 
    int                  src_addr_unicast_prefixlen;   ///< M46E-PR 送信元アドレスのunicast prefix長
    struct in6_addr*     multicast_prefix;    ///< M46E multicast prefix 
    int                  multicast_prefixlen; ///< M46E multicast prefix長
    m46e_tunnel_mode    tunnel_mode;         ///< 仮想デバイスの動作モード
    bool                 debug_log;           ///< デバッグログを出力するかどうか
    bool                 daemon;              ///< デーモン化するかどうか
    char*                startup_script;      ///< スタートアップスクリプト
    bool                 force_fragment;      ///< 強制フラグメント機能を有効にするかどうか
    bool                 route_sync;          ///< 経路同期をおこなうかどうか
    int                  route_entry_max;     ///< 経路表に登録できるエントリの最大数
};
typedef struct m46e_config_general_t m46e_config_general_t;

///////////////////////////////////////////////////////////////////////////////
//! M46E-ASモード専用の設定
///////////////////////////////////////////////////////////////////////////////
struct m46e_config_m46e_as_t
{
    struct in_addr* shared_address;   ///< 共有するIPアドレス
    int             start_port;       ///< 管理するポートの先頭番号
    int             port_num;         ///< 管理するポートの数
};
typedef struct m46e_config_m46e_as_t m46e_config_m46e_as_t;

///////////////////////////////////////////////////////////////////////////////
//! Path MTU Discovery情報保持タイプ
///////////////////////////////////////////////////////////////////////////////
enum m46e_pmtud_type
{
    M46E_PMTUD_TYPE_NONE,    ///< PMTU Discovery処理なし
    M46E_PMTUD_TYPE_TUNNEL,  ///< トンネル毎
    M46E_PMTUD_TYPE_HOST     ///< ホスト毎
};
typedef enum m46e_pmtud_type m46e_pmtud_type;

///////////////////////////////////////////////////////////////////////////////
//! IPv6網側のPath MTU Discovery関連設定
///////////////////////////////////////////////////////////////////////////////
struct m46e_config_pmtud_t
{
    m46e_pmtud_type  type;            ///< PMTU情報保持タイプ
    int               expire_time;     ///< PMTU長保持期限(秒)
};
typedef struct m46e_config_pmtud_t m46e_config_pmtud_t;


///////////////////////////////////////////////////////////////////////////////
//! デバイス種別
///////////////////////////////////////////////////////////////////////////////
enum m46e_device_type
{
    M46E_DEVICE_TYPE_TUNNEL_IPV4,      ///< トンネルデバイス(IPv4側)
    M46E_DEVICE_TYPE_TUNNEL_IPV6,      ///< トンネルデバイス(IPv6側)
    M46E_DEVICE_TYPE_VETH,             ///< veth方式
    M46E_DEVICE_TYPE_MACVLAN,          ///< macvlan方式
    M46E_DEVICE_TYPE_PHYSICAL,         ///< 物理デバイス方式
    M46E_DEVICE_TYPE_NONE       = -1,  ///< 種別なし
};
typedef enum m46e_device_type m46e_device_type;

///////////////////////////////////////////////////////////////////////////////
//! デバイス情報
///////////////////////////////////////////////////////////////////////////////
struct m46e_device_t
{
    m46e_device_type   type;            ///< デバイスのネットワーク空間への移動方式
    char*               name;            ///< デバイス名
    char*               physical_name;   ///< 対応する物理デバイスのデバイス名
    struct ether_addr*  physical_hwaddr; ///< 対応する物理デバイスのMACアドレス（自動取得）
    struct in_addr*     ipv4_address;    ///< デバイスに設定するIPv4アドレス
    int                 ipv4_netmask;    ///< デバイスに設定するIPv4サブネットマスク
    struct in_addr*     ipv4_gateway;    ///< デフォルトゲートウェイアドレス
    struct in6_addr*    ipv6_address;    ///< デバイスに設定するIPv6アドレス
    int                 ipv6_prefixlen;  ///< デバイスに設定するIPv6プレフィックス長
    int                 mtu;             ///< デバイスに設定するMTU長
    struct ether_addr*  hwaddr;          ///< デバイスに設定するMACアドレス
    int                 ifindex;         ///< デバイスのインデックス番号
    union {
        struct {
            char* bridge;                ///< 接続するブリッジデバイスのデバイス名
            char* pair_name;             ///< vethの対向デバイス(Backboneに残すデバイス)のデバイス名
        } veth;                          ///< veth固有の設定
        struct {
            int mode;                    ///< MACVLANのモード
        } macvlan;                       ///< MACVLAN固有の設定
        struct {
            int mode;                    ///< トンネル方式(TUN/TAP)
            int fd;                      ///< トンネルデバイスファイルディスクリプタ
        } tunnel;                        ///< トンネルデバイス固有の設定
    } option;                            ///< オプション設定
};
typedef struct m46e_device_t m46e_device_t;

///////////////////////////////////////////////////////////////////////////////
//! トンネルデバイス情報
///////////////////////////////////////////////////////////////////////////////
struct m46e_config_tunnel_t
{
    m46e_device_t ipv4;  ///< IPv4側トンネルデバイス設定
    m46e_device_t ipv6;  ///< IPv6側トンネルデバイス設定
};
typedef struct m46e_config_tunnel_t m46e_config_tunnel_t;

///////////////////////////////////////////////////////////////////////////////
//! M46E-PR Config 情報
///////////////////////////////////////////////////////////////////////////////
typedef struct _m46e_config_pr_entry_t
{
    bool                    enable;             ///< エントリーか有効(true)/無効(false)かを表すフラグ
    struct in_addr*         v4addr;             ///< 送信先のIPv4ネットワークアドレス
    int                     v4cidr;             ///< CIDR形式でのIPv4サブネットマスク
    struct in6_addr*        pr_prefix;          ///< M46E-PR address prefixのIPv6アドレス
    int                     v6cidr;             ///< M46E-PR address prefix長（CIDR形式）
} m46e_pr_config_entry_t;

///////////////////////////////////////////////////////////////////////////////
//! M46E-PR Config Table
///////////////////////////////////////////////////////////////////////////////
typedef struct _m46e_config_pr_table_t
{
    int                     num;            ///< M46E-PR Config Entry 数
    m46e_list              entry_list;     ///< M46E-PR Config Entry list
} m46e_pr_config_table_t;


///////////////////////////////////////////////////////////////////////////////
//! M46Eアプリケーション 設定
///////////////////////////////////////////////////////////////////////////////
struct m46e_config_t
{
    char*                      filename;     ///< 設定ファイルのフルパス
    m46e_config_general_t*    general;      ///< 共通設定
    m46e_config_m46e_as_t*   m46e_as;     ///< M46E-ASモード 専用の設定
    m46e_config_pmtud_t*      pmtud;        ///< IPv6網側のPMTUD関連設定
    m46e_config_tunnel_t*     tunnel;       ///< トンネルデバイス設定
    m46e_list                 device_list;  ///< デバイス設定リスト
    m46e_pr_config_table_t*   pr_conf_table;///< M46E-PR Config Table
};
typedef struct m46e_config_t m46e_config_t;


///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
m46e_config_t* m46e_config_load(const char* filename);
void m46e_config_destruct(m46e_config_t* config);
void m46e_config_dump(const m46e_config_t* config, int fd);


#endif // __M46EAPP_CONFIG_H__

