/******************************************************************************/
/* ファイル名 : m46eapp_rtnetlink.h                                           */
/* 機能概要   : rtnetlink ヘッダファイル                                      */
/* 修正履歴   : 2013.06.06 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_RTNETLINK_H__
#define __M46EAPP_RTNETLINK_H__

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "m46eapp_command.h"


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int m46e_rtnl_parse_rcv(struct nlmsghdr* nlmsg_h, int *errcd, void *data);
int m46e_lib_parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len);
int m46e_netlink_multicast_recv(int sock_fd, struct sockaddr_nl* local_sa, uint32_t seq, int* errcd, netlink_parse_func parse_func, void* data);
int m46e_ctrl_route_entry_sync(int ad, int family, void *route_info_p);

void m46e_print_rtmsg(struct nlmsghdr* nlhdr);

#endif // __M46EAPP_RTNETLINK_H__

