/******************************************************************************/
/* ファイル名 : m46eapp_rtnetlink.c                                           */
/* 機能概要   : 経路同期 rtnetlink ソースファイル                             */
/* 修正履歴   : 2013.06.06 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/shm.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "m46eapp.h"
#include "m46eapp_log.h"
#include "m46eapp_netlink.h"
#include "m46eapp_rtnetlink.h"
#include "m46eapp_mng_com_route.h"
#include "m46eapp_mng_v4_route_data.h"
#include "m46eapp_mng_v6_route_data.h"


/********************************************************************************************************************/
/**
 *  @brief  Parse RT attribute data from netlink message.
 *  @param  tb              [out]  RT attribute address arry list
 *  @param  max             [in]   RT attribute address arry list num
 *  @param  rta             [in]   RT attribute top address
 *  @param  len             [in]   RT attribute area total length
 *  @return result code
 *  @retval RESULT_OK          normal end
 *  @retval RESULT_NG          another error
 */
/********************************************************************************************************************/
int m46e_lib_parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len)
{
    memset(tb, 0, sizeof(struct rtattr *) * (max + 1));

    // 受信したAttributes DataをAttribute type毎のポインタ配列に詰める
    while (RTA_OK(rta, len)) {
        if (rta->rta_type <= max) {
            tb[rta->rta_type] = rta;
            }
        rta = RTA_NEXT(rta,len);
    }

    // メッセージの残サイズが0以外の場合は、エラーとする
    if (len){
        /* rta_type is over arry list max ..*/
        return RESULT_NG;
    }
    return RESULT_OK;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Netlink Message 出力処理
//!
//! Netlink Messageを出力する。
//!
//! @param [in]  nlmsg_h    netlink message
//!
//! @return none
///////////////////////////////////////////////////////////////////////////////
void m46e_print_rtmsg(struct nlmsghdr* nlhdr)
{
    _DS_(char    addr[INET6_ADDRSTRLEN] = { 0 };)

    struct rtattr *tb[RTA_MAX+1];
    struct rtmsg  *rtm;
    struct rtattr *rtattr_p;   /* rt attribute address */
    struct ifreq    ifr;
    int             fd;

    memset(&ifr, 0, sizeof(ifr));
    DEBUG_SYNC_LOG( "============================================\n");

    // Nelink Message type
    switch( nlhdr->nlmsg_type ){
        case RTM_NEWROUTE:
            DEBUG_SYNC_LOG( "NEW ROUTE!!\n");
            break;
        case RTM_DELROUTE:
            DEBUG_SYNC_LOG( "DEL ROUTE!!\n");
            break;
        case RTM_NEWADDR:
            DEBUG_SYNC_LOG( "NEW ADDR!!\n");
            break;
        case RTM_DELADDR:
            DEBUG_SYNC_LOG( "DEL ADDR!!\n");
            break;
        case RTM_NEWLINK:
            DEBUG_SYNC_LOG( "NEW LINK!!\n");
            break;
        case RTM_DELLINK:
            DEBUG_SYNC_LOG( "DEL LINK!!\n");
            break;
        default:
            DEBUG_SYNC_LOG( "OTHER");
            break;
    }

    // Nelink Message lenght
    DEBUG_SYNC_LOG( "len  :%d\n", nlhdr->nlmsg_len);

    // ルートの追加と削除のみ以降の処理を行う
    if ((nlhdr->nlmsg_type != RTM_NEWROUTE) && (nlhdr->nlmsg_type != RTM_DELROUTE)) {
        DEBUG_SYNC_LOG( "ignore message type : %d\n", nlhdr->nlmsg_type);
        return;
    }

    // RTNelink Message header
    rtm = (struct rtmsg*)NLMSG_DATA(nlhdr);
    DEBUG_SYNC_LOG( "rtm_family   :  %d\n", rtm->rtm_family);
    DEBUG_SYNC_LOG( "rtm_dst_len  :  %d\n", rtm->rtm_dst_len);
    DEBUG_SYNC_LOG( "rtm_src_len  :  %d\n", rtm->rtm_src_len);
    DEBUG_SYNC_LOG( "rtm_tos      :  %d\n", rtm->rtm_tos);
    DEBUG_SYNC_LOG( "rtm_table    :  %d\n", rtm->rtm_table);
    DEBUG_SYNC_LOG( "rtm_protocol :  %d\n", rtm->rtm_protocol);
    DEBUG_SYNC_LOG( "rtm_scope    :  %d\n", rtm->rtm_scope);
    DEBUG_SYNC_LOG( "rtm_type     :  %d\n", rtm->rtm_type);
    DEBUG_SYNC_LOG( "rtm_flags    :  %d\n", rtm->rtm_flags);


    // rt attribute
    rtattr_p = (struct rtattr*)(((char*)rtm) + NLMSG_ALIGN(sizeof(struct rtmsg)));
    m46e_lib_parse_rtattr(tb, RTN_MAX, rtattr_p,
            nlhdr->nlmsg_len - NLMSG_LENGTH(sizeof(*rtm)));

    if (rtm->rtm_family == AF_INET) {
        if ((rtm->rtm_type == RTN_UNICAST) && (rtm->rtm_table == RT_TABLE_MAIN)) {
            if( tb[RTA_OIF] ){
                // ether名を取得
                fd = socket( PF_INET, SOCK_DGRAM, 0 );
                ifr.ifr_ifindex = *(int*)RTA_DATA(tb[RTA_OIF]);
                ioctl(fd, SIOCGIFNAME, &ifr);
                close(fd);

                DEBUG_SYNC_LOG( "RTA_OIF     :  %d(%s)\n", *(int*)RTA_DATA(tb[RTA_OIF]) ,ifr.ifr_name);
            }
            if( tb[RTA_IIF] ){
                DEBUG_SYNC_LOG( "RTA_IIF     :  %d\n", *(int*)RTA_DATA(tb[RTA_IIF]));
            }
            if( tb[RTA_SRC] ){
                DEBUG_SYNC_LOG( "RTA_SRC     :  %s\n", inet_ntop(AF_INET, RTA_DATA(tb[RTA_SRC]), addr, sizeof(addr)));
            }
            if( tb[RTA_DST] ){
                DEBUG_SYNC_LOG( "RTA_DST     :  %s\n", inet_ntop(AF_INET, RTA_DATA(tb[RTA_DST]), addr, sizeof(addr)));
            }
            if( tb[RTA_PREFSRC] ){
                DEBUG_SYNC_LOG( "RTA_PREFSRC :  %s\n", inet_ntop(AF_INET, RTA_DATA(tb[RTA_PREFSRC]), addr, sizeof(addr)));
            }
            if( tb[RTA_GATEWAY] ){
                DEBUG_SYNC_LOG( "RTA_GATEWAY :  %s\n", inet_ntop(AF_INET, RTA_DATA(tb[RTA_GATEWAY]), addr, sizeof(addr)));
            }
            if( tb[RTA_PRIORITY] ){
                DEBUG_SYNC_LOG( "RTA_PRIORITY:  %d\n", *(int*)RTA_DATA(tb[RTA_PRIORITY]));
            }
            if( tb[RTA_METRICS] ){
                DEBUG_SYNC_LOG( "RTA_METRICS :  %d\n", *(int*)RTA_DATA(tb[RTA_METRICS]));
            }
        }
    } else if(rtm->rtm_family == AF_INET6) {
        if ((rtm->rtm_type == RTN_UNICAST) && (rtm->rtm_table == RT_TABLE_MAIN)) {
            if( tb[RTA_OIF] ){

                // ether名を取得
                fd = socket( PF_INET, SOCK_DGRAM, 0 );
                ifr.ifr_ifindex = *(int*)RTA_DATA(tb[RTA_OIF]);
                ioctl(fd, SIOCGIFNAME, &ifr);
                close(fd);

                DEBUG_SYNC_LOG( "RTA_OIF     :  %d(%s)\n", *(int*)RTA_DATA(tb[RTA_OIF]) ,ifr.ifr_name);
            }
            if( tb[RTA_IIF] ){
                DEBUG_SYNC_LOG( "RTA_IIF     :  %d\n", *(int*)RTA_DATA(tb[RTA_IIF]));
            }
            if( tb[RTA_SRC] ){
                DEBUG_SYNC_LOG( "RTA_SRC     :  %s\n", inet_ntop(AF_INET6, RTA_DATA(tb[RTA_SRC]), addr, sizeof(addr)));
            }
            if( tb[RTA_DST] ){
                DEBUG_SYNC_LOG( "RTA_DST     :  %s\n", inet_ntop(AF_INET6, RTA_DATA(tb[RTA_DST]), addr, sizeof(addr)));
            }
            if( tb[RTA_PREFSRC] ){
                DEBUG_SYNC_LOG( "RTA_PREFSRC :  %s\n", inet_ntop(AF_INET6, RTA_DATA(tb[RTA_PREFSRC]), addr, sizeof(addr)));
            }
            if( tb[RTA_GATEWAY] ){
                DEBUG_SYNC_LOG( "RTA_GATEWAY :  %s\n", inet_ntop(AF_INET6, RTA_DATA(tb[RTA_GATEWAY]), addr, sizeof(addr)));
            }
            if( tb[RTA_PRIORITY] ){
                DEBUG_SYNC_LOG( "RTA_PRIORITY:  %d\n", *(int*)RTA_DATA(tb[RTA_PRIORITY]));
            }
            if( tb[RTA_METRICS] ){
                DEBUG_SYNC_LOG( "RTA_METRICS :  %d\n", *(int*)RTA_DATA(tb[RTA_METRICS]));
            }
        }
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 経路情報解析関数
//!
//! RTNETLINKからの経路表変更通知受信時に呼ばれ、
//! 経路表情報更新関数を起動する。
//!
//! @param [in]  nlmsg_h    netlink message
//! @param [out] errcd      エラーコード
//! @param [out] handler    M46E アプリケーションハンドラー
//!
//! @return RESULT_OK           正常終了
//! @return RESULT_SKIP_NLMSG   予期せぬメッセージ
//! @return RESULT_NG           異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_rtnl_parse_rcv(struct nlmsghdr* nlmsg_h, int *errcd, void *handler)
{
    struct rtattr *tb[RTA_MAX+1];   /* rt attribute container       */
    struct rtmsg  *rtm;             /* rtnetlink message adddress   */
    struct rtattr *rtattr_p;        /* rt attribute top address     */


    DEBUG_SYNC_LOG("********* rtnetlink message recieve **********\n");

    // マルチパートメッセージの最後のメッセージの場合
    if (nlmsg_h->nlmsg_type == NLMSG_DONE){
        /* Message end (No entry) */
        return RESULT_OK;
    }

    /* NEWNEIGH or DENEIGH Netlink Message ? */
    if ((nlmsg_h->nlmsg_type == RTM_NEWROUTE) ||
            (nlmsg_h->nlmsg_type == RTM_DELROUTE)) {

        _DS_(m46e_print_rtmsg(nlmsg_h);)

        rtm = (struct rtmsg*)NLMSG_DATA(nlmsg_h);

        /* payload length check */
        if (nlmsg_h->nlmsg_len < NLMSG_LENGTH(sizeof(struct rtmsg))) {
            /* too short */

            /* LOG(ERROR) */
            m46e_logging(LOG_ERR, "Neigh netlink message. payload is too short. seq=%d, length=%d",
                   nlmsg_h->nlmsg_seq, nlmsg_h->nlmsg_len);

            return RESULT_NG;
        }

        /* Parse attribute data */
        rtattr_p = (struct rtattr*)(((char*)rtm) + NLMSG_ALIGN(sizeof(struct rtmsg)));

        m46e_lib_parse_rtattr(tb, RTN_MAX, rtattr_p,
                nlmsg_h->nlmsg_len - NLMSG_LENGTH(sizeof(*rtm)));

        if( rtm->rtm_family == AF_INET ){
            /* IPv4 */
            m46e_update_route_info(nlmsg_h->nlmsg_type, AF_INET, rtm, tb, handler);

        }else if(rtm->rtm_family == AF_INET6 ){
            /* IPv6 */
            m46e_update_route_info(nlmsg_h->nlmsg_type, AF_INET6, rtm, tb, handler);
        }
        return RESULT_SKIP_NLMSG;

    }
    else {
        DEBUG_SYNC_LOG("nlmsg_type(%d) is outside.\n", nlmsg_h->nlmsg_type);
    }

    /* Unexpected  messege */
    return RESULT_SKIP_NLMSG;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief  Netlink Multicast message RECV from kernel.
//!
//! @param [in]      sock_fd     Socket descriptor
//! @param [in]      local_sa    Local netlink sock address
//! @param [in]      seq         Sequence number
//! @param [out]     errcd       Detail error code
//! @param [in]      parse_func  The function for parse Netlink Message
//! @param [in,out]  data        Input parameter of parse function.
//!
//! @return result code
//! @retval RESULT_OK          normal end
//! @retval RESULT_SYSCALL_NG  system call error
//! @retval RESULT_NG          another error
///////////////////////////////////////////////////////////////////////////////
//
//  Netlink Message 詳細解析関数(parse_func) 仕様
//
//  int(*parse_func)(struct nlmsghdr* nlmsg_h, int* errcd, void* indata, void* outdata),
//
//  nlmsg_h       [In]      Netlink message 先頭アドレス
//  errcd         [Out]     エラーコード
//  data          [In/Out]  解析用データ
//
//  戻り値   処理結果
//
//  RESULT_OK          正常
//  RESULT_NG          異常
//  RESULT_SKIP_NLMSG  対象外データ
//
//  戻り値が対象外データの場合は、次のNetlink messageについて
//  再度解析関数をコールする。
//
//  解析関数内では、nlmsg_hで指定されたNetlink Messageをdataの情報を元に解析し
//  dataに解析結果を設定する処理を盛り込む。
//  なお、解析用データ(data)が不要の場合はdataにNULL設定すること
//  dataの情報は解析関数で必要な型定義と領域を確保しておくこと
//
///////////////////////////////////////////////////////////////////////////////
int m46e_netlink_multicast_recv(
    int                 sock_fd,
    struct sockaddr_nl* local_sa,
    uint32_t            seq,
    int*                errcd,
    netlink_parse_func  parse_func,
    void*               data
)
{
    int                status;
    struct nlmsghdr*   nlmsg_h;
    struct sockaddr_nl nladdr;
    socklen_t          nladdr_len = sizeof(nladdr);
    void*              buf;
    int                ret;

    buf = malloc(NETLINK_RCVBUF); /* 16kbyte */
    if( buf == NULL ){
        *errcd = errno;
        m46e_logging(LOG_ERR, "recv buf malloc ng. errno=%d\n",errno);
        return RESULT_SYSCALL_NG;
    }

    // サイズが大きいため、初期化せず使用
    // memset(buf, 0, sizeof(buf));

    /* ------------------------------ */
    /* Recv Netlink Message           */
    /* ------------------------------ */
    while (1) {
        status = recvfrom(sock_fd, buf, NETLINK_RCVBUF, 0, (struct sockaddr*)&nladdr, &nladdr_len);

        /* recv error */
        if (status < 0) {
            if((errno == EINTR) || (errno == EAGAIN)){
                /* Interrupt */
                continue;
            }

            *errcd = errno;

            /* LOG(ERROR) */
            m46e_logging(LOG_ERR, "Recieve netlink msg error. seq=%d,errno=%d\n", seq, errno);
            free(buf);
            return RESULT_SYSCALL_NG;
        }

        /* No data */
        if (status == 0) {
            /* LOG(ERROR) */
            m46e_logging(LOG_ERR, "EOF on netlink. seq=%d\n", seq);
            free(buf);
            return RESULT_NG;
        }

        /* Sockaddr length check */
        if (nladdr_len != sizeof(nladdr)) {
            /* LOG(ERROR) */
            m46e_logging(LOG_ERR, "Illegal sockaddr length. length=%d,seq=%d\n", nladdr_len, seq);
            free(buf);
            return RESULT_NG;
        }

        /* ------------------------------ */
        /* Parse Netlink Message          */
        /* ------------------------------ */
        nlmsg_h = (struct nlmsghdr*)buf;
        while (NLMSG_OK(nlmsg_h, status)) {

            /* process id & sequence number check */
            if (nladdr.nl_pid != 0)           /* From pid is kernel? */
               {

                /* That netlink message is not my expected msg. */

                /* LOG(ERROR) */
                m46e_logging(LOG_ERR, "Unexpected netlink msg recieve. From pid=%d To pid=%d/%d seq=%d/%d\n",
                       nladdr.nl_pid,
                       nlmsg_h->nlmsg_pid, local_sa->nl_pid,
                       nlmsg_h->nlmsg_seq, seq);

                nlmsg_h = NLMSG_NEXT(nlmsg_h, status);


                continue;
            }

            /* -------------------------------------------------- */
            /* Call the function of parse Netlink Message detail. */
            /* -------------------------------------------------- */
            ret = parse_func(nlmsg_h, errcd, data);

            /* RESULT_SKIP_NLMSG is skip messge */
            if(ret != RESULT_SKIP_NLMSG){
                free(buf);
                /* Finish netlink message recieve & parse */
                return ret;
            }

            /* message skip */
            nlmsg_h = NLMSG_NEXT(nlmsg_h, status);
        }

        /* Recieve message Remain? */
        if (status) {
            /* LOG(ERROR) */
            m46e_logging(LOG_ERR, "Recieve message Remant of size. remsize=%d,seq=%d\n", status, seq);
            free(buf);
            return RESULT_NG;
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
//! @brief 経路同期設定関数
//!
//! 経路同期種別と、アドレス種別に応じして、
//! IPv4/IPv6の経路を、追加/削除する。
//!
//! @param [in]  ad             経路同期種別(RTSYNC_ROUTE_ADD or RTSYNC_ROUTE_DEL)
//! @param [in]  family         設定するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  route_info_p   経路情報(エントリー情報)
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_ctrl_route_entry_sync(int ad, int family, void *route_info_p)
{

    char *req;                              /* Request netlink message buffer    */
    struct nlmsghdr   *nlmsg;               /* Netlink message header address    */
    struct rtmsg      *rtm;                 /* Netlink Rtmsg address             */
    struct m46e_v4_route_info_t   *route_info;
    struct m46e_v6_route_info_t   *route_info6;

    int sock_fd;                             /* Netlink socket descriptor        */
    struct sockaddr_nl local;                /* Local netlink socket address     */
    uint32_t seq;                            /* Sequence number(for netlink msg) */
    int ret;                                 /* return value                     */
    int errcd;

    DEBUG_SYNC_LOG("m46e_ctrl_route_entry_sync start.\n");

    req = malloc(NETLINK_SNDBUF); /* 16kbyte */
    if( req == NULL ){
        m46e_logging(LOG_ERR, "snd buf malloc ng. errno=%d\n",errno);
        return RESULT_NG;
    }

    /* netlink message address set */
    nlmsg = (struct nlmsghdr*)req;
    rtm = (struct rtmsg*)(req + NLMSG_HDRLEN);

    /* ------------------------------------ */
    /* Netlink socket open                  */
    /* ------------------------------------ */
    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);

    if(ret != RESULT_OK){
        /* socket open error */
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return ret;
    }

    /* ------------------------------------ */
    /* Set netlink message                  */
    /* ------------------------------------ */
    /* struct nlmsghdr */
    memset(req, 0, NETLINK_SNDBUF);

    switch(ad){
       case 0:
            /* route entry add */
            nlmsg->nlmsg_type  = RTM_NEWROUTE;
            nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE;
            break;

       case 1:
            /* route entry del */
            nlmsg->nlmsg_type  = RTM_DELROUTE;
            nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
            break;

       default:
            /* Paramete error */
            m46e_logging(LOG_ERR, "Parameter error. ad=%d\n", ad);
            m46e_netlink_close(sock_fd);
            return RESULT_NG;
    }

    nlmsg->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlmsg->nlmsg_seq = seq;             /* sequence no */
    nlmsg->nlmsg_pid = 0;               /* To kernel   */

    /* struct rtmsg */
    rtm->rtm_family  = family;
    rtm->rtm_dst_len = 0;
    rtm->rtm_src_len = 0;
    rtm->rtm_tos     = 0;
    rtm->rtm_protocol= RTPROT_STATIC;
    rtm->rtm_scope   = RT_SCOPE_UNIVERSE;
    rtm->rtm_type    = RTN_UNICAST;
    rtm->rtm_flags   = RTM_F_NOTIFY;

    if (family == AF_INET) {
        route_info = (struct m46e_v4_route_info_t*)route_info_p;

        rtm->rtm_table   = RT_TABLE_MAIN;

        /* Dst address */
        m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_DST, &route_info->in_dst, 4);

        /* Gateway address */
        if (route_info->in_dst.s_addr !=0) {
            m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_GATEWAY, &route_info->in_gw, 4);
        }

        /* subnet mask */
        rtm->rtm_dst_len = route_info->mask;

        // device index
        m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_OIF, &route_info->out_if_index, 4);

        /* Src address */
        if (route_info->in_src.s_addr != 0) {
            m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_PREFSRC, &route_info->in_src, 4);
        }

        /* Metric */
        if (route_info->priority !=0) {
            m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_PRIORITY, &route_info->priority, 4);
        }
    }
    else {
        route_info6 = (struct m46e_v6_route_info_t*)route_info_p;

        //rtm->rtm_table   = 0;                   // カーネル
        rtm->rtm_table   = RT_TABLE_MAIN;

        if ((route_info6->in_dst.s6_addr32[0] != 0) ||
            (route_info6->in_dst.s6_addr32[1] != 0) ||
            (route_info6->in_dst.s6_addr32[2] != 0) ||
            (route_info6->in_dst.s6_addr32[3] != 0)) {
            m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_DST, &route_info6->in_dst, 16);
        }

        /* Gateway address */
        if ((route_info6->in_gw.s6_addr32[0] != 0) ||
            (route_info6->in_gw.s6_addr32[1] != 0) ||
            (route_info6->in_gw.s6_addr32[2] != 0) ||
            (route_info6->in_gw.s6_addr32[3] != 0)) {
            m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_GATEWAY, &route_info6->in_gw, 16);
        }

        /* subnet mask */
        rtm->rtm_dst_len = route_info6->mask;

        // device index
        m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_OIF, &route_info6->out_if_index, 4);

        /* Src address */
        if ((route_info6->in_src.s6_addr32[0] != 0) ||
            (route_info6->in_src.s6_addr32[1] != 0) ||
            (route_info6->in_src.s6_addr32[2] != 0) ||
            (route_info6->in_src.s6_addr32[3] != 0)) {
            m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_PREFSRC, &route_info6->in_src, 16);
        }

        /* Metric */
        if (route_info6->priority != 0) {
            m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_PRIORITY, &route_info6->priority, 4);
        }

    }

    /* ---------------------------------------------------- */
    /* Netlink message send                                 */
    /* ---------------------------------------------------- */
    ret = m46e_netlink_send(sock_fd, seq, nlmsg, &errcd);

    if(ret != RESULT_OK){
        /* Netlink message send error */
        m46e_logging(LOG_ERR, "Netlink message send error\n");
        m46e_netlink_close(sock_fd);
        return ret;
    }

    /* ---------------------------------------------------- */
    /* Netlink message ACK recieve                          */
    /* ---------------------------------------------------- */
    ret = m46e_netlink_recv(sock_fd, &local, seq, &errcd,
                 m46e_netlink_parse_ack, NULL);

    if(ret != RESULT_OK){
        /*Netlink message act recv already exist*/
        if ((errcd == EEXIST) || (errcd == ESRCH)) {
            m46e_logging(LOG_INFO, "Netlink message ack. %s(%d)\n", strerror(errcd), errcd);
        }
        /* Netlink message ack recv error */
        else {
            m46e_logging(LOG_ERR,"Netlink message ack recv error. %s(%d)\n", strerror(errcd), errcd);
        }
        m46e_netlink_close(sock_fd);
        return ret;
    }

    /* ---------------------------------------------------- */
    /* Netlink Socket close                                 */
    /* ---------------------------------------------------- */
    m46e_netlink_close(sock_fd);

    DEBUG_SYNC_LOG("m46e_ctrl_route_entry_sync end.\n");
    return RESULT_OK;

}

