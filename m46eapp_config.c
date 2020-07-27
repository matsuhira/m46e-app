/******************************************************************************/
/* ファイル名 : m46eapp_config.c                                              */
/* 機能概要   : 設定情報管理 ソースファイル                                   */
/* 修正履歴   : 2011.12.20  T.Maeda 新規作成                                  */
/*              2012.07.12  T.Maeda Phase4向けに全面改版                      */
/*              2013.04.09  H.Koganemaru バグ修正                             */
/*              2013.07.10  K.Nakamura 強制フラグメント機能 追加              */
/*              2013.09.13  K.Nakamura M46E-PR拡張機能 追加                   */
/*              2013.12.02  Y.Shibata 経路同期機能追加                        */
/*              2016.04.15  H.Koganemaru 名称変更に伴う修正                   */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <regex.h>
#include <netinet/ether.h>
#include <netinet/ip6.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_tun.h>
#include <linux/if_link.h>

#include "m46eapp_config.h"
#include "m46eapp_log.h"
#include "m46eapp_pr.h"

//! 設定ファイルを読込む場合の１行あたりの最大文字数
#define CONFIG_LINE_MAX 256

// 設定ファイルのbool値
#define CONFIG_BOOL_TRUE  "yes"
#define CONFIG_BOOL_FALSE "no"

// 設定ファイルの動作モード種別
#define CONFIG_TUNNEL_MODE_NORMAL 0
#define CONFIG_TUNNEL_MODE_AS     1
#define CONFIG_TUNNEL_MODE_PR     2

#define CONFIG_PMTUD_TYPE_NONE   0
#define CONFIG_PMTUD_TYPE_TUNNEL 1
#define CONFIG_PMTUD_TYPE_HOST   2

// 設定ファイルの数値関連の最小値と最大値
#define CONFIG_TUNNEL_MODE_MIN CONFIG_TUNNEL_MODE_NORMAL
#define CONFIG_TUNNEL_MODE_MAX CONFIG_TUNNEL_MODE_PR

#define CONFIG_IPV4_NETMASK_MIN 1
#define CONFIG_IPV4_NETMASK_MAX 32
#define CONFIG_IPV4_NETMASK_PR_MIN 0

#define CONFIG_IPV6_PREFIX_MIN 1
#define CONFIG_IPV6_PREFIX_MAX 128

#define CONFIG_AS_PORT_MIN 0
#define CONFIG_AS_PORT_MAX 65535

#define CONFIG_AS_PORT_NUM_MIN 1
#define CONFIG_AS_PORT_NUM_MAX 65536

#define CONFIG_PMTUD_EXPIRE_TIME_MIN 301
#define CONFIG_PMTUD_EXPIRE_TIME_MAX 65535
#define CONFIG_PMTUD_EXPIRE_TIME_DEFAULT 600

#define CONFIG_PMTUD_TYPE_MIN CONFIG_PMTUD_TYPE_NONE
#define CONFIG_PMTUD_TYPE_MAX CONFIG_PMTUD_TYPE_HOST

#define CONFIG_TUNNEL_MTU_MIN 1280
#define CONFIG_TUNNEL_MTU_MAX 65521
#define CONFIG_TUNNEL_MTU_DEFAULT 1500

#define CONFIG_DEVICE_MTU_MIN 548
#define CONFIG_DEVICE_MTU_MAX 65521

#define CONFIG_PLANE_PREFIX_MAX    96
#define CONFIG_PLANE_AS_PREFIX_MAX 80

#define CONFIG_ROUTE_ENTRY_MIN   1
#define CONFIG_ROUTE_ENTRY_MAX   65535

// 設定ファイルのセクション名とキー名
#define SECTION_GENERAL                   "general"
#define SECTION_GENERAL_PLANE_NAME        "plane_name"
#define SECTION_GENERAL_PLANE_ID          "plane_id"
#define SECTION_GENERAL_TUNNEL_MODE       "tunnel_mode"
#define SECTION_GENERAL_UNICAST_PREFIX    "m46e_unicast_prefix"
#define SECTION_GENERAL_SRC_ADDR_UNICAST_PREFIX    "m46e_pr_src_addr_unicast_prefix"
#define SECTION_GENERAL_MULTICAST_PREFIX  "m46e_multicast_prefix"
#define SECTION_GENERAL_DEBUG_LOG         "debug_log"
#define SECTION_GENERAL_DAEMON            "daemon"
#define SECTION_GENERAL_STARTUP_SCRIPT    "startup_script"
#define SECTION_GENERAL_FORCE_FRAGMENT    "force_fragment"
#define SECTION_ROUTING_SYNC              "route_sync"
#define SECTION_GENERAL_ROUTE_ENTRY_MAX   "route_entry_max"


#define SECTION_M46E_AS                 "m46e-as"
#define SECTION_M46E_AS_SHARED_ADDRESS  "shared_ipv4_address"
#define SECTION_M46E_AS_START_PORT      "start_port"
#define SECTION_M46E_AS_PORT_NUM        "port_num"

#define SECTION_PMTUD                    "pmtud"
#define SECTION_PMTUD_TYPE               "type"
#define SECTION_PMTUD_EXPIRE_TIME        "expire_time"

#define SECTION_TUNNEL                   "tunnel"
#define SECTION_TUNNEL_NAME              "tunnel_name"
#define SECTION_TUNNEL_IPV4_ADDRESS      "ipv4_address"
#define SECTION_TUNNEL_IPV4_HWADDR       "ipv4_hwaddr"
#define SECTION_TUNNEL_IPV6_ADDRESS      "ipv6_address"
#define SECTION_TUNNEL_IPV6_HWADDR       "ipv6_hwaddr"
#define SECTION_TUNNEL_MTU               "mtu"
#define SECTION_TUNNEL_IPV4_DEFAULT_GW   "ipv4_default_gw"

#define SECTION_DEVICE                        "device"
#define SECTION_DEVICE_TYPE                   "type"
#define SECTION_DEVICE_TYPE_VETH              "veth"
#define SECTION_DEVICE_TYPE_MACVLAN           "macvlan"
#define SECTION_DEVICE_TYPE_PHYS              "physical"
#define SECTION_DEVICE_NAME                   "name"
#define SECTION_DEVICE_PHYS_DEV               "physical_dev"
#define SECTION_DEVICE_IPV4_ADDRESS           "ipv4_address"
#define SECTION_DEVICE_IPV4_GATEWAY           "ipv4_gateway"
#define SECTION_DEVICE_MTU                    "mtu"
#define SECTION_DEVICE_HWADDR                 "hwaddr"
#define SECTION_DEVICE_VETH_BRIDGE            "veth_bridge"
#define SECTION_DEVICE_VETH_PAIR_NAME         "veth_pair_name"
#define SECTION_DEVICE_MACVLAN_MODE           "macvlan_mode"
#define SECTION_DEVICE_MACVLAN_MODE_PRIVATE   "private"
#define SECTION_DEVICE_MACVLAN_MODE_VEPA      "vepa"
#define SECTION_DEVICE_MACVLAN_MODE_BRIDGE    "bridge"
#define SECTION_DEVICE_MACVLAN_MODE_PASSTHRU  "passthru"

#define SECTION_M46E_PR                      "m46e-pr"
#define SECTION_M46E_PR_IPV4_ADDRESS         "ipv4_address"
#define SECTION_M46E_PR_PREFIX               "m46e_pr_prefix"

///////////////////////////////////////////////////////////////////////////////
//! セクション種別
///////////////////////////////////////////////////////////////////////////////
typedef enum _config_section {
    CONFIG_SECTION_NONE      =  0, ///< セクション以外の行
    CONFIG_SECTION_GENERAL,        ///< [general]セクション
    CONFIG_SECTION_M46E_AS,       ///< [m46e-as]セクション
    CONFIG_SECTION_PMTUD,          ///< [pmtud]セクション
    CONFIG_SECTION_TUNNEL,         ///< [tunnel]セクション
    CONFIG_SECTION_DEVICE,         ///< [device]セクション
    CONFIG_SECTION_M46E_PR,       ///< [m46e-pr]セクション
    CONFIG_SECTION_UNKNOWN   = -1  ///< 不明なセクション
} config_section;

///////////////////////////////////////////////////////////////////////////////
//! KEY/VALUE格納用構造体
///////////////////////////////////////////////////////////////////////////////
typedef struct _config_keyvalue_t {
    char key[CONFIG_LINE_MAX];   ///< KEYの値
    char value[CONFIG_LINE_MAX]; ///< VALUEの値
} config_keyvalue_t;

//! セクション行判定用正規表現 ([で始まって、]で終わる行)
#define SECTION_LINE_REGEX "^\\[(.*)\\]$"
//! KEY VALUE行判定用正規表現 (# 以外で始まって、KEY = VALUE 形式になっている行)
#define KV_REGEX "[ \t]*([^#][^ \t]*)[ \t]*=[ \t]*([^ \t].*)"


///////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
///////////////////////////////////////////////////////////////////////////////
static void config_init(m46e_config_t* config);
static bool config_validate_all(m46e_config_t* config);

bool config_init_general(m46e_config_t* config);
static void config_destruct_general(m46e_config_general_t* general);
static bool config_parse_general(const config_keyvalue_t* kv, m46e_config_t* config);
static bool config_validate_general(m46e_config_t* config);

static bool config_init_m46e_as(m46e_config_t* config);
static void config_destruct_m46e_as(m46e_config_m46e_as_t* m46e_as);
static bool config_parse_m46e_as(const config_keyvalue_t* kv, m46e_config_t* config);
static bool config_validate_m46e_as(m46e_config_t* config);
static bool config_check_m46e_as_port_setting(const int port, const int port_num);

static bool config_init_pmtud(m46e_config_t* config);
static bool config_parse_pmtud(const config_keyvalue_t* kv, m46e_config_t* config);

