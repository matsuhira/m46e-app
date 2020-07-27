/******************************************************************************/
/* ファイル名 : m46eapp_network.h                                             */
/* 機能概要   : ネットワーク関連関数 ヘッダファイル                           */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2013.08.21 H.Koganemaru M46E-PR機能拡張                       */
/*              2013.08.30 H.Koganemaru 動的定義変更機能追加                  */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_NETWORK_H__
#define __M46EAPP_NETWORK_H__

#include <unistd.h>

struct ether_addr;
struct m46e_device_t;

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
int m46e_network_create_tap(const char* name, struct m46e_device_t* tunnel_dev);
int m46e_network_create_macvlan(const char* name, struct m46e_device_t* tunnel_dev);
int m46e_network_create_physical(const char* name, struct m46e_device_t* tunnel_dev);
int m46e_network_device_delete_by_index(const int ifindex);
int m46e_network_device_move_by_index(const int ifindex, const pid_t pid);
int m46e_network_device_rename_by_index(const int ifindex, const char* newname);
int m46e_network_get_mtu_by_name(const char* ifname, int* mtu);
int m46e_network_get_mtu_by_index(const int ifindex, int* mtu);
int m46e_network_set_mtu_by_name(const char* ifname, const int mtu);
int m46e_network_set_mtu_by_index(const int ifindex, const int mtu);
int m46e_network_get_hwaddr_by_name(const char* ifname, struct ether_addr* hwaddr);
int m46e_network_get_hwaddr_by_index(const int ifindex, struct ether_addr* hwaddr);
int m46e_network_set_hwaddr_by_name(const char* ifname, const struct ether_addr* hwaddr);
int m46e_network_set_hwaddr_by_index(const int ifindex, const struct ether_addr* hwaddr);
int m46e_network_get_flags_by_name(const char* ifname, short* flags);
int m46e_network_get_flags_by_index(const int ifindex, short* flags);
int m46e_network_set_flags_by_name(const char* ifname, const short flags);
int m46e_network_set_flags_by_index(const int ifindex, const short flags);
int m46e_network_add_ipaddr(const int family, const int ifindex, const void* addr, const int prefixlen);
int m46e_network_add_route(const int family, const int ifindex, const void* dst, const int prefixlen, const void* gw);
int m46e_network_add_gateway(const int family, const int ifindex, const void* gw);
int m46e_network_del_route(const int family, const int ifindex, const void* dst, const int prefixlen, const void* gw);
int m46e_network_del_gateway(const int family, const int ifindex, const void* gw);

#endif // __M46EAPP_NETWORK_H__
