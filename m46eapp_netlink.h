/******************************************************************************/
/* ファイル名 : m46eapp_netlink.c                                             */
/* 機能概要   : netlinkソケット送受信クラス ヘッダファイル                    */
/* 修正履歴   : 2011.12.20 M.Nagano 新規作成                                  */
/*              2012.08.08 T.Maeda Phase4向けに全面改版                       */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#ifndef __M46EAPP_NETLINK_H__
#define __M46EAPP_NETLINK_H__

#include <stdint.h>
#include <sys/socket.h>
#include <linux/netlink.h>

/* ------------------ */
/* define result code */
/* ------------------ */
#define RESULT_OK           0
#define RESULT_SYSCALL_NG  -1
#define RESULT_NG           1
#define RESULT_SKIP_NLMSG   2
#define RESULT_DONE         3

#define NETLINK_RCVBUF (16*1024)
#define NETLINK_SNDBUF (16*1024)

//! 受信データ解析関数
typedef int (*netlink_parse_func)(struct nlmsghdr* , int*, void*);

///////////////////////////////////////////////////////////////////////////////
// インライン関数
///////////////////////////////////////////////////////////////////////////////
inline struct rtattr* m46e_nlmsg_tail(struct nlmsghdr* nmsg)
{
    return (struct rtattr*)(((void*)nmsg) + NLMSG_ALIGN(nmsg->nlmsg_len));
}

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
int  m46e_netlink_open(unsigned long group, int* sock_fd, struct sockaddr_nl* local, uint32_t* seq, int* errcd);
void m46e_netlink_close(int sock_fd);
int  m46e_netlink_send(int sock_fd, uint32_t seq, struct nlmsghdr* nlm, int* errcd);
int  m46e_netlink_recv(int sock_fd, struct sockaddr_nl* local_sa, uint32_t seq, int* errcd, netlink_parse_func parse_func, void* data);
int  m46e_netlink_transaction(int sock_fd, struct sockaddr_nl* local_sa, uint32_t seq, struct nlmsghdr* nlm, int* errcd);
int  m46e_netlink_addattr_l(struct nlmsghdr* n, int maxlen, int type, const void* data, int alen);
struct rtattr* m46e_netlink_attr_begin(struct nlmsghdr* n, int maxlen, int attr);
void m46e_netlink_attr_end(struct nlmsghdr* n, struct rtattr* attr);
int  m46e_netlink_parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len);
int m46e_netlink_parse_ack(struct nlmsghdr* nlmsg_h, int* errcd, void* data);

#endif // __M46EAPP_NETLINK_H__