static bool config_init_tunnel(m46e_config_t* config);
static void config_destruct_tunnel(m46e_config_tunnel_t* tunnel);
static bool config_parse_tunnel(const config_keyvalue_t* kv, m46e_config_t* config);
static bool config_validate_tunnel(m46e_config_t* config);

static bool config_init_device(m46e_config_t* config);
static void config_destruct_device(m46e_device_t* device);
static bool config_parse_device(const config_keyvalue_t* kv, m46e_config_t* config);
static bool config_validate_device(m46e_config_t* config);

static bool config_init_m46e_pr(m46e_config_t* config);
static void config_destruct_m46e_pr(m46e_pr_config_entry_t* pr_config_entry);
static bool config_parse_m46e_pr(const config_keyvalue_t* kv, m46e_config_t* config);
static bool config_validate_m46e_pr(m46e_config_t* config);

static bool config_is_section(const char* str, config_section* section);
static bool config_is_keyvalue(const char* line_str, config_keyvalue_t* kv);

static int  config_ipv4_prefix(const struct in_addr* addr);

static bool parse_bool(const char* str, bool* output);
static bool parse_int(const char* str, int* output, const int min, const int max);
static bool parse_ipv4address(const char* str, struct in_addr* output, int* prefixlen);
static bool parse_ipv4address_pr(const char* str, struct in_addr* output, int* prefixlen);
static bool parse_ipv6address(const char* str, struct in6_addr* output, int* prefixlen);
static bool parse_macaddress(const char* str, struct ether_addr* output);


///////////////////////////////////////////////////////////////////////////////
//! 設定ファイル解析用テーブル
///////////////////////////////////////////////////////////////////////////////
static struct {
    char* name;
    bool  (*init_func)(m46e_config_t* config);
    bool  (*parse_func)(const config_keyvalue_t* kv, m46e_config_t* config);
    bool  (*validate_func)(m46e_config_t* config);
} config_table[] = {
   { "none",           NULL,                 NULL,                  NULL                     },
   { SECTION_GENERAL,  config_init_general,  config_parse_general,  config_validate_general  },
   { SECTION_M46E_AS, config_init_m46e_as, config_parse_m46e_as, config_validate_m46e_as },
   { SECTION_PMTUD,    config_init_pmtud,    config_parse_pmtud,    NULL                     },
   { SECTION_TUNNEL,   config_init_tunnel,   config_parse_tunnel,   config_validate_tunnel   },
   { SECTION_DEVICE,   config_init_device,   config_parse_device,   config_validate_device   },
   { SECTION_M46E_PR, config_init_m46e_pr, config_parse_m46e_pr, config_validate_m46e_pr },
};

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定ファイル読込み関数
//!
//! 引数で指定されたファイルの内容を読み込み、構造体に格納する。
//! <b>戻り値の構造体は本関数内でallocするので、呼出元で解放すること。</b>
//! <b>解放にはfreeではなく、必ずm46e_config_destruct()関数を使用すること。</b>
//!
//! @param [in] filename 設定ファイル名
//!
//! @return 設定を格納した構造体のポインタ
///////////////////////////////////////////////////////////////////////////////
m46e_config_t* m46e_config_load(const char* filename)
{
    // ローカル変数宣言
    FILE*                     fp;
    char                      line[CONFIG_LINE_MAX];
    config_section            current_section;
    config_section            next_section;
    m46e_config_t*           config;
    uint32_t                  line_cnt;
    bool                      result;
    config_keyvalue_t         kv;

    // 引数チェック
    if(filename == NULL || strlen(filename) == 0){
        return NULL;
    }

    // ローカル変数初期化
    fp              = NULL;
    current_section = CONFIG_SECTION_NONE;
    next_section    = CONFIG_SECTION_NONE;
    config          = NULL;
    line_cnt        = 0;
    result          = true;

    // 設定ファイルオープン
    fp = fopen(filename, "r");
    if(fp == NULL){
        return NULL;
    }

    // 設定保持用構造体alloc
    config = (m46e_config_t*)malloc(sizeof(m46e_config_t));
    if(config == NULL){
        fclose(fp);
        return NULL;
    }

    // 設定保持用構造体初期化
    config_init(config);

    while(fgets(line, sizeof(line), fp) != NULL){
        // ラインカウンタをインクリメント
        line_cnt++;

        // 改行文字を終端文字に置き換える
        line[strlen(line)-1] = '\0';

        // コメント行と空行はスキップ
        if((line[0] == '#') || (strlen(line) == 0)){
            continue;
        }

        if(config_is_section(line, &next_section)){
            // セクション行の場合
            // 現在のセクションの整合性チェック
            if(config_table[current_section].validate_func != NULL){
                result = config_table[current_section].validate_func(config);
                if(!result){
                    m46e_logging(LOG_ERR, "Section Validation Error [%s]\n", config_table[current_section].name);
                    break;
                }
            }
            // 次セクションが不明の場合は処理中断
            if(next_section == CONFIG_SECTION_UNKNOWN){
                // 処理結果を異常に設定
                result = false;
                break;
            }
            // 次セクションの初期化関数をコール(セクション重複チェックなどはここでおこなう)
            if(config_table[next_section].init_func != NULL){
                result = config_table[next_section].init_func(config);
                if(!result){
                    m46e_logging(LOG_ERR, "Section Initialize Error [%s]\n", config_table[next_section].name);
                    break;
                }
            }

            current_section = next_section;
        }
        else if(config_is_keyvalue(line, &kv)){
             // キーバリュー行の場合、セクションごとの解析関数をコール
             if(config_table[current_section].parse_func != NULL){
                 result = config_table[current_section].parse_func(&kv, config);
                 if(!result){
                     m46e_logging(LOG_ERR, "Parse Error [%s] line %d : %s\n", config_table[current_section].name, line_cnt, line);
                     break;
                 }
             }
        }
        else{
            // なにもしない
        }
    }
    fclose(fp);

    // 最後のセクションの整合性チェック
    if(config_table[current_section].validate_func != NULL){
        result = config_table[current_section].validate_func(config);
        if(!result){
            m46e_logging(LOG_ERR, "Section Validation Error [%s]\n", config_table[current_section].name);
        }
    }

    if(result){
        // 全体の整合性チェック
        result = config_validate_all(config);
        if(!result){
            m46e_logging(LOG_ERR, "Config Validation Error\n");
        }
    }

    if(!result){
        // 処理結果が異常の場合、後始末をしてNULLを返す
        m46e_config_destruct(config);
        config = NULL;
    }
    else{
        // 読み込んだ設定ファイルのフルパスを格納
        config->filename = realpath(filename, NULL);
    }

    return config;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定ファイル構造体解放関数
//!
//! 引数で指定された設定ファイル構造体を解放する。
//!
//! @param [in,out] config 解放する設定ファイル構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_config_destruct(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return;
    }

    free(config->filename);

    if(config->general != NULL){
        config_destruct_general(config->general);
        free(config->general);
    }

    if(config->m46e_as != NULL){
        config_destruct_m46e_as(config->m46e_as);
        free(config->m46e_as);
    }

    if(config->pmtud != NULL){
        free(config->pmtud);
    }

    if(config->tunnel != NULL){
        config_destruct_tunnel(config->tunnel);
        free(config->tunnel);
    }

    while(!m46e_list_empty(&config->device_list)){
        m46e_list* node = config->device_list.next;
        m46e_device_t* device = node->data;
        config_destruct_device(device);
        free(device);
        m46e_list_del(node);
        free(node);
    }

    if(config->pr_conf_table != NULL){
        while(!m46e_list_empty(&config->pr_conf_table->entry_list)){
            m46e_list* node = config->pr_conf_table->entry_list.next;
            m46e_pr_config_entry_t* pr_config_entry = node->data;
            config_destruct_m46e_pr(pr_config_entry);
            free(pr_config_entry);
            m46e_list_del(node);
            free(node);
        }
        free(config->pr_conf_table);
    }

    // 設定情報を解放
    free(config);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定ファイル構造体ダンプ関数
