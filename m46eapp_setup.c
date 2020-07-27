/******************************************************************************/
/* ファイル名 : m46eapp_setup.c                                               */
/* 機能概要   : ネットワーク設定クラス ソースファイル                         */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2013.05.24 H.Koganemaru バグ修正                              */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2013.11.15 H.Koganemaru mkstempワーニング対処                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <netinet/ip6.h>
#include <netinet/ether.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "m46eapp.h"
#include "m46eapp_setup.h"
#include "m46eapp_log.h"
#include "m46eapp_network.h"
#include "m46eapp_pr.h"

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static int run_startup_script(struct m46e_handler_t* handler, const char* type, const char* tunnel_name);


///////////////////////////////////////////////////////////////////////////////
//! @brief M46Eプレーンプレフィックス生成関数
//!
//! 設定ファイルのM46EプレフィックスとPlaneIDの値を元に
//! M46Eプレーンプレフィックスを生成する。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_setup_plane_prefix(struct m46e_handler_t* handler)
{
    m46e_config_t* conf = handler->conf;
    uint8_t*        src_addr;
    uint8_t*        dst_addr;
    int             prefixlen;

    if(conf->general->plane_id != NULL){
        // Plane IDが指定されている場合は、Plane IDで初期化する。
        char address[INET6_ADDRSTRLEN] = { 0 };
        strcat(address, "::");
        strcat(address, conf->general->plane_id);
        strcat(address, ":0:0");
        if(conf->general->tunnel_mode == M46E_TUNNEL_MODE_AS){
            strcat(address, ":0");
        }
        // 値の妥当性は設定読み込み時にチェックしているので戻り値は見ない
        inet_pton(AF_INET6, address, &handler->unicast_prefix);
        inet_pton(AF_INET6, address, &handler->src_addr_unicast_prefix);
        inet_pton(AF_INET6, address, &handler->multicast_prefix);
    }
    else{
        // Plane IDが指定されていない場合はALL0で初期化する
        handler->unicast_prefix   = in6addr_any;
        handler->src_addr_unicast_prefix = in6addr_any;
        handler->multicast_prefix = in6addr_any;
    }

    // plefix length分コピーする
    src_addr  = conf->general->unicast_prefix->s6_addr;
    dst_addr  = handler->unicast_prefix.s6_addr;
    prefixlen = conf->general->unicast_prefixlen;
    for(int i = 0; (i < 16 && prefixlen > 0); i++, prefixlen-=CHAR_BIT){
        if(prefixlen >= CHAR_BIT){
            dst_addr[i] = src_addr[i];
        }
        else{
            dst_addr[i] = (src_addr[i] & (0xff << prefixlen)) | (dst_addr[i] & ~(0xff << prefixlen));
            break;
        }
    }

    src_addr  = conf->general->src_addr_unicast_prefix->s6_addr;
    dst_addr  = handler->src_addr_unicast_prefix.s6_addr;
    prefixlen = conf->general->src_addr_unicast_prefixlen;
    for(int i = 0; (i < 16 && prefixlen > 0); i++, prefixlen-=CHAR_BIT){
        if(prefixlen >= CHAR_BIT){
            dst_addr[i] = src_addr[i];
        }
        else{
            dst_addr[i] = (src_addr[i] & (0xff << prefixlen)) | (dst_addr[i] & ~(0xff << prefixlen));
            break;
        }
    }

    src_addr  = conf->general->multicast_prefix->s6_addr;
    dst_addr  = handler->multicast_prefix.s6_addr;
    prefixlen = conf->general->multicast_prefixlen;
    for(int i = 0; (i < 16 && prefixlen > 0); i++, prefixlen-=CHAR_BIT){
        if(prefixlen >= CHAR_BIT){
            dst_addr[i] = src_addr[i];
        }
        else{
            dst_addr[i] = (src_addr[i] & (0xff << prefixlen)) | (dst_addr[i] & ~(0xff << prefixlen));
            break;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ネットワークデバイス生成関数
//!
//! 設定ファイルで指定されているトンネルデバイスと仮想デバイスを生成する。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_create_network_device(struct m46e_handler_t* handler)
{
    m46e_config_t* conf = handler->conf;

    char  devbuf[IFNAMSIZ];
    char* devname;
    int   ret;
    int   filedes = -1;

    // トンネルデバイス生成
    snprintf(devbuf, sizeof(devbuf), "tunXXXXXX");
    filedes = mkstemp(devbuf);
    devname = devbuf;
    unlink(devbuf);
    if(m46e_network_create_tap(devname, &conf->tunnel->ipv4) != 0){
        m46e_logging(LOG_ERR, "fail to create IPv4 tunnel device\n");
        return -1;
    }
    if(m46e_network_create_tap(conf->tunnel->ipv6.name, &conf->tunnel->ipv6) != 0){
        m46e_logging(LOG_ERR, "fail to create IPv6 tunnel device\n");
        return -1;
    }

    // ネットワークデバイス生成
    struct m46e_list* iter;
    m46e_list_for_each(iter, &conf->device_list){
        m46e_device_t* device = iter->data;
        DEBUG_LOG("create device %s\n", device->name);
        switch(device->type){
        case M46E_DEVICE_TYPE_VETH: // vethの場合
            break;

        case M46E_DEVICE_TYPE_MACVLAN: // macvlanの場合
            snprintf(devbuf, sizeof(devbuf), "mvlanXXXXXX");
            filedes = mkstemp(devbuf);
            devname = devbuf;
            unlink(devbuf);
            ret = m46e_network_create_macvlan(devname, device);
            if(ret != 0){
                m46e_logging(LOG_ERR, "fail to create macvlan device : %s\n", strerror(ret));
                return -1;
            }
            break;

        case M46E_DEVICE_TYPE_PHYSICAL: // physicalの場合
            ret = m46e_network_create_physical(device->physical_name, device);
            if(ret != 0){
                m46e_logging(LOG_ERR, "fail to create physical device : %s\n", strerror(ret));
                return -1;
            }
            break;

        default:
            // なにもしない
            break;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ネットワークデバイス削除関数
//!
//! 設定ファイルで指定されている仮想デバイスを削除する。
//! 但し、type=physicalの場合はデバイスの削除ではなく、
//! デバイス名のロールバックをおこなう。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_delete_network_device(struct m46e_handler_t* handler)
{
    m46e_config_t* conf = handler->conf;
    int ret;

    // トンネルデバイスはプロセスが消えれば自動的に削除されるので
    // ここでは何もしない

    // ネットワークデバイス削除
    struct m46e_list* iter;
    m46e_list_for_each(iter, &conf->device_list){
        m46e_device_t* device = iter->data;
        DEBUG_LOG("delete device %s\n", device->name);

        if(device->ifindex == -1){
            continue;
        }

        if(device->type == M46E_DEVICE_TYPE_PHYSICAL){
            // 物理デバイス移動の場合はデバイス名のロールバック
            DEBUG_LOG("rollback physical dev index=%d %s -> %s\n", device->ifindex, device->name, device->physical_name);
            ret = m46e_network_device_rename_by_index(device->ifindex, device->physical_name);
            if(ret != 0){
                m46e_logging(LOG_WARNING, "rollback failed : %s\n", strerror(ret));
            }
        }
        else{
            DEBUG_LOG("delete virtual dev index=%d %s\n", device->ifindex, device->name);
            ret = m46e_network_device_delete_by_index(device->ifindex);
            if(ret != 0){
                // 通常は子プロセス消滅時に自動的に消えているので、必ずこのルートに入るはず
                DEBUG_LOG("delete failed : %s\n", strerror(ret));
            }
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ネットワークデバイス移動関数
//!
//! 設定ファイルで指定されているトンネルデバイスと仮想デバイスを
//! Stubネットワーク空間に移動する。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_move_network_device(struct m46e_handler_t* handler)
{
    int ret;

    // トンネルデバイス移動
    ret = m46e_network_device_move_by_index(
        handler->conf->tunnel->ipv4.ifindex,
        handler->stub_nw_pid
    );
    if(ret != 0){
        m46e_logging(LOG_ERR, "fail to move tunnnel device to stub network  : %s\n", strerror(ret));
        return -1;
    }

    // ネットワークデバイスの移動
    struct m46e_list* iter;
    m46e_list_for_each(iter, &handler->conf->device_list){
        m46e_device_t* device = iter->data;
        DEBUG_LOG("move device %s to stub network\n", device->name);
        ret = m46e_network_device_move_by_index(device->ifindex, handler->stub_nw_pid);
        if(ret != 0){
            m46e_logging(LOG_ERR, "fail to move device to stub network : %s\n", strerror(ret));
            return -1;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク設定関数
//!
//! Stubネットワークの初期設定をおこなう。具体的には
//!
//!   - IPv4トンネルデバイスのデバイス名変更
//!   - IPv4トンネルデバイスへのIPv4アドレスの割り当て
//!     (設定ファイルに書かれていれば)
//!   - 移動した仮想デバイスのデバイス名変更
//!   - 移動した仮想デバイスへのIPv4アドレスの割り当て
//!     (設定ファイルに書かれていれば)
//!   - 移動した仮想デバイスのMACアドレス変更
//!     (設定ファイルに書かれていれば)
//!
//!   をおこなう。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_setup_stub_network(struct m46e_handler_t* handler)
{
    int ret;

    // トンネルデバイスの名前設定
    m46e_network_device_rename_by_index(
        handler->conf->tunnel->ipv4.ifindex,
        handler->conf->tunnel->ipv4.name
    );

    // トンネルデバイスのIPv4アドレス設定
    if(handler->conf->tunnel->ipv4.ipv4_address != NULL){
        m46e_network_add_ipaddr(
            AF_INET,
            handler->conf->tunnel->ipv4.ifindex,
            handler->conf->tunnel->ipv4.ipv4_address,
            handler->conf->tunnel->ipv4.ipv4_netmask
        );
    }

    // ネットワークデバイスの設定
    struct m46e_list* iter;
    m46e_list_for_each(iter, &handler->conf->device_list){
        m46e_device_t* device = iter->data;
        DEBUG_LOG("setup device %s\n", device->name);
        // デバイスの名前設定
        m46e_network_device_rename_by_index(device->ifindex, device->name);

        // デバイスのIPv4アドレス設定
        if(device->ipv4_address != NULL){
            m46e_network_add_ipaddr(
                AF_INET,
                device->ifindex,
                device->ipv4_address,
                device->ipv4_netmask
            );
        }

        // MACアドレスの設定
        if(device->hwaddr != NULL){
            ret = m46e_network_set_hwaddr_by_name(device->name, device->hwaddr);
        }
        else{
			// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 2016/12/06 add start
			// デバイス名からMACアドレスを取得
			m46e_network_set_hwaddr_by_name(device->physical_name, device->physical_hwaddr);
			// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 2016/12/06 add end
            device->hwaddr = malloc(sizeof(struct ether_addr));
            ret = m46e_network_get_hwaddr_by_name(device->name, device->hwaddr);
        }
        if(ret != 0){
            m46e_logging(LOG_WARNING, "%s configure hwaddr error : %s\n", device->name, strerror(ret));
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク起動関数
//!
//! Stubネットワークの起動をおこなう。具体的には
//!
//!   - ループバックデバイスのUP
//!   - IPv4トンネルデバイスのUP
//!   - IPv4トンネルデバイスのデフォルトゲートウェイの設定
//!     (設定ファイルに書かれていれば)
//!   - M46E-AS共有アドレスへの経路設定
//!     (M46E-ASモードの場合のみ)
//!   - 移動した仮想デバイスのUP
//!   - 移動した仮想デバイスのデフォルトゲートウェイの設定
//!     (設定ファイルに書かれていれば)
//!
//!   をおこなう。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_start_stub_network(struct m46e_handler_t* handler)
{
    // ループバックデバイスの活性化
    m46e_network_set_flags_by_name("lo", IFF_UP);

    // トンネルデバイスの活性化
    m46e_network_set_flags_by_index(handler->conf->tunnel->ipv4.ifindex, IFF_UP);
    // デフォルトゲートウェイの設定
    if(handler->conf->tunnel->ipv4.ipv4_gateway != NULL){
        m46e_network_add_gateway(
            AF_INET,
            handler->conf->tunnel->ipv4.ifindex,
            handler->conf->tunnel->ipv4.ipv4_gateway
        );
    }
    else{
        // ASモードでトンネルデバイスがStubネットワークの
        // デフォルトゲートウェイに設定されていない場合、
        // The Internetに接続する側と判断して、共有IPアドレス/32
        // の経路をトンネルデバイスに向ける
        if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_AS){
            m46e_network_add_route(
                AF_INET,
                handler->conf->tunnel->ipv4.ifindex,
                handler->conf->m46e_as->shared_address,
                IPV4_PREFIX_MAX,
                NULL
            );
        }
    }

    // ネットワークデバイスの設定
    struct m46e_list* iter;
    m46e_list_for_each(iter, &handler->conf->device_list){
        m46e_device_t* device = iter->data;
        DEBUG_LOG("startup device %s\n", device->name);
        // デバイスの活性化
        m46e_network_set_flags_by_index(device->ifindex, IFF_UP);

        // デフォルトゲートウェイの設定
        if(device->ipv4_gateway != NULL){
            m46e_network_add_gateway(
                AF_INET,
                device->ifindex,
                device->ipv4_gateway
            );
        }
    }

    // ip_forwardingの設定
    FILE* fp;
    if ((fp = fopen("/proc/sys/net/ipv4/ip_forward", "w")) != NULL) {
        fprintf(fp, "1");
        fclose(fp);
    }
    else{
        m46e_logging(LOG_WARNING, "ipv4/ip_forward set failed.\n");
    }

    if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR){

        bool flag;
        m46e_list* iter;
        m46e_list_for_each(iter, &handler->pr_handler->entry_list){
            m46e_pr_entry_t* pr_entry;
            pr_entry = iter->data;

            flag = false;
            m46e_list* iter_device;
            m46e_list_for_each(iter_device, &handler->conf->device_list){
                m46e_device_t* device  = iter_device->data;
                struct in_addr outaddr;

                if(m46eapp_pr_convert_network_addr(device->ipv4_address, device->ipv4_netmask, &outaddr) == false){
                    continue;
                }

                if( (pr_entry->v4addr.s_addr == outaddr.s_addr) && (pr_entry->v4cidr == device->ipv4_netmask)) {
                    // ヒット
                    flag = true;
                    break;
                } else {
                    // 失敗
                }
            }
            if(flag) {
                // 経路は追加しない。
            } else {
                // 経路を追加
                m46e_network_add_route( AF_INET, handler->conf->tunnel->ipv4.ifindex, &pr_entry->v4addr, pr_entry->v4cidr, NULL);
            }
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク設定関数
//!
//! Backboneネットワークの初期設定をおこなう。具体的には
//!
//!   - IPv6トンネルデバイスへのIPv6アドレスの割り当て
//!     (設定ファイルに書かれていれば)
//!
//!   をおこなう。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_setup_backbone_network(struct m46e_handler_t* handler)
{
    // トンネルデバイスのIPv6アドレス設定
    if(handler->conf->tunnel->ipv6.ipv6_address != NULL){
        m46e_network_add_ipaddr(
            AF_INET6,
            handler->conf->tunnel->ipv6.ifindex,
            handler->conf->tunnel->ipv6.ipv6_address,
            handler->conf->tunnel->ipv6.ipv6_prefixlen
        );
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク起動関数
//!
//! Backboneネットワークの起動をおこなう。具体的には
//!
//!   - IPv6トンネルデバイスのUP
//!   - 物理デバイスのUP
//!   - StubネットワークのIPv4アドレスに対応したBackboneネットワークの経路設定
//!     (通常モードのみ)
//!   - M46E-AS共有アドレスに対応したBackboneネットワークの経路設定
//!     (M46E-ASモードの場合のみ)
//!
//!   をおこなう。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_start_backbone_network(struct m46e_handler_t* handler)
{
    int ret;

    // IPv6側のトンネルデバイス活性化
    ret = m46e_network_set_flags_by_index(
        handler->conf->tunnel->ipv6.ifindex,
        IFF_UP
    );
    if(ret != 0){
        m46e_logging(LOG_WARNING, "IPv6 tunnel device up failed : %s\n", strerror(ret));
    }

    struct in6_addr v6addr = handler->unicast_prefix;
    int             prefixlen;

    struct m46e_list* iter;
    m46e_list_for_each(iter, &handler->conf->device_list){
        m46e_device_t* device = iter->data;

        // 対応する物理デバイスの活性化
        if(device->type != M46E_DEVICE_TYPE_PHYSICAL){
            ret = m46e_network_set_flags_by_name(device->physical_name, IFF_UP);
            if(ret != 0){
                m46e_logging(LOG_WARNING, "Physical device up failed : %s\n", strerror(ret));
            }
        }

        // StubネットワークのIPv4アドレスに対応した
        // Backboneネットワークの経路設定(M46E-PRモード)
        if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_NORMAL){
            DEBUG_LOG("setup backbone routing device %s\n", device->name);

            if(device->ipv4_address != NULL){
                v6addr.s6_addr32[3] = device->ipv4_address->s_addr;
                prefixlen = IPV6_PREFIX_MAX - (IPV4_PREFIX_MAX - device->ipv4_netmask);

                m46e_network_add_route(
                    AF_INET6,
                    handler->conf->tunnel->ipv6.ifindex,
                    &v6addr,
                    prefixlen,
                    NULL
                );
            }
        }

        // StubネットワークのIPv4アドレスに対応した
        // Backboneネットワークの経路設定(M46E-PRモード)
        if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR){
            DEBUG_LOG("setup backbone routing device %s\n", device->name);

            struct in6_addr v6addr_pr;
            struct in_addr v4addr_tmp;
            m46e_pr_config_entry_t* pr_config_entry = NULL;

            if(device->ipv4_address != NULL){
                m46eapp_pr_convert_network_addr(device->ipv4_address,
                    device->ipv4_netmask, &v4addr_tmp);
                pr_config_entry = m46e_search_pr_config_table(
                    handler->conf->pr_conf_table, &v4addr_tmp, device->ipv4_netmask);
                if(pr_config_entry == NULL){
                    continue;
                }

                if(m46e_pr_plane_prefix(pr_config_entry->pr_prefix,
                        pr_config_entry->v6cidr, handler->conf->general->plane_id,
                        &v6addr_pr) == false){
                    m46e_logging(LOG_ERR, "NG(m46e_pr_plane_prefix).");
                    return -1;
                }

                v6addr_pr.s6_addr32[3] = v4addr_tmp.s_addr;
                prefixlen = IPV6_PREFIX_MAX - (IPV4_PREFIX_MAX - pr_config_entry->v4cidr);

                m46e_network_add_route(
                    AF_INET6,
                    handler->conf->tunnel->ipv6.ifindex,
                    &v6addr_pr,
                    prefixlen,
                    NULL
                );
            }
        }

    }

    // トンネルデバイスにIPv4アドレスが設定されている
    // 場合は/128のプレフィックスで経路を設定する(通常モード)
    if(handler->conf->tunnel->ipv4.ipv4_address != NULL){
        if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_NORMAL){
            v6addr.s6_addr32[3] = handler->conf->tunnel->ipv4.ipv4_address->s_addr;
            prefixlen = IPV6_PREFIX_MAX;

            m46e_network_add_route(
                AF_INET6,
                handler->conf->tunnel->ipv6.ifindex,
                &v6addr,
                prefixlen,
                NULL
            );
        }
    }

    if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_AS){
        if(handler->conf->tunnel->ipv4.ipv4_gateway == NULL){
            // ASモードでトンネルデバイスがStubネットワークの
            // デフォルトゲートウェイに設定されていない場合、
            // The Internetに接続する側と判断して、M46E Prefix+PlaneID/80
            // の経路をトンネルデバイスに向ける
            m46e_network_add_route(
                AF_INET6,
                handler->conf->tunnel->ipv6.ifindex,
                &handler->unicast_prefix,
                (IPV6_PREFIX_MAX - IPV4_PREFIX_MAX - PORT_BIT_MAX),
                NULL
            );
        }
        else{
            // 上記以外の場合は、データセンター側に接するネットワークと
            // 判断して、共有IPアドレス＋ポート番号に対応するIPv6経路を
            // トンネルデバイスに向ける
            memcpy(
                &v6addr.s6_addr16[5],
                &handler->conf->m46e_as->shared_address->s_addr,
                sizeof(in_addr_t)
            );
            v6addr.s6_addr16[7] = htons(handler->conf->m46e_as->start_port);

            // ポート数をマスク値に変換(2^*の*を求める)
            prefixlen = IPV6_PREFIX_MAX - PORT_BIT_MAX;
            for(int netmask = 0; netmask < IPV4_PREFIX_MAX; netmask++){
                if((1 << netmask) >= handler->conf->m46e_as->port_num){
                    prefixlen = IPV6_PREFIX_MAX - netmask;
                    break;
                }
            }

            m46e_network_add_route(
                AF_INET6,
                handler->conf->tunnel->ipv6.ifindex,
                &v6addr,
                prefixlen,
                NULL
            );
        }
    }

    // ip_forwardingの設定
    FILE* fp;
    if ((fp = fopen("/proc/sys/net/ipv6/conf/all/forwarding", "w")) != NULL) {
        fprintf(fp, "1");
        fclose(fp);
    }
    else{
        m46e_logging(LOG_WARNING, "ipv4/ip_forward set failed.\n");
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク スタートアップスクリプト実行関数
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_backbone_startup_script(struct m46e_handler_t* handler)
{
    return run_startup_script(handler, "bb", handler->conf->tunnel->ipv6.name);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク スタートアップスクリプト実行関数
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_stub_startup_script(struct m46e_handler_t* handler)
{
    return run_startup_script(handler, "stub", handler->conf->tunnel->ipv4.name);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief スタートアップスクリプト実行関数
//!
//! @param [in]     handler      M46Eハンドラ
//! @param [in]     type         ネットワーク空間種別("bb" or "stub")
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int run_startup_script(
    struct m46e_handler_t* handler,
    const char*             type,
    const char*             tunnel_name
)
{
    m46e_config_t* conf = handler->conf;

    if(conf->general->startup_script == NULL){
        // スクリプトが指定されていないので、何もせずにリターン
        return 0;
    }

    // コマンド長分のデータ領域確保
    char* command = NULL;
    char  uni_addr[INET6_ADDRSTRLEN]   = { 0 };
    char  multi_addr[INET6_ADDRSTRLEN] = { 0 };

    inet_ntop(AF_INET6, &handler->unicast_prefix,   uni_addr, sizeof(uni_addr));
    inet_ntop(AF_INET6, &handler->src_addr_unicast_prefix, uni_addr, sizeof(uni_addr));
    inet_ntop(AF_INET6, &handler->multicast_prefix, multi_addr, sizeof(multi_addr));

    if((uni_addr[strlen(uni_addr)-1]) == '0') {
        // 末端が0となるのは、prefix+plane_id:0:0 のケース。
        // 下位32bit相当'0:0'を除去。
        uni_addr[strlen(uni_addr)-3] = '\0';
    }
    else if((uni_addr[strlen(uni_addr)-1]) == ':') {
        // 末端が:の場合は補正不要。
    }

    if((multi_addr[strlen(multi_addr)-1]) == '0') {
        // 末端が0となるのは、prefix+plane_id:0:0 のケース。
        // 下位32bit相当'0:0'を除去。
        multi_addr[strlen(multi_addr)-3] = '\0';
    }
    else if((multi_addr[strlen(multi_addr)-1]) == ':') {
        // 末端が:の場合は補正不要。
    }

    int command_len = asprintf(&command, "%s %s %s %d %s %s %s 2>&1",
        conf->general->startup_script,
        conf->general->plane_name,
        type,
        conf->general->tunnel_mode,
        uni_addr,
        multi_addr,
        tunnel_name
    );

    if(command_len > 0){
        DEBUG_LOG("run startup script : %s\n", command);

        FILE* fp = popen(command, "r");
        if(fp != NULL){
            char buf[256];
            while(fgets(buf, sizeof(buf), fp) != NULL){
                DEBUG_LOG("script output : %s", buf);
            }
            pclose(fp);
        }
        else{
            m46e_logging(LOG_WARNING, "run script error : %s\n", strerror(errno));
        }

        free(command);
    }

    return 0;
}

// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
///////////////////////////////////////////////////////////////////////////////
//! @brief 物理デバイスのMACアドレスにローカルアドレスを設定
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int  m46e_set_mac_of_physicalDevice_to_localAddr(
    struct m46e_handler_t* handler
)
{
    struct ether_addr *tmp_ether_addr = malloc(sizeof(struct ether_addr));
    struct ether_addr *org_ether_addr = malloc(sizeof(struct ether_addr));
	if( ether_aton_r("02:00:00:00:00:00", tmp_ether_addr) == NULL ){
        return -1;
	}
	struct m46e_list* iter;
	m46e_list_for_each(iter, &handler->conf->device_list){
		m46e_device_t* device = iter->data;
		if(device != NULL){
			if(device->type != M46E_DEVICE_TYPE_PHYSICAL){
				// dprintf(STDOUT_FILENO, "### physical_name = %s\n", device->physical_name);
				// IFをDown
				if( m46e_network_set_flags_by_name( device->physical_name, ~IFF_UP ) == 0 ){
    				// vlanのMACアドレスを保存
					if( m46e_network_get_hwaddr_by_name( device->physical_name, org_ether_addr) == 0 ){
						device->physical_hwaddr = malloc(sizeof(struct ether_addr));
						memcpy(device->physical_hwaddr, org_ether_addr, sizeof(struct ether_addr) );
						// vlanのMACアドレスを「02:00:00:00:00:00」に変更。
						if( m46e_network_set_hwaddr_by_name( device->physical_name, tmp_ether_addr) != 0 ){
							m46e_set_mac_of_physicalDevice( handler );
							return -1;
						}
					}
					else{
						return -1;
					}
				}
				else{
					return -1;
				}
			}
		}
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 物理デバイスのMACアドレスを元に戻す。
//!
//! @param [in]     handler      M46Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int  m46e_set_mac_of_physicalDevice(
    struct m46e_handler_t* handler
)
{
	struct m46e_list* iter;
	m46e_list_for_each(iter, &handler->conf->device_list){
		m46e_device_t* device = iter->data;
		if(device != NULL){
			if(device->type != M46E_DEVICE_TYPE_PHYSICAL){
				if( m46e_network_set_hwaddr_by_name( device->physical_name, device->physical_hwaddr) == 0 ){
					if( m46e_network_set_flags_by_name( device->physical_name, IFF_UP ) != 0 ){
						// 異常終了
						return -1;
					}
				}
				else{
					return -1;
				}
			}
		}
	}
	return 0;
}
// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end