//!
//! 引数で指定された設定ファイル構造体の内容をダンプする。
//!
//! @param [in] config ダンプする設定ファイル構造体へのポインタ
//! @param [in] fd     出力先のファイルディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_config_dump(const m46e_config_t* config, int fd)
{
    // ローカル変数宣言
    char address[INET6_ADDRSTRLEN];
    char* strbool[] = { CONFIG_BOOL_FALSE, CONFIG_BOOL_TRUE };

    // 引数チェック
    if(config == NULL){
        return;
    }

    // ローカル変数初期化

    dprintf(fd, "\n");

    // 設定ファイル名
    if(config->filename != NULL){
        dprintf(fd, "Config file name = %s\n", config->filename);
        dprintf(fd, "\n");
    }

    // 共通設定
    if(config->general != NULL){
        dprintf(fd, "[%s]\n", SECTION_GENERAL);
        dprintf(fd, "%s = %s\n", SECTION_GENERAL_PLANE_NAME, config->general->plane_name);
        dprintf(fd, "%s = %s\n", SECTION_GENERAL_PLANE_ID, config->general->plane_id);
        dprintf(fd, "%s = %d\n", SECTION_GENERAL_TUNNEL_MODE, config->general->tunnel_mode);
        dprintf(fd, "%s = %s/%d\n",
            SECTION_GENERAL_UNICAST_PREFIX,
            inet_ntop(AF_INET6, config->general->unicast_prefix, address, sizeof(address)),
            config->general->unicast_prefixlen
        );
        dprintf(fd, "%s = %s/%d\n",
            SECTION_GENERAL_SRC_ADDR_UNICAST_PREFIX,
            inet_ntop(AF_INET6, config->general->src_addr_unicast_prefix, address, sizeof(address)),
            config->general->src_addr_unicast_prefixlen
        );
        dprintf(fd, "%s = %s/%d\n",
            SECTION_GENERAL_MULTICAST_PREFIX,
            inet_ntop(AF_INET6, config->general->multicast_prefix, address, sizeof(address)),
            config->general->multicast_prefixlen
        );
        dprintf(fd, "%s = %s\n", SECTION_GENERAL_DEBUG_LOG, strbool[config->general->debug_log]);
        dprintf(fd, "%s = %s\n", SECTION_GENERAL_DAEMON, strbool[config->general->daemon]);
        dprintf(fd, "%s = %s\n", SECTION_GENERAL_STARTUP_SCRIPT, config->general->startup_script);
        dprintf(fd, "%s = %s\n", SECTION_GENERAL_FORCE_FRAGMENT, strbool[config->general->force_fragment]);
        dprintf(fd, "%s = %s\n", SECTION_ROUTING_SYNC, strbool[config->general->route_sync]);
        dprintf(fd, "%s = %d\n", SECTION_GENERAL_ROUTE_ENTRY_MAX, config->general->route_entry_max);
        dprintf(fd, "\n");
    }

    // M46E-ASモード専用設定
    if(config->m46e_as != NULL){
        dprintf(fd, "[%s]\n", SECTION_M46E_AS);
        dprintf(fd, "%s = %s\n",
            SECTION_M46E_AS_SHARED_ADDRESS,
            inet_ntop(AF_INET, config->m46e_as->shared_address, address, sizeof(address))
        );
        dprintf(fd, "%s = %d\n", SECTION_M46E_AS_START_PORT, config->m46e_as->start_port);
        dprintf(fd, "%s = %d\n", SECTION_M46E_AS_PORT_NUM, config->m46e_as->port_num);
        dprintf(fd, "\n");
    }

    // PMTU Discovery 関連設定
    if(config->pmtud != NULL){
        dprintf(fd, "[%s]\n", SECTION_PMTUD);
        dprintf(fd, "%s = %d\n", SECTION_PMTUD_TYPE, config->pmtud->type);
        dprintf(fd, "%s = %d\n", SECTION_PMTUD_EXPIRE_TIME, config->pmtud->expire_time);
        dprintf(fd, "\n");
    }

    // トンネルデバイス設定
    if(config->tunnel != NULL){
        dprintf(fd, "[%s]\n", SECTION_TUNNEL);
        dprintf(fd, "%s = %s\n", SECTION_TUNNEL_NAME, config->tunnel->ipv4.name);
        if(config->tunnel->ipv4.ipv4_address != NULL){
            dprintf(fd, "%s = %s/%d\n",
                SECTION_TUNNEL_IPV4_ADDRESS,
                inet_ntop(AF_INET, config->tunnel->ipv4.ipv4_address, address, sizeof(address)),
                config->tunnel->ipv4.ipv4_netmask
            );
        }
        if(config->tunnel->ipv4.hwaddr != NULL){
            dprintf(fd, "%s = %s\n", SECTION_TUNNEL_IPV4_HWADDR, ether_ntoa(config->tunnel->ipv4.hwaddr));
        }
        if(config->tunnel->ipv6.ipv6_address != NULL){
            dprintf(fd, "%s = %s/%d\n",
                SECTION_TUNNEL_IPV6_ADDRESS,
                inet_ntop(AF_INET6, config->tunnel->ipv6.ipv6_address, address, sizeof(address)),
                config->tunnel->ipv6.ipv6_prefixlen
            );
        }
        if(config->tunnel->ipv6.hwaddr != NULL){
            dprintf(fd, "%s = %s\n", SECTION_TUNNEL_IPV6_HWADDR, ether_ntoa(config->tunnel->ipv6.hwaddr));
        }
        dprintf(fd, "%s = %d\n", SECTION_TUNNEL_MTU, config->tunnel->ipv6.mtu);
        dprintf(fd, "%s = %s\n", SECTION_TUNNEL_IPV4_DEFAULT_GW, config->tunnel->ipv4.ipv4_gateway?CONFIG_BOOL_TRUE:CONFIG_BOOL_FALSE);
        dprintf(fd, "\n");
    }

    // デバイス設定
    m46e_list* iter;
    m46e_list_for_each(iter, &config->device_list){
        m46e_device_t* device = iter->data;
        if(device != NULL){
            dprintf(fd, "[%s]\n", SECTION_DEVICE);
            switch(device->type){
            case M46E_DEVICE_TYPE_VETH:
                dprintf(fd, "%s = %s\n", SECTION_DEVICE_TYPE, SECTION_DEVICE_TYPE_VETH);
                dprintf(fd, "%s = %s\n", SECTION_DEVICE_VETH_BRIDGE, device->option.veth.bridge);
                dprintf(fd, "%s = %s\n", SECTION_DEVICE_VETH_PAIR_NAME, device->option.veth.pair_name);
                break;
            case M46E_DEVICE_TYPE_MACVLAN:
            dprintf(fd, "%s = %s\n", SECTION_DEVICE_TYPE, SECTION_DEVICE_TYPE_MACVLAN);
                switch(device->option.macvlan.mode){
                case MACVLAN_MODE_PRIVATE:
                    dprintf(fd, "%s = %s\n", SECTION_DEVICE_MACVLAN_MODE, SECTION_DEVICE_MACVLAN_MODE_PRIVATE);
                    break;
                case MACVLAN_MODE_VEPA:
                    dprintf(fd, "%s = %s\n", SECTION_DEVICE_MACVLAN_MODE, SECTION_DEVICE_MACVLAN_MODE_VEPA);
                    break;
                case MACVLAN_MODE_BRIDGE:
                    dprintf(fd, "%s = %s\n", SECTION_DEVICE_MACVLAN_MODE, SECTION_DEVICE_MACVLAN_MODE_BRIDGE);
                    break;
#ifdef HAVE_MACVLAN_MODE_PASSTHRU
                case MACVLAN_MODE_PASSTHRU:
                    dprintf(fd, "%s = %s\n", SECTION_DEVICE_MACVLAN_MODE, SECTION_DEVICE_MACVLAN_MODE_PASSTHRU);
                    break;
#endif
                default:
                    // ありえない
                    break;
                }
                break;
            case M46E_DEVICE_TYPE_PHYSICAL:
                dprintf(fd, "%s = %s\n", SECTION_DEVICE_TYPE, SECTION_DEVICE_TYPE_PHYS);
                break;
            default:
                // ありえない
                break;
            }
            dprintf(fd, "%s = %s\n", SECTION_DEVICE_NAME, device->name);
            dprintf(fd, "%s = %s\n", SECTION_DEVICE_PHYS_DEV, device->physical_name);
            if(device->ipv4_address != NULL){
                dprintf(fd, "%s = %s/%d\n",
                    SECTION_DEVICE_IPV4_ADDRESS,
                    inet_ntop(AF_INET, device->ipv4_address, address, sizeof(address)),
                    device->ipv4_netmask
                );
            }
            if(device->ipv4_gateway != NULL){
                dprintf(fd, "%s = %s\n",
                    SECTION_DEVICE_IPV4_GATEWAY,
                    inet_ntop(AF_INET, device->ipv4_gateway, address, sizeof(address))
                );
            }
            dprintf(fd, "%s = %d\n", SECTION_DEVICE_MTU, device->mtu);
            if(device->hwaddr != NULL){
                dprintf(fd, "%s = %s\n", SECTION_DEVICE_HWADDR, ether_ntoa(device->hwaddr));
            }
            dprintf(fd, "\n");
        }
    }

    dprintf(fd, "\n");

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定情報格納用構造体初期化関数
//!
//! 設定情報格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_init(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return;
    }

    // 設定ファイル名
    config->filename    = NULL;
    // 共通設定
    config->general     = NULL;
    // M46E-ASモード専用設定
    config->m46e_as    = NULL;
    // Path MTU Discovery 関連設定
    config->pmtud       = NULL;
    // トンネルデバイス設定
    config->tunnel      = NULL;
    // デバイス設定リスト
    m46e_list_init(&config->device_list);

    // M46E-PR Config Table
    config->pr_conf_table = malloc(sizeof(m46e_pr_config_table_t));
    if(config->pr_conf_table == NULL){
        return;
    }
    config->pr_conf_table->num = 0;
    m46e_list_init(&config->pr_conf_table->entry_list);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定情報整合性チェック関数
//!
//! 設定情報全体での整合性をチェックする。
//!
//! @param [in] config  設定情報格納用構造体へのポインタ
//!
//! @retval true   整合性OK
//! @retval false  整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_all(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;
    }

    // 必須情報が設定されているかチェック
    if(config->general == NULL){
        m46e_logging(LOG_ERR, "[%s] section is not found", SECTION_GENERAL);
        return false;
    }
    if((config->m46e_as == NULL) &&
       (config->general->tunnel_mode == M46E_TUNNEL_MODE_AS)){
        m46e_logging(LOG_ERR, "M46E-AS mode, but [%s] section not found", SECTION_M46E_AS);
        return false;
    }
    if(config->tunnel == NULL){
        m46e_logging(LOG_ERR, "[%s] section is not found", SECTION_TUNNEL);
        return false;
    }

    // PMTUDが設定されていない場合はデフォルト値で生成する
    if(config->pmtud == NULL){
        m46e_logging(LOG_INFO, "[%s] section is not found. generate initial setting", SECTION_PMTUD);
        if(!config_init_pmtud(config)){
            return false;
        }
    }

    if(config->general->plane_id == NULL){
        const char plane_id_default[] = "0:0";
        config->general->plane_id = strdup(plane_id_default);
        m46e_logging(LOG_INFO, "plane_id is not found. generate initial setting");
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 共通設定格納用構造体初期化関数
//!
//! 共通設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool config_init_general(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;;
    }

    if(config->general != NULL){
        // 既に共通設定が格納済みの場合はNGを返す。
        m46e_logging(LOG_ERR, "[%s] section is already loaded. check duplicate", SECTION_GENERAL);
        return false;
    }

    config->general = malloc(sizeof(m46e_config_general_t));
    if(config->general == NULL){
        return false;
    }

    // 設定初期化
    config->general->plane_name          = NULL;
    config->general->plane_id            = NULL;
    config->general->unicast_prefix      = NULL;
    config->general->unicast_prefixlen   = -1;
    config->general->src_addr_unicast_prefix= NULL;
    config->general->src_addr_unicast_prefixlen= -1;
    config->general->multicast_prefix    = NULL;
    config->general->multicast_prefixlen = -1;
    config->general->tunnel_mode         = M46E_TUNNEL_MODE_NONE;
    config->general->debug_log           = false;
    config->general->daemon              = true;
    config->general->startup_script      = NULL;
    config->general->force_fragment      = false;
    config->general->route_sync        = false;
    config->general->route_entry_max     = 256;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 共通設定格納用構造体解放関数
//!
//! 共通設定格納用構造体を解放する。
//!
//! @param [in,out] general   共通設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_general(m46e_config_general_t* general)
{
    // 引数チェック
    if(general == NULL){
        return;
    }

    free(general->plane_name);
    free(general->plane_id);
    free(general->unicast_prefix);
    free(general->src_addr_unicast_prefix);
    free(general->multicast_prefix);
    free(general->startup_script);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 共通設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値を共通設定構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_general(
    const config_keyvalue_t*  kv,
          m46e_config_t*     config
)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->general == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(SECTION_GENERAL_PLANE_NAME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_PLANE_NAME);
        if(config->general->plane_name == NULL){
            config->general->plane_name = strdup(kv->value);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_GENERAL_PLANE_ID, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_PLANE_ID);
        if(config->general->plane_id == NULL){
            char address[INET6_ADDRSTRLEN] = { 0 };
            strcat(address, "::");
            strcat(address, kv->value);
            struct in6_addr tmp;
            if(!parse_ipv6address(address, &tmp, NULL)){
                result = false;
            }
            else{
                config->general->plane_id = strdup(kv->value);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_GENERAL_TUNNEL_MODE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_TUNNEL_MODE);
        if(config->general->tunnel_mode == M46E_TUNNEL_MODE_NONE){
            int  tmp;
            result = parse_int(kv->value, &tmp, CONFIG_TUNNEL_MODE_MIN, CONFIG_TUNNEL_MODE_MAX);
            if(result){
                switch(tmp){
                case CONFIG_TUNNEL_MODE_NORMAL:
                    config->general->tunnel_mode = M46E_TUNNEL_MODE_NORMAL;
                    break;
                case CONFIG_TUNNEL_MODE_AS:
                    config->general->tunnel_mode = M46E_TUNNEL_MODE_AS;
                    break;
                case CONFIG_TUNNEL_MODE_PR:
                    config->general->tunnel_mode = M46E_TUNNEL_MODE_PR;
                    break;
                default:
                    result = false;
                    break;
                }
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_GENERAL_UNICAST_PREFIX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_UNICAST_PREFIX);
        if(config->general->unicast_prefix == NULL){
            config->general->unicast_prefix = malloc(sizeof(struct in6_addr));
            result = parse_ipv6address(
                kv->value,
                config->general->unicast_prefix,
                &config->general->unicast_prefixlen
            );
            if(result){
                // ユニキャストアドレスかどうかチェック
                result = !IN6_IS_ADDR_MULTICAST(config->general->unicast_prefix);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_GENERAL_SRC_ADDR_UNICAST_PREFIX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_SRC_ADDR_UNICAST_PREFIX);
        if(config->general->src_addr_unicast_prefix == NULL){
            config->general->src_addr_unicast_prefix = malloc(sizeof(struct in6_addr));
            result = parse_ipv6address(
                kv->value,
                config->general->src_addr_unicast_prefix,
                &config->general->src_addr_unicast_prefixlen
            );
            if(result){
                // ユニキャストアドレスかどうかチェック
                result = !IN6_IS_ADDR_MULTICAST(config->general->src_addr_unicast_prefix);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_GENERAL_MULTICAST_PREFIX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_MULTICAST_PREFIX);
        if(config->general->multicast_prefix == NULL){
            config->general->multicast_prefix = malloc(sizeof(struct in6_addr));
            result = parse_ipv6address(
                kv->value,
                config->general->multicast_prefix,
                &config->general->multicast_prefixlen
            );
            if(result){
                // マルチキャストアドレスかどうかチェック
                result = IN6_IS_ADDR_MULTICAST(config->general->multicast_prefix);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_GENERAL_DEBUG_LOG, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_DEBUG_LOG);
        result = parse_bool(kv->value, &config->general->debug_log);
    }
    else if(!strcasecmp(SECTION_GENERAL_DAEMON, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_DAEMON);
        result = parse_bool(kv->value, &config->general->daemon);
    }
    else if(!strcasecmp(SECTION_GENERAL_STARTUP_SCRIPT, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_STARTUP_SCRIPT);
        if(config->general->startup_script == NULL){
            config->general->startup_script = realpath(kv->value, NULL);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_GENERAL_FORCE_FRAGMENT, kv->key)){
      DEBUG_LOG("Match %s.\n", SECTION_GENERAL_FORCE_FRAGMENT);
      result = parse_bool(kv->value, &config->general->force_fragment);
    }
    else if(!strcasecmp(SECTION_ROUTING_SYNC, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_ROUTING_SYNC);
        result = parse_bool(kv->value, &config->general->route_sync);
    }
    else if(!strcasecmp(SECTION_GENERAL_ROUTE_ENTRY_MAX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_GENERAL_ROUTE_ENTRY_MAX);
        result = parse_int(kv->value, &config->general->route_entry_max, CONFIG_ROUTE_ENTRY_MIN, CONFIG_ROUTE_ENTRY_MAX);
    }
    else{
        // 不明なキーなのでスキップ
        m46e_logging(LOG_WARNING, "Ignore unknown key : %s\n", kv->key);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 共通設定の整合性チェック関数
//!
//! 共通設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_general(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;;
    }

    if(config->general == NULL){
        return false;
    }

    // 必須情報が設定されているかをチェック
    if(config->general->plane_name == NULL){
        m46e_logging(LOG_ERR, "plane name is not found");
        return false;
    }

    if(config->general->unicast_prefix == NULL){
        m46e_logging(LOG_ERR, "m46e unicast prefix is not specified");
        return false;
    }

    if(config->general->src_addr_unicast_prefix == NULL){
        m46e_logging(LOG_ERR, "m46e-pr unicast prefix(for src address) is not specified");
        return false;
    }
    if(config->general->multicast_prefix == NULL){
        m46e_logging(LOG_ERR, "m46e multicast prefix is not specified");
        return false;
    }

    // Plane IDと動作モードの整合性チェック
    // Plane IDがM46Eアドレスに収まるかどうかをチェックする。
    // Plane IDがIPv6アドレス形式になっているかどうかは、
    // 解析時にチェック済みなので、ここでは気にしない。
    if(config->general->plane_id != NULL){
        char address[INET6_ADDRSTRLEN] = { 0 };
        strcat(address, "::");
        strcat(address, config->general->plane_id);
        strcat(address, ":0:0");
        if(config->general->tunnel_mode == M46E_TUNNEL_MODE_AS){
            strcat(address, ":0");
        }
        struct in6_addr tmp;
        if(!parse_ipv6address(address, &tmp, NULL)){
            m46e_logging(LOG_ERR, "Invalid plane ID");
            return false;
        }
    }

    // 動作モードとプレフィックス長の整合性チェック
    switch(config->general->tunnel_mode){
    case M46E_TUNNEL_MODE_NORMAL:
        if(config->general->unicast_prefixlen > CONFIG_PLANE_PREFIX_MAX){
            m46e_logging(LOG_ERR, "m46e unicast prefixlen too long");
            return false;
        }
        if(config->general->multicast_prefixlen > CONFIG_PLANE_PREFIX_MAX){
            m46e_logging(LOG_ERR, "m46e multicast prefixlen too long");
            return false;
        }
        // Plane IDが指定されている場合、MAXと等しいプレフィックスは不正とみなす
        // (Plane IDの入る余地が無い為)
        if(config->general->plane_id != NULL){
            if(config->general->unicast_prefixlen == CONFIG_PLANE_PREFIX_MAX){
                m46e_logging(LOG_ERR, "m46e unicast prefixlen too long");
                return false;
            }
            if(config->general->multicast_prefixlen == CONFIG_PLANE_PREFIX_MAX){
                m46e_logging(LOG_ERR, "m46e multicast prefixlen too long");
                return false;
            }
        }
        break;

    case M46E_TUNNEL_MODE_PR:
        if(config->general->src_addr_unicast_prefixlen > CONFIG_PLANE_PREFIX_MAX){
            m46e_logging(LOG_ERR, "m46e-pr unicast prefixlen(for src address) too long");
            return false;
        }
        // Plane IDが指定されている場合、MAXと等しいプレフィックスは不正とみなす
        // (Plane IDの入る余地が無い為)
        if(config->general->plane_id != NULL){
            if(config->general->src_addr_unicast_prefixlen == CONFIG_PLANE_PREFIX_MAX){
                m46e_logging(LOG_ERR, "m46e-pr unicast prefixlen(for src address) too long");
                return false;
            }
        }
        if(config->general->multicast_prefixlen == CONFIG_PLANE_PREFIX_MAX){
            m46e_logging(LOG_ERR, "m46e multicast prefixlen too long");
            return false;
        }
        if(config->general->route_sync == true){
            m46e_logging(LOG_ERR, "Can't set yes to route_sync when tunnel mode is M46E-PR");
            return false;
        }
        break;

    case M46E_TUNNEL_MODE_AS:
        if(config->general->unicast_prefixlen > CONFIG_PLANE_AS_PREFIX_MAX){
            m46e_logging(LOG_ERR, "m46e unicast prefixlen too long");
            return false;
        }
        if(config->general->multicast_prefixlen > CONFIG_PLANE_AS_PREFIX_MAX){
            m46e_logging(LOG_ERR, "m46e multicast prefixlen too long");
            return false;
        }
        // Plane IDが指定されている場合、MAXと等しいプレフィックスは不正とみなす
        // (Plane IDの入る余地が無い為)
        if(config->general->plane_id != NULL){
            if(config->general->unicast_prefixlen == CONFIG_PLANE_AS_PREFIX_MAX){
                m46e_logging(LOG_ERR, "m46e unicast prefixlen too long");
                return false;
            }
            if(config->general->multicast_prefixlen == CONFIG_PLANE_AS_PREFIX_MAX){
                m46e_logging(LOG_ERR, "m46e multicast prefixlen too long");
                return false;
            }
        }
        break;

    case M46E_TUNNEL_MODE_NONE:
    default:
        // ありえない
        m46e_logging(LOG_ERR, "m46e tunnel mode is not specified");
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-ASモード専用設定格納用構造体初期化関数
//!
//! M46E-AS専用設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_m46e_as(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;;
    }

    if(config->m46e_as != NULL){
        // 既にM46E-AS設定が格納済みなので、エラーとする。
        // セクションが複数登録されている場合
        m46e_logging(LOG_ERR, "[%s] section is already loaded. check duplicate", SECTION_M46E_AS);
        return false;
    }

    config->m46e_as = malloc(sizeof(m46e_config_m46e_as_t));
    if(config->m46e_as == NULL){
        return false;
    }

    // 設定初期化
    config->m46e_as->shared_address = NULL;
    config->m46e_as->start_port     = -1;
    config->m46e_as->port_num       = -1;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-ASモード専用設定格納用構造体初期化関数
//!
//! M46E-AS専用設定格納用構造体を解放する。
//!
//! @param [in,out] m46e_as   M46E-ASモード専用設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_m46e_as(m46e_config_m46e_as_t* m46e_as)
{
    // 引数チェック
    if(m46e_as == NULL){
        return;
    }

    free(m46e_as->shared_address);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-ASモード専用設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をM46E-ASモード専用設定構造体に格納する。
//!
//! @param [in]  kv       設定ファイルから読込んだ一行情報
//! @param [out] config   設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_m46e_as(
    const config_keyvalue_t*  kv,
          m46e_config_t*     config
)
{
    // ローカル変数宣言
    bool                     result;
    m46e_config_m46e_as_t* m46e_as;

    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->m46e_as == NULL)){
        return false;
    }

    // ローカル変数初期化
    result   = true;
    m46e_as = config->m46e_as;

    if(!strcasecmp(SECTION_M46E_AS_SHARED_ADDRESS, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_M46E_AS_SHARED_ADDRESS);
        if(m46e_as->shared_address == NULL){
            m46e_as->shared_address = malloc(sizeof(struct in_addr));
            result = parse_ipv4address(kv->value, m46e_as->shared_address, NULL);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_M46E_AS_START_PORT, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_M46E_AS_START_PORT);
        if(m46e_as->start_port == -1){
            result = parse_int(kv->value, &m46e_as->start_port, CONFIG_AS_PORT_MIN, CONFIG_AS_PORT_MAX);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_M46E_AS_PORT_NUM, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_M46E_AS_PORT_NUM);
        if(m46e_as->port_num == -1){
            result = parse_int(kv->value, &m46e_as->port_num, CONFIG_AS_PORT_NUM_MIN, CONFIG_AS_PORT_NUM_MAX);
        }
        else{
            result = false;
        }
    }
    else{
        // 不明なキーなのでスキップ
        m46e_logging(LOG_WARNING, "Ignore unknown key : %s\n", kv->key);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-ASモード専用設定の整合性チェック関数
//!
//! M46E-ASモード専用設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_m46e_as(m46e_config_t* config)
{
    // 引数チェック
    if((config == NULL) || (config->m46e_as == NULL)){
        return false;
    }

    if(config->m46e_as->shared_address == NULL){
        m46e_logging(LOG_ERR, "shared IPv4 address is not specified");
        return false;
    }

    if(config->m46e_as->start_port == -1){
        m46e_logging(LOG_ERR, "start port is not specified");
        return false;
    }

    if(config->m46e_as->port_num == -1){
        m46e_logging(LOG_ERR, "port number is not specified");
        return false;
    }

    bool result = config_check_m46e_as_port_setting(
        config->m46e_as->start_port,
        config->m46e_as->port_num
    );
    if(!result){
        m46e_logging(LOG_ERR, "Invalid setting start port and port number");
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-ASモードのport設定の妥当値チェック関数
//!
//! portとport_numの設定値が以下の規則に従っているかをチェックする<br/>
//!   - port_numが2の乗数になっていること<br/>
//!   - portがport_numで割り切れる値になっていること<br/>
//!   - portが0以外の値の場合、port_num以上の値になっていること<br/>
//!   - portとport_numの合計がTCP/UDPのポート番号の最大値(65535)+1を越えないこと<br/>
//!     (全ポート対象の場合を考慮して65536は許容する)<br/>
//!
//! @param [in] port     管理するポートの先頭番号
//! @param [in] port_num 管理するポートの数
//!
//! @retval true  正常値
//! @retval false 異常値
///////////////////////////////////////////////////////////////////////////////
static bool config_check_m46e_as_port_setting(
    const int port,
    const int port_num
)
{
    if((port_num & (port_num-1)) != 0){
        return false;
    }

    if((port % port_num) != 0){
        return false;
    }

    if((port != 0) && (port < port_num)){
        return false;
    }

    if((port + port_num) > CONFIG_AS_PORT_NUM_MAX){
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery関連設定格納用構造体初期化関数
//!
//! Path MTU Discovery関連設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_pmtud(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;;
    }

    if(config->pmtud != NULL){
        return false;
    }

    config->pmtud = malloc(sizeof(m46e_config_pmtud_t));
    if(config->pmtud == NULL){
        return false;
    }

    // 共通設定
    config->pmtud->type        = CONFIG_PMTUD_TYPE_NONE;
    config->pmtud->expire_time = CONFIG_PMTUD_EXPIRE_TIME_DEFAULT;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery関連設定設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をPath MTU Discovery関連設定構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_pmtud(
    const config_keyvalue_t*  kv,
          m46e_config_t*     config
)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->pmtud == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(SECTION_PMTUD_TYPE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PMTUD_TYPE);
        int  tmp;
        result = parse_int(kv->value, &tmp, CONFIG_PMTUD_TYPE_MIN, CONFIG_PMTUD_TYPE_MAX);
        if(result){
            switch(tmp){
            case CONFIG_PMTUD_TYPE_NONE:
                config->pmtud->type = CONFIG_PMTUD_TYPE_NONE;
                break;
            case CONFIG_PMTUD_TYPE_TUNNEL:
                config->pmtud->type = M46E_PMTUD_TYPE_TUNNEL;
                break;
            case CONFIG_PMTUD_TYPE_HOST:
                config->pmtud->type = M46E_PMTUD_TYPE_HOST;
                break;
            default:
                result = false;
                break;
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcmp(SECTION_PMTUD_EXPIRE_TIME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PMTUD_EXPIRE_TIME);
        result = parse_int(kv->value, &config->pmtud->expire_time, CONFIG_PMTUD_EXPIRE_TIME_MIN, CONFIG_PMTUD_EXPIRE_TIME_MAX);
    }
    else{
        // 不明なキーなのでスキップ
        m46e_logging(LOG_WARNING, "Ignore unknown key : %s\n", kv->key);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイス設定格納用構造体初期化関数
//!
//! トンネルデバイス設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_tunnel(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;;
    }

    if(config->tunnel != NULL){
        m46e_logging(LOG_ERR, "[%s] section is already loaded. check duplicate", SECTION_TUNNEL);
        return false;
    }

    config->tunnel = malloc(sizeof(m46e_config_tunnel_t));
    if(config->tunnel == NULL){
        return false;
    }

    // IPv4側のデバイス情報
    config->tunnel->ipv4.type               = M46E_DEVICE_TYPE_TUNNEL_IPV4;
    config->tunnel->ipv4.name               = NULL;
    config->tunnel->ipv4.physical_name      = NULL;
    config->tunnel->ipv4.ipv4_address       = NULL;
    config->tunnel->ipv4.ipv4_netmask       = -1;
    config->tunnel->ipv4.ipv4_gateway       = NULL;
    config->tunnel->ipv4.ipv6_address       = NULL;
    config->tunnel->ipv4.ipv6_prefixlen     = -1;
    config->tunnel->ipv4.mtu                = -1;
    config->tunnel->ipv4.hwaddr             = NULL;
    config->tunnel->ipv4.ifindex            = -1;
    config->tunnel->ipv4.option.tunnel.mode = IFF_TAP;
    config->tunnel->ipv4.option.tunnel.fd   = -1;

    // IPv6側のデバイス情報
    config->tunnel->ipv6.type               = M46E_DEVICE_TYPE_TUNNEL_IPV6;
    config->tunnel->ipv6.name               = NULL;
    config->tunnel->ipv6.physical_name      = NULL;
    config->tunnel->ipv6.ipv4_address       = NULL;
    config->tunnel->ipv6.ipv4_netmask       = -1;
    config->tunnel->ipv6.ipv4_gateway       = NULL;
    config->tunnel->ipv6.ipv6_address       = NULL;
    config->tunnel->ipv6.ipv6_prefixlen     = -1;
    config->tunnel->ipv6.mtu                = -1;
    config->tunnel->ipv6.hwaddr             = NULL;
    config->tunnel->ipv6.ifindex            = -1;
    config->tunnel->ipv6.option.tunnel.mode = IFF_TAP;
    config->tunnel->ipv6.option.tunnel.fd   = -1;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイス設定格納用構造体解放関数
//!
//! トンネルデバイス設定格納用構造体を解放する。
//!
//! @param [in,out] tunnel   トンネルデバイス設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_tunnel(m46e_config_tunnel_t* tunnel)
{
    // 引数チェック
    if(tunnel == NULL){
        return;
    }

    // IPv4側のデバイス情報
    free(tunnel->ipv4.name);
    free(tunnel->ipv4.physical_name);
    free(tunnel->ipv4.ipv4_address);
    free(tunnel->ipv4.ipv4_gateway);
    free(tunnel->ipv4.ipv6_address);
    free(tunnel->ipv4.hwaddr);

    // IPv6側のデバイス情報
    free(tunnel->ipv6.name);
    free(tunnel->ipv6.physical_name);
    free(tunnel->ipv6.ipv4_address);
    free(tunnel->ipv6.ipv4_gateway);
    free(tunnel->ipv6.ipv6_address);
    free(tunnel->ipv6.hwaddr);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイス設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をトンネルデバイス設定構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_tunnel(
    const config_keyvalue_t*  kv,
          m46e_config_t*     config
)
{
    // ローカル変数宣言
    bool                   result;
    m46e_config_tunnel_t* tunnel;

    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->tunnel == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;
    tunnel = config->tunnel;

    if(!strcasecmp(SECTION_TUNNEL_NAME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_TUNNEL_NAME);
        if(tunnel->ipv4.name == NULL){
            tunnel->ipv4.name = strdup(kv->value);
        }
        else{
            result = false;
        }
        if(tunnel->ipv6.name == NULL){
            tunnel->ipv6.name = strdup(kv->value);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_TUNNEL_IPV4_ADDRESS, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_TUNNEL_IPV4_ADDRESS);
        if(tunnel->ipv4.ipv4_address == NULL){
            tunnel->ipv4.ipv4_address = malloc(sizeof(struct in_addr));
            result = parse_ipv4address(kv->value, tunnel->ipv4.ipv4_address, &tunnel->ipv4.ipv4_netmask);
            if(result && (tunnel->ipv4.ipv4_netmask == 0)){
                // プレフィックスが指定されていない場合はアドレスクラスから計算する
                tunnel->ipv4.ipv4_netmask = config_ipv4_prefix(tunnel->ipv4.ipv4_address);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_TUNNEL_IPV4_HWADDR, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_TUNNEL_IPV4_HWADDR);
        if(tunnel->ipv4.hwaddr == NULL){
            tunnel->ipv4.hwaddr = malloc(sizeof(struct ether_addr));
            result = parse_macaddress( kv->value, tunnel->ipv4.hwaddr);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_TUNNEL_IPV6_ADDRESS, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_TUNNEL_IPV6_ADDRESS);
        if(tunnel->ipv6.ipv6_address == NULL){
            tunnel->ipv6.ipv6_address = malloc(sizeof(struct in6_addr));
            result = parse_ipv6address(kv->value, tunnel->ipv6.ipv6_address, &tunnel->ipv6.ipv6_prefixlen);
            if(result && (tunnel->ipv6.ipv6_prefixlen == 0)){
                // プレフィックスが指定されていない場合は64固定
                tunnel->ipv6.ipv6_prefixlen = 64;
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_TUNNEL_IPV6_HWADDR, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_TUNNEL_IPV6_HWADDR);
        if(tunnel->ipv6.hwaddr == NULL){
            tunnel->ipv6.hwaddr = malloc(sizeof(struct ether_addr));
            result = parse_macaddress(kv->value, tunnel->ipv6.hwaddr);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_TUNNEL_MTU, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_TUNNEL_MTU);
        int tmp;
        result = parse_int(kv->value, &tmp, CONFIG_TUNNEL_MTU_MIN, CONFIG_TUNNEL_MTU_MAX);
        if(result){
            if(tunnel->ipv4.mtu == -1){
                // IPv4側のMTUは設定値からIPv6ヘッダ長(40byte)を引いた値を設定する
                tunnel->ipv4.mtu = tmp - sizeof(struct ip6_hdr);
            }
            else{
                result = false;
            }
            if(tunnel->ipv6.mtu == -1){
                tunnel->ipv6.mtu = tmp;
            }
            else{
                result = false;
            }
        }
    }
    else if(!strcasecmp(SECTION_TUNNEL_IPV4_DEFAULT_GW, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_TUNNEL_IPV4_DEFAULT_GW);
        if(tunnel->ipv4.ipv4_gateway == NULL){
            bool tmp;
            result = parse_bool(kv->value, &tmp);
            if(result && tmp){
                tunnel->ipv4.ipv4_gateway = malloc(sizeof(struct in_addr));
                if(tunnel->ipv4.ipv4_gateway != NULL){
                    tunnel->ipv4.ipv4_gateway->s_addr = INADDR_ANY;
                }
                else{
                    result = false;
                }
            }
        }
        else{
            result = false;
        }
    }
    else{
        // 不明なキーなのでスキップ
        m46e_logging(LOG_WARNING, "Ignore unknown key : %s\n", kv->key);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイス設定の整合性チェック関数
//!
//! トンネルデバイス設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_tunnel(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;;
    }

    if(config->tunnel == NULL){
        return false;
    }

    // 必須情報が設定されているかをチェック
    if(config->tunnel->ipv4.name == NULL){
        m46e_logging(LOG_ERR, "m46e tunnel device is NULL");
        return false;
    }

    if(config->tunnel->ipv6.name == NULL){
        m46e_logging(LOG_ERR, "m46e tunnel device is NULL");
        return false;
    }

    // デバイス名が重複していないかチェック
    if(if_nametoindex(config->tunnel->ipv6.name) != 0){
        m46e_logging(LOG_ERR, "Same name device is found : %s", config->tunnel->ipv6.name);
        return false;
    }

    if(config->tunnel->ipv4.mtu == -1){
        config->tunnel->ipv4.mtu = CONFIG_TUNNEL_MTU_DEFAULT - sizeof(struct ip6_hdr);
    }

    if(config->tunnel->ipv6.mtu == -1){
        config->tunnel->ipv6.mtu = CONFIG_TUNNEL_MTU_DEFAULT;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス設定格納用構造体初期化関数
//!
//! デバイス設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_device(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;;
    }

    m46e_list* node = malloc(sizeof(m46e_list));
    if(node == NULL){
        return false;
    }

    m46e_device_t* device = malloc(sizeof(m46e_device_t));
    if(device == NULL){
        free(node);
        return false;
    }

    // デバイス情報
    device->type               = M46E_DEVICE_TYPE_NONE;
    device->name               = NULL;
    device->physical_name      = NULL;
    device->ipv4_address       = NULL;
    device->ipv4_netmask       = -1;
    device->ipv4_gateway       = NULL;
    device->ipv6_address       = NULL;
    device->ipv6_prefixlen     = -1;
    device->mtu                = -1;
    device->hwaddr             = NULL;
    device->ifindex            = -1;
    memset(&device->option, 0, sizeof(device->option));

    // デバイス情報をノードに設定して、ノードをデバイスリストの最後に追加
    m46e_list_init(node);
    m46e_list_add_data(node, device);
    m46e_list_add_tail(&config->device_list, node);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス設定格納用構造体解放関数
//!
//! デバイス設定格納用構造体を解放する。
//!
//! @param [in,out] device   デバイス設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_device(m46e_device_t* device)
{
    // 引数チェック
    if(device == NULL){
        return;
    }

    // デバイス情報
    free(device->name);
    free(device->physical_name);
    free(device->ipv4_address);
    free(device->ipv4_gateway);
    free(device->ipv6_address);
    free(device->hwaddr);
    if(device->type == M46E_DEVICE_TYPE_VETH){
        free(device->option.veth.bridge);
        free(device->option.veth.pair_name);
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_device(
    const config_keyvalue_t*  kv,
          m46e_config_t*     config
)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL)){
        return false;
    }

    m46e_device_t* device = m46e_list_last_data(&config->device_list);
    if(device == NULL){
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(SECTION_DEVICE_TYPE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_TYPE);
        if(device->type == M46E_DEVICE_TYPE_NONE){
            if(!strcasecmp(SECTION_DEVICE_TYPE_VETH, kv->value)){
                device->type = M46E_DEVICE_TYPE_VETH;
            }
            else if(!strcasecmp(SECTION_DEVICE_TYPE_MACVLAN, kv->value)){
                device->type = M46E_DEVICE_TYPE_MACVLAN;
            }
            else if(!strcasecmp(SECTION_DEVICE_TYPE_PHYS, kv->value)){
                device->type = M46E_DEVICE_TYPE_PHYSICAL;
            }
            else{
                result = false;
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_DEVICE_NAME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_NAME);
        if(device->name == NULL){
            device->name = strdup(kv->value);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_DEVICE_PHYS_DEV, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_PHYS_DEV);
        if(device->physical_name == NULL){
            device->physical_name = strdup(kv->value);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_DEVICE_IPV4_ADDRESS, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_IPV4_ADDRESS);
        if(device->ipv4_address == NULL){
            device->ipv4_address = malloc(sizeof(struct in_addr));
            result = parse_ipv4address(kv->value, device->ipv4_address, &device->ipv4_netmask);
            if(result && (device->ipv4_netmask == 0)){
                // プレフィックスが指定されていない場合はアドレスクラスから計算する
                device->ipv4_netmask = config_ipv4_prefix(device->ipv4_address);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_DEVICE_IPV4_GATEWAY, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_IPV4_GATEWAY);
        if(device->ipv4_gateway == NULL){
            device->ipv4_gateway = malloc(sizeof(struct in_addr));
            result = parse_ipv4address(kv->value, device->ipv4_gateway, NULL);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_DEVICE_HWADDR, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_HWADDR);
        if(device->hwaddr == NULL){
            device->hwaddr = malloc(sizeof(struct ether_addr));
            result = parse_macaddress(kv->value, device->hwaddr);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_DEVICE_MTU, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_MTU);
        result = parse_int(kv->value, &device->mtu, CONFIG_DEVICE_MTU_MIN, CONFIG_DEVICE_MTU_MAX);
    }
    else if(!strcasecmp(SECTION_DEVICE_VETH_BRIDGE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_VETH_BRIDGE);
        if(device->option.veth.bridge == NULL){
            device->option.veth.bridge = strdup(kv->value);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_DEVICE_VETH_PAIR_NAME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_VETH_PAIR_NAME);
        if(device->option.veth.pair_name == NULL){
            device->option.veth.pair_name = strdup(kv->value);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_DEVICE_MACVLAN_MODE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_DEVICE_MACVLAN_MODE);
        if(device->option.macvlan.mode == 0){
            if(!strcasecmp(SECTION_DEVICE_MACVLAN_MODE_PRIVATE, kv->value)){
                device->option.macvlan.mode = MACVLAN_MODE_PRIVATE;
            }
            else if(!strcasecmp(SECTION_DEVICE_MACVLAN_MODE_VEPA, kv->value)){
                device->option.macvlan.mode = MACVLAN_MODE_VEPA;
            }
            else if(!strcasecmp(SECTION_DEVICE_MACVLAN_MODE_BRIDGE, kv->value)){
                device->option.macvlan.mode = MACVLAN_MODE_BRIDGE;
            }
#ifdef HAVE_MACVLAN_MODE_PASSTHRU
            else if(!strcasecmp(SECTION_DEVICE_MACVLAN_MODE_PASSTHRU, kv->value)){
                device->option.macvlan.mode = MACVLAN_MODE_PASSTHRU;
            }
#endif
            else{
                result = false;
            }
        }
        else{
            result = false;
        }
    }
    else{
        // 不明なキーなのでスキップ
        m46e_logging(LOG_WARNING, "Ignore unknown key : %s\n", kv->key);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス設定の整合性チェック関数
//!
//! デバイス設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_device(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;;
    }

    m46e_device_t* device = m46e_list_last_data(&config->device_list);
    if(device == NULL){
        return false;
    }

    // 必須情報が設定されているかをチェック
    if(device->physical_name == NULL){
        m46e_logging(LOG_ERR, "Physical device name is NULL");
        return false;
    }
    // デバイスが存在するかチェック
    if(if_nametoindex(device->physical_name) == 0){
        m46e_logging(LOG_ERR, "No such device : %s", device->physical_name);
        return false;
    }

    switch(device->type){
    case M46E_DEVICE_TYPE_VETH:
        if(device->option.veth.bridge == NULL){
            m46e_logging(LOG_ERR, "Bridge device name is NULL");
            return false;
        }
        // ブリッジデバイスの重複チェック
        if(if_nametoindex(device->option.veth.bridge) != 0){
            m46e_logging(LOG_ERR, "Same device name is found : %s", device->option.veth.bridge);
            return false;
        }
        if(device->option.veth.pair_name != NULL){
            // ペアデバイスの重複チェック
            if(if_nametoindex(device->option.veth.pair_name) != 0){
                m46e_logging(LOG_ERR, "Same device name is found : %s", device->option.veth.pair_name);
                return false;
            }
        }
        break;
    case M46E_DEVICE_TYPE_MACVLAN:
        if(device->option.macvlan.mode == 0){
            m46e_logging(LOG_INFO, "macvlan mode is not specified. set private mode");
            device->option.macvlan.mode = MACVLAN_MODE_PRIVATE;
        }
        break;
    case M46E_DEVICE_TYPE_PHYSICAL:
        // なにもしない
        break;
    default:
        // typeが指定されていない場合はMAC-VLANをデフォルト値とする
        DEBUG_LOG("device type is not specified. use MAC-VLAN");
        device->type = M46E_DEVICE_TYPE_MACVLAN;
        if(device->option.macvlan.mode == 0){
            DEBUG_LOG("macvlan mode is not specified. set private mode");
            device->option.macvlan.mode = MACVLAN_MODE_PRIVATE;
        }
        break;
    }

    if(device->name == NULL){
        // デバイス名が指定されていない場合は、物理デバイスの名前をコピーする
        device->name = strdup(device->physical_name);
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Config 情報格納用構造体初期化関数
//!
//! M46E-PR Config 情報格納用構造体を初期化する。
//!
//! @param [in,out] config   M46E-PR Config 情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_m46e_pr(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;
    }

    if(config->pr_conf_table == NULL){
        return false;
    }

    m46e_list* node = malloc(sizeof(m46e_list));
    if(node == NULL){
        return false;
    }

    m46e_pr_config_entry_t* pr_config_entry = malloc(sizeof(m46e_pr_config_entry_t));
    if(pr_config_entry == NULL){
        free(node);
        return false;
    }

    // M46E-PR Config 情報
    pr_config_entry->enable = true;
    pr_config_entry->v4addr = NULL;
    pr_config_entry->v4cidr = -1;
    pr_config_entry->pr_prefix = NULL;
    pr_config_entry->v6cidr = -1;

    // M46E-PR Config 情報をノードに設定して、
    // ノードをM46E-PR Config Entry listの最後に追加
    m46e_list_init(node);
    m46e_list_add_data(node, pr_config_entry);
    m46e_list_add_tail(&config->pr_conf_table->entry_list, node);
    config->pr_conf_table->num++;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Config 情報格納用構造体解放関数
//!
//! M46E-PR Config 情報格納用構造体を解放する。
//!
//! @param [in,out] pr_config_entry   M46E-PR Config 情報格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_m46e_pr(m46e_pr_config_entry_t* pr_config_entry)
{
    // 引数チェック
    if(pr_config_entry == NULL){
        return;
    }

    // M46E-PR Config 情報
    free(pr_config_entry->v4addr);
    free(pr_config_entry->pr_prefix);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Config 情報格納用構造体の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をM46E-PR Config 情報格納用構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_m46e_pr(const config_keyvalue_t* kv, m46e_config_t* config)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL)){
        return false;
    }

   // M46E-PR以外のトンネルモードであれば以降の処理をスキップ
   if(config->general->tunnel_mode != M46E_TUNNEL_MODE_PR){
        return true;
   }

    m46e_pr_config_entry_t* pr_config_entry = m46e_list_last_data(&config->pr_conf_table->entry_list);
    if(pr_config_entry == NULL){
        return false;
    }

    // ローカル変数初期化
    result = true;
    struct in_addr* tmp_addr = NULL;
    int tmp_cidr = -1;

    if(!strcasecmp(SECTION_M46E_PR_IPV4_ADDRESS, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_M46E_PR_IPV4_ADDRESS);
        if(pr_config_entry->v4addr == NULL){
            pr_config_entry->v4addr = malloc(sizeof(struct in_addr));
            tmp_addr = malloc(sizeof(struct in_addr));
            result = parse_ipv4address_pr(kv->value, tmp_addr, &tmp_cidr);

            // 同一エントリー有無検索
            if(m46e_search_pr_config_table(config->pr_conf_table, tmp_addr, tmp_cidr) != NULL) {
                m46e_logging(LOG_ERR, "This entry is already exists.");
                return false;
            }
            pr_config_entry->v4addr = tmp_addr;
            pr_config_entry->v4cidr = tmp_cidr;
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_M46E_PR_PREFIX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_M46E_PR_PREFIX);
        if(pr_config_entry->pr_prefix == NULL){
            pr_config_entry->pr_prefix = malloc(sizeof(struct in6_addr));
            result = parse_ipv6address(kv->value, pr_config_entry->pr_prefix, &pr_config_entry->v6cidr);
        }
        else{
            result = false;
        }
        if(pr_config_entry->v6cidr < 0 || pr_config_entry->v6cidr > 128){
            result = false;
        }
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Config 情報の整合性チェック関数
//!
//! M46E-PR Config 情報の設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_m46e_pr(m46e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;
    }

   // M46E-PR以外のトンネルモードであれば以降の処理をスキップ
   if(config->general->tunnel_mode != M46E_TUNNEL_MODE_PR){
        return true;
   }

    m46e_pr_config_entry_t* pr_config_entry = m46e_list_last_data(&config->pr_conf_table->entry_list);
    if(pr_config_entry == NULL){
        return false;
    }

    // IPv4ネットワークアドレスチェック
    if (!m46eapp_pr_check_network_addr(pr_config_entry->v4addr, pr_config_entry->v4cidr)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(Invalid network address).");
        return false;
    }

    // 必須情報が設定されているかをチェック
    // pr_config_entry->enable はチェックしない

    if((pr_config_entry->v4addr == NULL) || IN_MULTICAST(ntohl(pr_config_entry->v4addr->s_addr))){
        m46e_logging(LOG_ERR, "Dest IPv4 network address is not specified");
        return false;
    }

    if(pr_config_entry->v4cidr == -1){
        m46e_logging(LOG_ERR, "Dest IPv4 subnet mask is not specified");
        return false;
    }

    if((pr_config_entry->pr_prefix == NULL) || IN6_IS_ADDR_MULTICAST(pr_config_entry->pr_prefix)){
        m46e_logging(LOG_ERR, "M46E-PR address is not specified");
        return false;
    }

    if(pr_config_entry->v6cidr == -1){
        m46e_logging(LOG_ERR, "M46E-PR address prefix length is invalid");
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief セクション行判定関数
//!
//! 引数で指定された文字列がセクション行に該当するかどうかをチェックし、
//! セクション行の場合は、出力パラメータにセクション種別を格納する。
//!
//! @param [in]  line_str  設定ファイルから読込んだ一行文字列
//! @param [out] section   セクション種別格納先ポインタ
//!
//! @retval true  引数の文字列がセクション行である
//! @retval false 引数の文字列がセクション行でない
///////////////////////////////////////////////////////////////////////////////
static bool config_is_section(const char* line_str, config_section* section)
{
    // ローカル変数宣言
    regex_t    preg;
    size_t     nmatch = 2;
    regmatch_t pmatch[nmatch];
    bool       result;

    // 引数チェック
    if(line_str == NULL){
        return false;
    }

    if (regcomp(&preg, SECTION_LINE_REGEX, REG_EXTENDED|REG_NEWLINE) != 0) {
        DEBUG_LOG("regex compile failed.\n");
        return false;
    }

    DEBUG_LOG("String = %s\n", line_str);

    if (regexec(&preg, line_str, nmatch, pmatch, 0) == REG_NOMATCH) {
        result = false;
    }
    else {
        result = true;
        // セクションのOUTパラメータが指定されている場合はセクション名チェック
        if((section != NULL) && (pmatch[1].rm_so >= 0)){
            if(!strncmp(SECTION_GENERAL, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_GENERAL);
                *section = CONFIG_SECTION_GENERAL;
            }
            else if(!strncmp(SECTION_M46E_AS, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_M46E_AS);
                *section = CONFIG_SECTION_M46E_AS;
            }
            else if(!strncmp(SECTION_PMTUD, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_PMTUD);
                *section = CONFIG_SECTION_PMTUD;
            }
            else if(!strncmp(SECTION_TUNNEL, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_TUNNEL);
                *section = CONFIG_SECTION_TUNNEL;
            }
            else if(!strncmp(SECTION_DEVICE, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_DEVICE);
                *section = CONFIG_SECTION_DEVICE;
            }
            else if(!strncmp(SECTION_M46E_PR, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_M46E_PR);
                *section = CONFIG_SECTION_M46E_PR;
            }
            else{
                m46e_logging(LOG_ERR, "unknown section(%.*s)\n", (pmatch[1].rm_eo-pmatch[1].rm_so), &line_str[pmatch[1].rm_so]);
                *section = CONFIG_SECTION_UNKNOWN;
            }
        }
    }

    regfree(&preg);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief KEY/VALUE行判定関数
//!
//! 引数で指定された文字列がKEY/VALUE行に該当するかどうかをチェックし、
//! KEY/VALUE行の場合は、出力パラメータにKEY/VALUE値を格納する。
//!
//! @param [in]  line_str  設定ファイルから読込んだ一行文字列
//! @param [out] kv        KEY/VALUE値格納先ポインタ
//!
//! @retval true  引数の文字列がKEY/VALUE行である
//! @retval false 引数の文字列がKEY/VALUE行でない
///////////////////////////////////////////////////////////////////////////////
static bool config_is_keyvalue(const char* line_str, config_keyvalue_t* kv)
{
    // ローカル変数宣言
    regex_t    preg;
    size_t     nmatch = 3;
    regmatch_t pmatch[nmatch];
    bool       result;

    // 引数チェック
    if((line_str == NULL) || (kv == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;

    if (regcomp(&preg, KV_REGEX, REG_EXTENDED|REG_NEWLINE) != 0) {
        DEBUG_LOG("regex compile failed.\n");
        return false;
    }

    if (regexec(&preg, line_str, nmatch, pmatch, 0) == REG_NOMATCH) {
        DEBUG_LOG("No match.\n");
        result = false;
    }
    else {
        sprintf(kv->key,   "%.*s", (pmatch[1].rm_eo-pmatch[1].rm_so), &line_str[pmatch[1].rm_so]);
        sprintf(kv->value, "%.*s", (pmatch[2].rm_eo-pmatch[2].rm_so), &line_str[pmatch[2].rm_so]);
        DEBUG_LOG("Match. key=\"%s\", value=\"%s\"\n", kv->key, kv->value);
    }

    regfree(&preg);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief IPv4アドレスからプレフィックス長を計算する。
//!
//! 引数で指定されたIPv4アドレスからアドレスクラスに基づいた
//! プレフィックス長(ネットマスク)を計算して戻り値として返す。
//!
//! @param [in]  addr     IPv4アドレス
//!
//! @return プレフィックス長
///////////////////////////////////////////////////////////////////////////////
static int config_ipv4_prefix(const struct in_addr* addr)
{
    // ローカル変数宣言
    int netmask;

    // 引数チェック
    if(addr == NULL){
        return -1;
    }

    if(IN_CLASSA(ntohl(addr->s_addr))){
        netmask = CONFIG_IPV4_NETMASK_MAX - IN_CLASSA_NSHIFT;
    }
    else if(IN_CLASSB(ntohl(addr->s_addr))){
        netmask = CONFIG_IPV4_NETMASK_MAX - IN_CLASSB_NSHIFT;
    }
    else if(IN_CLASSC(ntohl(addr->s_addr))){
        netmask = CONFIG_IPV4_NETMASK_MAX - IN_CLASSC_NSHIFT;
    }
    else{
        // ありえない
        netmask = -1;
    }

    return netmask;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をbool値に変換する
//!
//! 引数で指定された文字列がyes or noの場合に、yesならばtrueに、
//! noならばfalseに変換して出力パラメータに格納する。
//!
//! @param [in]  str     変換対象の文字列
//! @param [out] output  変換結果の出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がbool値でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_bool(const char* str, bool* output)
{
    // ローカル変数定義
    bool result;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(str, CONFIG_BOOL_TRUE)){
        *output = true;
    }
    else if(!strcasecmp(str, CONFIG_BOOL_FALSE)){
        *output = false;
    }
    else{
        result = false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列を整数値に変換する
//!
//! 引数で指定された文字列が整数値で、且つ最小値と最大値の範囲に
//! 収まっている場合に、数値型に変換して出力パラメータに格納する。
//!
//! @param [in]  str     変換対象の文字列
//! @param [out] output  変換結果の出力先ポインタ
//! @param [in]  min     変換後の数値が許容する最小値
//! @param [in]  max     変換後の数値が許容する最大値
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列が整数値でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_int(const char* str, int* output, const int min, const int max)
{
    // ローカル変数定義
    bool  result;
    int   tmp;
    char* endptr;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = 0;
    endptr = NULL;

    tmp = strtol(str, &endptr, 10);

    if((tmp == LONG_MIN || tmp == LONG_MAX) && (errno != 0)){
        // strtol内でエラーがあったのでエラー
        result = false;
    }
    else if(endptr == str){
        // strtol内でエラーがあったのでエラー
        result = false;
    }
    else if(tmp > max){
        // 最大値よりも大きいのでエラー
        result = false;
    }
    else if(tmp < min) {
        // 最小値よりも小さいのでエラー
        result = false;
    }
    else if (*endptr != '\0') {
        // 最終ポインタが終端文字でない(=文字列の途中で変換が終了した)のでエラー
        result = false;
    }
    else {
        // ここまでくれば正常に変換できたので、出力変数に格納
        *output = tmp;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をIPv4アドレス型に変換する
//!
//! 引数で指定された文字列がIPv4アドレスのフォーマットの場合に、
//! IPv4アドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//! @param [out] prefixlen  プレフィックス長出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がIPv4アドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_ipv4address(const char* str, struct in_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;

    DEBUG_LOG("%s start", __func__);

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = strdup(str);

    if(tmp == NULL){
        return false;
    }

    token = strtok(tmp, "/");
    if(tmp){
        if(inet_pton(AF_INET, token, output) <= 0){
            result = false;
        }
        else{
            result = true;
        }
    }

    if(result && (prefixlen != NULL)){
        token = strtok(NULL, "/");
        if(token == NULL){
            *prefixlen = 0;
        }
        else{
            result = parse_int(token, prefixlen, CONFIG_IPV4_NETMASK_MIN, CONFIG_IPV4_NETMASK_MAX);
        }
    }

    free(tmp);

    DEBUG_LOG("%s end. return %s", __func__, result?"true":"false");
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列(IPv4 ネットワークアドレス)をIPv4アドレス型に変換する
//! ※M46E-PRのみ
//! 引数で指定された文字列がIPv4アドレスのフォーマットの場合に、
//! IPv4アドレス型に変換して出力パラメータに格納する。
//! (default gateway:0.0.0.0, CIDR:0 設定用)
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//! @param [out] prefixlen  プレフィックス長出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がIPv4アドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_ipv4address_pr(const char* str, struct in_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;

    DEBUG_LOG("%s start", __func__);

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = strdup(str);

    if(tmp == NULL){
        return false;
    }

    token = strtok(tmp, "/");
    if(tmp){
        if(inet_pton(AF_INET, token, output) <= 0){
            result = false;
        }
        else{
            result = true;
        }
    }

    if(result && (prefixlen != NULL)){
        token = strtok(NULL, "/");
        if(token == NULL){
            *prefixlen = -1;
            result = false;
        }
        else{
            result = parse_int(token, prefixlen, CONFIG_IPV4_NETMASK_PR_MIN, CONFIG_IPV4_NETMASK_MAX);
        }
    }

    free(tmp);

    DEBUG_LOG("%s end. return %s", __func__, result?"true":"false");
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をIPv6アドレス型に変換する
//!
//! 引数で指定された文字列がIPv6アドレスのフォーマットの場合に、
//! IPv6アドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//! @param [out] prefixlen  プレフィックス長出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がIPv6アドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_ipv6address(const char* str, struct in6_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;

    DEBUG_LOG("%s start", __func__);

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = strdup(str);

    if(tmp == NULL){
        return false;
    }

    token = strtok(tmp, "/");
    if(tmp){
        if(inet_pton(AF_INET6, token, output) <= 0){
            result = false;
        }
        else{
            result = true;
        }
    }

    if(result && (prefixlen != NULL)){
        token = strtok(NULL, "/");
        if(token == NULL){
            *prefixlen = -1;
            result = false;
        }
        else{
            result = parse_int(token, prefixlen, CONFIG_IPV6_PREFIX_MIN, CONFIG_IPV6_PREFIX_MAX);
        }
    }

    free(tmp);

    DEBUG_LOG("%s end. return %s", __func__, result?"true":"false");
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をMACアドレス型に変換する
//!
//! 引数で指定された文字列がMACアドレスのフォーマットの場合に、
//! MACアドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がMACアドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_macaddress(const char* str, struct ether_addr* output)
{
    // ローカル変数定義
    bool result;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    if(ether_aton_r(str, output) == NULL){
        result = false;
    }
    else{
        result = true;
    }

    return result;
}

