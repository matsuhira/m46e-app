/******************************************************************************/
/* ファイル名 : m46eapp_tunnel.c                                              */
/* 機能概要   : トンネルクラス ソースファイル                                 */
/* 修正履歴   : 2011.12.20 T.Maeda 新規作成                                   */
/*              2012.07.24 T.Maeda Phase4向けに全面改版                       */
/*              2013.07.10 K.Nakamura 強制フラグメント機能 追加               */
/*              2013.09.13 K.Nakamura バグ修正                                */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/fcntl.h>

#include "m46eapp.h"
#include "m46eapp_tunnel.h"
#include "m46eapp_log.h"
#include "m46eapp_config.h"
#include "m46eapp_print_packet.h"
#include "m46eapp_util.h"
#include "m46eapp_statistics.h"
#include "m46eapp_pmtudisc.h"
#include "m46eapp_command.h"
#include "m46eapp_pr.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

//! 受信バッファのサイズ
#define TUNNEL_RECV_BUF_SIZE 65535

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static void tunnel_buffer_cleanup(void* buffer);
static void tunnel_ipv4_main_loop(struct m46e_handler_t* handler);
static void tunnel_ipv6_main_loop(struct m46e_handler_t* handler);
static void tunnel_forward_ipv4_packet(struct m46e_handler_t* handler, char* recv_buffer, ssize_t recv_len, m46e_device_t* recv_dev, m46e_device_t* send_dev);
static void tunnel_forward_ipv6_packet(struct m46e_handler_t* handler, char* recv_buffer, ssize_t recv_len, m46e_device_t* recv_dev, m46e_device_t* send_dev);
static void tunnel_send_fragment_packet(struct m46e_handler_t* hander, m46e_device_t* send_dev, struct ethhdr* p_ether, struct ip6_hdr* p_ip6, struct iphdr* p_ip4, const int pmtu_size);
static void tunnel_send_frag_need_error(struct m46e_handler_t* handler, struct iphdr* p_ip4, const uint16_t next_mtu);
static bool tunnel_check_icmp_error_send(const struct iphdr* p_ip4);

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク用 パケットカプセル化スレッド
//!
//! IPv4パケット受信のメインループを呼ぶ。
//!
//! @param [in] arg M46Eハンドラ
//!
//! @return NULL固定
///////////////////////////////////////////////////////////////////////////////
void* m46e_tunnel_stub_thread(void* arg)
{
    // ローカル変数宣言
    struct m46e_handler_t* handler;

    // 引数チェック
    if(arg == NULL){
        pthread_exit(NULL);
    }

    // ローカル変数初期化
    handler = (struct m46e_handler_t*)arg;

    // メインループ開始
    tunnel_ipv4_main_loop(handler);

    pthread_exit(NULL);

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク用 パケットデカプセル化スレッド
//!
//! IPv6パケット受信のメインループを呼ぶ。
//!
//! @param [in] arg M46Eハンドラ
//!
//! @return NULL固定
///////////////////////////////////////////////////////////////////////////////
void* m46e_tunnel_backbone_thread(void* arg)
{
    // ローカル変数宣言
    struct m46e_handler_t* handler;

    // 引数チェック
    if(arg == NULL){
        pthread_exit(NULL);
    }

    // ローカル変数初期化
    handler = (struct m46e_handler_t*)arg;

    // メインループ開始
    tunnel_ipv6_main_loop(handler);

    pthread_exit(NULL);

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 受信バッファ解放関数
//!
//! 引数で指定されたバッファを解放する。
//! スレッドの終了時に呼ばれる。
//!
//! @param [in] buffer    受信バッファ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void tunnel_buffer_cleanup(void* buffer)
{
    DEBUG_LOG("tunnel_buffer_cleanup\n");

    // 確保したメモリを解放
    free(buffer);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク メインループ関数
//!
//! Stubネットワークのメインループ。
//! 仮想デバイスからのパケット受信を待ち受けて、
//! パケット受信時にカプセル化の処理をおこなう。
//!
//! @param [in] handler   M46Eハンドラ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void tunnel_ipv4_main_loop(struct m46e_handler_t* handler)
{
    // ローカル変数宣言
    int     max_fd;
    fd_set  fds;
    char*   recv_buffer;
    ssize_t recv_len;
    m46e_device_t* ipv4_dev;
    m46e_device_t* ipv6_dev;

    // 引数チェック
    if(handler == NULL){
        return;
    }

    // 受信バッファ領域を確保
    recv_buffer = (char*)malloc(TUNNEL_RECV_BUF_SIZE);
    if(recv_buffer == NULL){
        m46e_logging(LOG_ERR, "receive buffer allocation failed\n");
        return;
    }

    // 後始末ハンドラ登録
    pthread_cleanup_push(tunnel_buffer_cleanup, (void*)recv_buffer);

    // トンネルデバイス
    ipv4_dev = &handler->conf->tunnel->ipv4;
    ipv6_dev = &handler->conf->tunnel->ipv6;

    // selector用のファイディスクリプタ設定
    // (待ち受けるディスクリプタの最大値+1)
    max_fd = -1;
    max_fd = max(max_fd, ipv4_dev->option.tunnel.fd);
    max_fd++;

    // ループ前に今溜まっているデータを全て吐き出す
    while(1){
        struct timeval t;
        FD_ZERO(&fds);
        FD_SET(ipv4_dev->option.tunnel.fd, &fds);

        // timevalを0に設定することで、即時に受信できるデータを待つ
        t.tv_sec  = 0;
        t.tv_usec = 0;

        // 受信待ち
        if(select(max_fd, &fds , NULL, NULL, &t) > 0){
            if(FD_ISSET(ipv4_dev->option.tunnel.fd, &fds)){
                recv_len = read(ipv4_dev->option.tunnel.fd, recv_buffer, TUNNEL_RECV_BUF_SIZE);
            }
        }
        else{
            // 即時に受信できるデータが無くなったのでループを抜ける
            break;
        }
    }

    m46e_logging(LOG_INFO, "IPv4 tunnel thread main loop start\n");

    while(1){
        // selectorの初期化
        FD_ZERO(&fds);
        FD_SET(ipv4_dev->option.tunnel.fd, &fds);

        // 受信待ち
        if(select(max_fd, &fds , NULL, NULL, NULL) < 0){
            if(errno == EINTR){
                // シグナル割込みの場合は処理継続
                DEBUG_LOG("signal receive. continue thread loop.");
                continue;
            }
            else{
                m46e_logging(LOG_ERR, "IPv4 tunnel main loop receive error : %s\n", strerror(errno));
                break;;
            }
        }

        // IPv4用TAPデバイスでデータ受信
        if(FD_ISSET(ipv4_dev->option.tunnel.fd, &fds)){
            if((recv_len=read(ipv4_dev->option.tunnel.fd, recv_buffer, TUNNEL_RECV_BUF_SIZE)) > 0){
                tunnel_forward_ipv4_packet(handler, recv_buffer, recv_len, ipv4_dev, ipv6_dev);
            }
            else{
                m46e_logging(LOG_ERR, "v4 recvfrom\n");
            }
        }
    }

    m46e_logging(LOG_INFO, "IPv4 tunnel thread main loop end\n");

    // 後始末
    pthread_cleanup_pop(1);
    
    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク メインループ関数
//!
//! Backboneネットワークのメインループ。
//! 仮想デバイスからのパケット受信を待ち受けて、
//! パケット受信時にデカプセル化の処理をおこなう。
//!
//! @param [in] handler   M46Eハンドラ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void tunnel_ipv6_main_loop(struct m46e_handler_t* handler)
{
    // ローカル変数宣言
    int     max_fd;
    fd_set  fds;
    char*   recv_buffer;
    ssize_t recv_len;
    m46e_device_t* ipv4_dev;
    m46e_device_t* ipv6_dev;

    // 引数チェック
    if(handler == NULL){
        return;
    }

    // 受信バッファ領域を確保
    recv_buffer = (char*)malloc(TUNNEL_RECV_BUF_SIZE);
    if(recv_buffer == NULL){
        m46e_logging(LOG_ERR, "receive buffer allocation failed\n");
        return;
    }

    // 後始末ハンドラ登録
    pthread_cleanup_push(tunnel_buffer_cleanup, (void*)recv_buffer);

    // トンネルデバイス
    ipv4_dev = &handler->conf->tunnel->ipv4;
    ipv6_dev = &handler->conf->tunnel->ipv6;

    // selector用のファイディスクリプタ設定
    // (待ち受けるディスクリプタの最大値+1)
    max_fd = -1;
    max_fd = max(max_fd, ipv6_dev->option.tunnel.fd);
    max_fd++;

    // ループ前に今溜まっているデータを全て吐き出す
    while(1){
        struct timeval t;
        FD_ZERO(&fds);
        FD_SET(ipv6_dev->option.tunnel.fd, &fds);

        // timevalを0に設定することで、即時に受信できるデータを待つ
        t.tv_sec  = 0;
        t.tv_usec = 0;

        // 受信待ち
        if(select(max_fd, &fds , NULL, NULL, &t) > 0){
            if(FD_ISSET(ipv6_dev->option.tunnel.fd, &fds)){
                recv_len = read(ipv6_dev->option.tunnel.fd, recv_buffer, TUNNEL_RECV_BUF_SIZE);
            }
        }
        else{
            // 即時に受信できるデータが無くなったのでループを抜ける
            break;
        }
    }

    m46e_logging(LOG_INFO, "IPv6 tunnel thread main loop start\n");

    while(1){
        // selectorの初期化
        FD_ZERO(&fds);
        FD_SET(ipv6_dev->option.tunnel.fd, &fds);

        // 受信待ち
        if(select(max_fd, &fds , NULL, NULL, NULL) < 0){
            if(errno == EINTR){
                // シグナル割込みの場合は処理継続
                DEBUG_LOG("signal receive. continue thread loop.");
                continue;
            }
            else{
                m46e_logging(LOG_ERR, "IPv6 tunnel main loop receive error : %s\n", strerror(errno));
                break;;
            }
        }

        // IPv6用TAPデバイスでデータ受信
        if(FD_ISSET(ipv6_dev->option.tunnel.fd, &fds)){
            if((recv_len=read(ipv6_dev->option.tunnel.fd, recv_buffer, TUNNEL_RECV_BUF_SIZE)) > 0){
                tunnel_forward_ipv6_packet(handler, recv_buffer, recv_len, ipv6_dev, ipv4_dev);
            }
            else{
                m46e_logging(LOG_ERR, "v6 recvfrom\n");
            }
        }
    }

    m46e_logging(LOG_INFO, "IPv6 tunnel thread main loop end\n");

    // 後始末
    pthread_cleanup_pop(1);
    
    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief IPv4パケット転送関数
//!
//! 受信したIPv4パケットをカプセル化してIPv6デバイスに転送する。
//!
//! @param [in,out] handler     M46Eハンドラ
//! @param [in]     recv_buffer 受信パケットデータ
//! @param [in]     recv_len    受信パケット長
//! @param [in]     recv_dev    パケットを受信したデバイス
//! @param [in]     send_dev    パケットを転送するデバイス
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void tunnel_forward_ipv4_packet(
    struct m46e_handler_t*   handler,
    char*                     recv_buffer,
    ssize_t                   recv_len,
    m46e_device_t*           recv_dev,
    m46e_device_t*           send_dev
)
{
    // ローカル変数宣言
    struct ethhdr*   p_ether;
    struct iphdr*    p_ip4;
    struct ip6_hdr*  p_ip6;
    u_int32_t        v4saddr;
    u_int32_t        v4daddr;
    u_int16_t        v4sport;
    u_int16_t        v4dport;
    struct in6_addr* v6addr_u;
    struct in6_addr* v6addr_m;
    struct in6_addr* v6addr_pr_src      = NULL;

    in_addr_t        v4dhostaddr;
    m46e_pr_entry_t* pr_entry;

    // ローカル変数初期化
    p_ether        = (struct ethhdr*)recv_buffer;
    p_ip4          = NULL;
    p_ip6          = NULL;
    v4saddr        = 0;
    v4daddr        = 0;
    v4sport        = 0;
    v4dport        = 0;
    v6addr_u         = NULL;
    v6addr_m         = NULL;
    v6addr_pr_src      = NULL;
    v4dhostaddr    = INADDR_NONE;

    // 統計情報
    m46e_inc_tunnel_v4_recieve(handler->stat_info);

    if(m46e_util_is_broadcast_mac(&p_ether->h_dest[0])){
        // ブロードキャストパケットは黙って破棄
        DEBUG_LOG("drop packet so that recv packet is broadcast\n");
        m46e_inc_tunnel_v4_err_broadcast(handler->stat_info);
        return;
    }

    if(ntohs(p_ether->h_proto)==ETH_P_IP){
        DEBUG_LOG("\n");
        DEBUG_LOG("recv IPv4 packet.\n");
        _D_(m46e_print_packet(recv_buffer);)

        // IPv4パケットの場合、IPv6でカプセル化して仮想デバイスにwrite
        // 送信先はルーティングテーブルにお任せ

        p_ip4 = (struct iphdr*)(recv_buffer + sizeof(struct ethhdr));

        // 送信先アドレスをホストバイトオーダーにして保持
        v4dhostaddr = ntohl(p_ip4->daddr);

        if(IN_MULTICAST(v4dhostaddr)){
            // マルチキャストパケット
            DEBUG_LOG("recv packet is multicast.\n");

            if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR){
                DEBUG_LOG("drop packet so that recv packet is multicast.\n");
                m46e_inc_tunnel_v4_err_pr_multi(handler->stat_info);
                return;
            }
            if(v4dhostaddr <= INADDR_MAX_LOCAL_GROUP){
                // リンクローカルのマルチキャストなので黙って破棄
                DEBUG_LOG("drop packet so that recv packet is link local multicast.\n");
                m46e_inc_tunnel_v4_err_linklocal_multi(handler->stat_info);
                return;
            }
        }
        else{
            // ユニキャストパケット
            DEBUG_LOG("recv packet is unicast.\n");
        }

        switch(handler->conf->general->tunnel_mode){
        case M46E_TUNNEL_MODE_AS:
            // ASモードの場合、フラグメントされたパケットは
            // すべて破棄する。
            // フラグメントされているかどうかの判定は
            //   ・Fragment offset = 0
            //   ・MF(More Fragments)ビット = OFF(0)
            // が成立した場合、フラグメントされていないと判断する。
            if((ntohs(p_ip4->frag_off) & (IP_MF | IP_OFFMASK)) != 0){
                // フラグメントパケットなので黙って破棄
                m46e_inc_tunnel_v4_err_as_fragment(handler->stat_info);
                DEBUG_LOG("drop packet so that recv packet is fragment and mode is AS.\n");
                return;
            }

            // ASモードの場合はポート番号を取得
            switch(p_ip4->protocol){
            case IPPROTO_TCP:
                v4sport = ((struct tcphdr*)(((char*)p_ip4) + (p_ip4->ihl * 4)))->source;
                v4dport = ((struct tcphdr*)(((char*)p_ip4) + (p_ip4->ihl * 4)))->dest;
                break;
            case IPPROTO_UDP:
                v4sport = ((struct udphdr*)(((char*)p_ip4) + (p_ip4->ihl * 4)))->source;
                v4dport = ((struct udphdr*)(((char*)p_ip4) + (p_ip4->ihl * 4)))->dest;
                break;
            default:
                // L4がTCP/UDP以外の場合は黙って破棄
                m46e_inc_tunnel_v4_err_as_not_support_proto(handler->stat_info);
                DEBUG_LOG("drop packet so that payload is not tcp/udp.\n");
                return;
            }
            // IPアドレスも取得するので、このまま継続(breakしない)

        case M46E_TUNNEL_MODE_NORMAL:
        case M46E_TUNNEL_MODE_PR:
            // IPアドレス取得
            v4saddr = p_ip4->saddr;
            v4daddr = p_ip4->daddr;

            break;

        default:
            // ありえないのでパケット破棄
            DEBUG_LOG("drop packet so that M46E mode is invalid(mode=%d)\n", handler->conf->general->tunnel_mode);
            // このルートに来ることは無いので統計は省略
            return;
        }

        // M46E prefixアドレスを取得
        if(IN_MULTICAST(v4dhostaddr)){
            v6addr_u = &handler->unicast_prefix;
            v6addr_m = &handler->multicast_prefix;
        }
        else{
            switch(handler->conf->general->tunnel_mode) {
            case M46E_TUNNEL_MODE_NORMAL:
            case M46E_TUNNEL_MODE_AS:
                v6addr_u = &handler->unicast_prefix;
                break;
            case M46E_TUNNEL_MODE_PR:
                pr_entry = m46e_pr_entry_search_stub(handler->pr_handler, (struct in_addr*)&v4daddr);
                if(pr_entry == NULL){
                    m46e_inc_tunnel_v4_err_pr_search_failure(handler->stat_info);
                    DEBUG_LOG("drop packet so that destination address is NOT in M46E-PR Table.\n");
                    return;
                }
                v6addr_u    = &pr_entry->pr_prefix_planeid;
                v6addr_pr_src = &handler->src_addr_unicast_prefix;

                break;
            default:
                // ありえないのでパケット破棄
                DEBUG_LOG("drop packet so that M46E mode is invalid(mode=%d)\n", handler->conf->general->tunnel_mode);
                // このルートに来ることは無いので統計は省略
                return;
            }
        }

        // IPv6ヘッダ構築
        struct ip6_hdr ip6_header;
        p_ip6 = &ip6_header;
        p_ip6->ip6_flow = 0;
        p_ip6->ip6_vfc  = 6 << 4;
        p_ip6->ip6_plen = p_ip4->tot_len;
        p_ip6->ip6_nxt  = IPPROTO_IPIP;
        p_ip6->ip6_hops = 0x80;
        switch(handler->conf->general->tunnel_mode){
        case M46E_TUNNEL_MODE_NORMAL: // 通常モード
        case M46E_TUNNEL_MODE_PR:     // PRモード
            if(IN_MULTICAST(v4dhostaddr)){
                p_ip6->ip6_src.s6_addr32[0] = v6addr_u->s6_addr32[0];
                p_ip6->ip6_src.s6_addr32[1] = v6addr_u->s6_addr32[1];
                p_ip6->ip6_src.s6_addr32[2] = v6addr_u->s6_addr32[2];
            p_ip6->ip6_src.s6_addr32[3] = v4saddr;
                p_ip6->ip6_dst.s6_addr32[0] = v6addr_m->s6_addr32[0];
                p_ip6->ip6_dst.s6_addr32[1] = v6addr_m->s6_addr32[1];
                p_ip6->ip6_dst.s6_addr32[2] = v6addr_m->s6_addr32[2];
            p_ip6->ip6_dst.s6_addr32[3] = v4daddr;
            }
            else {
if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_NORMAL){
                p_ip6->ip6_src.s6_addr32[0] = v6addr_u->s6_addr32[0];
                p_ip6->ip6_src.s6_addr32[1] = v6addr_u->s6_addr32[1];
                p_ip6->ip6_src.s6_addr32[2] = v6addr_u->s6_addr32[2];
                p_ip6->ip6_src.s6_addr32[3] = v4saddr;
}else{//handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR
                p_ip6->ip6_src.s6_addr32[0] = v6addr_pr_src->s6_addr32[0];
                p_ip6->ip6_src.s6_addr32[1] = v6addr_pr_src->s6_addr32[1];
                p_ip6->ip6_src.s6_addr32[2] = v6addr_pr_src->s6_addr32[2];
                p_ip6->ip6_src.s6_addr32[3] = v4saddr;
}
                p_ip6->ip6_dst.s6_addr32[0] = v6addr_u->s6_addr32[0];
                p_ip6->ip6_dst.s6_addr32[1] = v6addr_u->s6_addr32[1];
                p_ip6->ip6_dst.s6_addr32[2] = v6addr_u->s6_addr32[2];
                p_ip6->ip6_dst.s6_addr32[3] = v4daddr;
            }
            break;

        case M46E_TUNNEL_MODE_AS: // ASモード
            if(IN_MULTICAST(v4dhostaddr)){
                p_ip6->ip6_src.s6_addr16[0] = v6addr_u->s6_addr16[0];
                p_ip6->ip6_src.s6_addr16[1] = v6addr_u->s6_addr16[1];
                p_ip6->ip6_src.s6_addr16[2] = v6addr_u->s6_addr16[2];
                p_ip6->ip6_src.s6_addr16[3] = v6addr_u->s6_addr16[3];
                p_ip6->ip6_src.s6_addr16[4] = v6addr_u->s6_addr16[4];
            memcpy(&p_ip6->ip6_src.s6_addr16[5], &v4saddr, sizeof(v4saddr));
            p_ip6->ip6_src.s6_addr16[7] = v4sport;
                p_ip6->ip6_dst.s6_addr16[0] = v6addr_m->s6_addr16[0];
                p_ip6->ip6_dst.s6_addr16[1] = v6addr_m->s6_addr16[1];
                p_ip6->ip6_dst.s6_addr16[2] = v6addr_m->s6_addr16[2];
                p_ip6->ip6_dst.s6_addr16[3] = v6addr_m->s6_addr16[3];
                p_ip6->ip6_dst.s6_addr16[4] = v6addr_m->s6_addr16[4];
            memcpy(&p_ip6->ip6_dst.s6_addr16[5], &v4daddr, sizeof(v4daddr));
            p_ip6->ip6_dst.s6_addr16[7] = v4dport;
              }
              else {
                p_ip6->ip6_src.s6_addr16[0] = v6addr_u->s6_addr16[0];
                p_ip6->ip6_src.s6_addr16[1] = v6addr_u->s6_addr16[1];
                p_ip6->ip6_src.s6_addr16[2] = v6addr_u->s6_addr16[2];
                p_ip6->ip6_src.s6_addr16[3] = v6addr_u->s6_addr16[3];
                p_ip6->ip6_src.s6_addr16[4] = v6addr_u->s6_addr16[4];
                memcpy(&p_ip6->ip6_src.s6_addr16[5], &v4saddr, sizeof(v4saddr));
                p_ip6->ip6_src.s6_addr16[7] = v4sport;
                p_ip6->ip6_dst.s6_addr16[0] = v6addr_u->s6_addr16[0];
                p_ip6->ip6_dst.s6_addr16[1] = v6addr_u->s6_addr16[1];
                p_ip6->ip6_dst.s6_addr16[2] = v6addr_u->s6_addr16[2];
                p_ip6->ip6_dst.s6_addr16[3] = v6addr_u->s6_addr16[3];
                p_ip6->ip6_dst.s6_addr16[4] = v6addr_u->s6_addr16[4];
                memcpy(&p_ip6->ip6_dst.s6_addr16[5], &v4daddr, sizeof(v4daddr));
                p_ip6->ip6_dst.s6_addr16[7] = v4dport;
              }
            break;

        default:
            // ありえないのでパケット破棄
            DEBUG_LOG("drop packet so that M46E mode is invalid(mode=%d)\n", handler->conf->general->tunnel_mode);
            // このルートに来ることは無いので統計は省略
            return;
        }

        // etherフレームのプロトコルをIPv6に書き換え
        p_ether->h_proto = htons(ETH_P_IPV6);
        // etherフレームのsrcをIPv4のMACに書き換え
        memcpy(p_ether->h_source, recv_dev->hwaddr, ETH_ALEN);

        if(IN_MULTICAST(v4dhostaddr)){
            m46e_inc_tunnel_v4_recv_multicast(handler->stat_info);
            // etherフレームのdstをマルチキャストのMACに書き換え
            p_ether->h_dest[0] = 0x33;
            p_ether->h_dest[1] = 0x33;
            p_ether->h_dest[2] = p_ip6->ip6_dst.s6_addr[12];
            p_ether->h_dest[3] = p_ip6->ip6_dst.s6_addr[13];
            p_ether->h_dest[4] = p_ip6->ip6_dst.s6_addr[14];
            p_ether->h_dest[5] = p_ip6->ip6_dst.s6_addr[15];
        }
        else{
            m46e_inc_tunnel_v4_recv_unicast(handler->stat_info);
            // etherフレームのdstをIPv6のMACに書き換え
            memcpy(p_ether->h_dest, send_dev->hwaddr, ETH_ALEN);
        }

        // 送信先IPv6アドレスのPMTUを取得
        int pmtu_size = m46e_path_mtu_get(handler->pmtud_handler, &p_ip6->ip6_dst);

        if(pmtu_size < 0){
            // 経路が見つからない(Network Unreachableを返すならここで)
        }
        else if(pmtu_size < (ntohs(p_ip6->ip6_plen) + sizeof(struct ip6_hdr))){
            // 送信しようとしているIPv6パケットのサイズがPMTUのサイズを越える場合
            // IPv4パケットを分割して再カプセル化した上で送信する。

            // 元のIPv4パケットのフラグメントビットを取得
            uint16_t frag_df = ntohs(p_ip4->frag_off) & IP_DF;

            // DFビットのチェック
            if(frag_df == 0){
                // DFビットが立っていないので、フラグメントして送信する
                DEBUG_LOG("df=0 fragment.\n");
                tunnel_send_fragment_packet(handler, send_dev, p_ether, p_ip6, p_ip4, pmtu_size);
            }
           else{
               if(handler->conf->general->force_fragment){
                  // 強制フラグメント機能が有効の場合
                  // 元のIPv4パケットのDFビットを落とす
                  p_ip4->frag_off = htons(~IP_DF & ntohs(p_ip4->frag_off));
                  DEBUG_LOG("df=0 fragment.\n");
                  tunnel_send_fragment_packet(handler, send_dev, p_ether, p_ip6, p_ip4, pmtu_size);
                }
               else{
                  // 強制フラグメント機能が無効の場合
                  // DFビットが立っているのでFragment NeededのICMPエラーを返す
                  // 通知するMTUは(PMTU-IPv6ヘッダ長)とする(カプセル化するので)
                  DEBUG_LOG("df=1 send icmp err.\n");
                  tunnel_send_frag_need_error(handler, p_ip4, pmtu_size-sizeof(struct ip6_hdr));
               }
           }
        }
        else{
            struct iovec iov[3];
            iov[0].iov_base = p_ether;
            iov[0].iov_len  = sizeof(struct ethhdr);
            iov[1].iov_base = p_ip6;
            iov[1].iov_len  = sizeof(struct ip6_hdr);
            iov[2].iov_base = p_ip4;
            iov[2].iov_len  = ntohs(p_ip4->tot_len);

            ssize_t send_len;
            if((send_len=writev(send_dev->option.tunnel.fd, iov, 3)) < 0){
                m46e_logging(LOG_ERR, "fail to send IPv6 packet\n");
                m46e_inc_tunnel_v4_send_err(handler->stat_info);
            }
            else{
                DEBUG_LOG("forward %d bytes to IPv6\n", send_len);
                m46e_inc_tunnel_v4_send_success(handler->stat_info);
            }
        }
    }
    else{
        // IPv4以外のパケットは、黙って破棄。
        DEBUG_LOG("Drop IPv4 Packet Ether Type : 0x%x\n", ntohs(p_ether->h_proto));
        m46e_inc_tunnel_v4_err_other_proto(handler->stat_info);
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief IPv6パケット転送関数
//!
//! 受信したIPv6パケットをデカプセル化してIPv4デバイスに転送する。
//!
//! @param [in,out] handler     M46Eハンドラ
//! @param [in]     recv_buffer 受信パケットデータ
//! @param [in]     recv_len    受信パケット長
//! @param [in]     recv_dev    パケットを受信したデバイス
//! @param [in]     send_dev    パケットを転送するデバイス
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void tunnel_forward_ipv6_packet(
    struct m46e_handler_t*   handler,
    char*                     recv_buffer,
    ssize_t                   recv_len,
    m46e_device_t*           recv_dev,
    m46e_device_t*           send_dev
)
{
    // ローカル変数宣言
    struct ethhdr*  p_ether;
    struct iphdr*   p_ip4;
    struct ip6_hdr* p_ip6;
    struct in_addr  v4dinaddr;
    in_addr_t       v4dhostaddr;
    struct ip6_hdr*   p_orig_hdr;
    struct icmp6_hdr* p_icmp6;

    // ローカル変数初期化
    p_ether        = (struct ethhdr*)recv_buffer;
    p_ip4          = NULL;
    p_ip6          = NULL;
    p_icmp6        = NULL;
    memset(&v4dinaddr, 0, sizeof(v4dinaddr));
    v4dhostaddr    = INADDR_NONE;

    // 統計情報
    m46e_inc_tunnel_v6_recieve(handler->stat_info);

    if(m46e_util_is_broadcast_mac(&p_ether->h_dest[0])){
        // ブロードキャストパケットは黙って破棄
        DEBUG_LOG("drop packet so that recv packet is broadcast\n");
        m46e_inc_tunnel_v6_err_broadcast(handler->stat_info);
        return;
    }

    if(ntohs(p_ether->h_proto)==ETH_P_IPV6){
        DEBUG_LOG("\n");
        DEBUG_LOG("recv IPv6 packet.\n");
        _D_(m46e_print_packet(recv_buffer);)

        // IPv6パケットの場合、デカプセル化してIPv4用仮想デバイスにwrite
        // 送信先はルーティングテーブルにお任せ

        p_ip6 = (struct ip6_hdr*)(recv_buffer + sizeof(struct ethhdr));

        if(p_ip6->ip6_nxt == IPPROTO_IPIP){

            p_ip4 = (struct iphdr*)(p_ip6+1);

            // 送信先アドレスを保持
            v4dhostaddr      = ntohl(p_ip4->daddr);
            v4dinaddr.s_addr = p_ip4->daddr;

            if(IN_MULTICAST(v4dhostaddr)){
                // マルチキャストパケット
                DEBUG_LOG("recv packet is multicast.\n");

                if(v4dhostaddr <= INADDR_MAX_LOCAL_GROUP){
                    // リンクローカルのマルチキャストなので黙って破棄
                    DEBUG_LOG("drop packet so that recv packet is link local multicast.\n");
                    m46e_inc_tunnel_v6_err_linklocal_multi(handler->stat_info);
                    return;
                }
            }
            else{
                // ユニキャストパケット
                DEBUG_LOG("recv packet is unicast.\n");
            }

            if(p_ip4->ttl == 1){
                // TTLが1のパケットは黙って破棄(これ以上転送できない為)
                DEBUG_LOG("drop packet so that ttl is 1.\n");
                m46e_inc_tunnel_v6_err_ttl(handler->stat_info);
                return;
            }

            // etherフレームのプロトコルをIPv4に書き換え
            p_ether->h_proto = htons(ETH_P_IP);
            // etherフレームのsrcをIPv6のMACに書き換え
            memcpy(p_ether->h_source, recv_dev->hwaddr, ETH_ALEN);
            // etherフレームのdstを書き換え
            if(IN_MULTICAST(v4dhostaddr)){
                m46e_inc_tunnel_v6_recv_multicast(handler->stat_info);
                // etherフレームのdstをマルチキャストのMACに書き換え
                ETHER_MAP_IP_MULTICAST(&v4dinaddr, p_ether->h_dest);
            }
            else{
                // etherフレームのdstをIPv4のMACに書き換え
                memcpy(p_ether->h_dest, send_dev->hwaddr, ETH_ALEN);
                m46e_inc_tunnel_v6_recv_unicast(handler->stat_info);
            }

            struct iovec iov[2];
            iov[0].iov_base = p_ether;
            iov[0].iov_len  = sizeof(struct ethhdr);
            iov[1].iov_base = (p_ip6 + 1);
            iov[1].iov_len  = ntohs(p_ip6->ip6_plen);

            ssize_t send_len;
            if((send_len=writev(send_dev->option.tunnel.fd, iov, 2)) < 0){
                m46e_logging(LOG_ERR, "fail to send IPv4 packet\n");
                m46e_inc_tunnel_v6_send_v4_err(handler->stat_info);
            }
            else{
                DEBUG_LOG("forward %d bytes to IPv4\n", send_len);
                m46e_inc_tunnel_v6_send_v4_success(handler->stat_info);
            }
        }
        // ICMPV6パケットの場合
        else if(p_ip6->ip6_nxt == IPPROTO_ICMPV6){
            
            p_icmp6 = (struct icmp6_hdr*)(p_ip6+1);

            // ICMP6_PACKET_TOO_BIGの場合
            if(p_icmp6->icmp6_type == ICMP6_PACKET_TOO_BIG){
                // Path MTU Discovery処理を実施
                p_orig_hdr = (struct ip6_hdr *) (p_icmp6 + 1);
                // socketでstub側に通知する
                m46e_command_t command;
                command.code = M46E_PACKET_TOO_BIG;
                command.req.too_big.dst_addr = p_orig_hdr->ip6_dst;
                command.req.too_big.mtu      = ntohl(p_icmp6->icmp6_mtu);
                int ret = m46e_command_send_request(handler, &command);
                if(ret < 0){
                    DEBUG_LOG("send error : %s\n", strerror(-ret));
                }

                // 統計情報取得 ICMP err(TooBig)受信
                DEBUG_LOG("receive icmpv6 packet too big.\n");
                m46e_inc_icmp_pkt_toobig_recieve(handler->stat_info);
            }

            // ICMPv6は、次ヘッダのエラーとしてカウント
            DEBUG_LOG("drop packet so that recv packet is not ipip. (icmp packet)\n");
            m46e_inc_tunnel_v6_err_nxthdr_count(handler->stat_info);
        }
        else{
            // 次ヘッダがIPIP以外(カプセル化されていないパケット)の場合、黙って破棄。
            DEBUG_LOG("drop packet so that recv packet is not ipip.\n");
            m46e_inc_tunnel_v6_err_nxthdr_count(handler->stat_info);
        }
    }
    else{
        // IPv6以外のパケットは、黙って破棄。
        DEBUG_LOG("Drop IPv6 Packet Ether Type : %d\n", ntohs(p_ether->h_proto));
        m46e_inc_tunnel_v6_err_other_proto(handler->stat_info);
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief IPv4パケットフラグメント関数
//!
//! 受信したIPv4パケットをフラグメント化して、IPv6にカプセル化して転送する。
//!
//! @param [in]     handler     M46Eハンドラ
//! @param [in]     send_dev    パケットを転送するデバイス
//! @param [in]     p_ether     受信したIPv4パケットを元に構築したEtherヘッダ
//! @param [in]     p_ip6       受信したIPv4パケットを元に構築したIPv6ヘッダ
//! @param [in]     p_ip4       受信したIPv4パケット
//! @param [in]     pmtu_size   IPv6送信先のPath MTU長
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void tunnel_send_fragment_packet(
    struct m46e_handler_t*  handler,
    m46e_device_t*          send_dev,
    struct ethhdr*           p_ether,
    struct ip6_hdr*          p_ip6,
    struct iphdr*            p_ip4,
    const int                pmtu_size
)
{
    // ローカル変数宣言
    struct iovec iov[4];

    // 引数チェック
    if((send_dev == NULL) || (handler == NULL)){
        return;
    }

    // Etherヘッダ
    iov[0].iov_base = p_ether;
    iov[0].iov_len  = sizeof(struct ethhdr);
    // IPv6ヘッダ
    iov[1].iov_base = p_ip6;
    iov[1].iov_len  = sizeof(struct ip6_hdr);
    // IPv4ヘッダ
    iov[2].iov_base = p_ip4;
    iov[2].iov_len  = p_ip4->ihl * 4;

    // 元のIPv4パケットのMFビットを取得
    uint16_t frag_mf = ntohs(p_ip4->frag_off) & IP_MF;

    // フラグメントパケットのペイロードは8byte単位にすると決まっているので
    // (pmtu-IPv6ヘッダ-IPv4ヘッダ)を8で割り切れる最大数を計算する
    int max_payload_len = ((pmtu_size - iov[1].iov_len - iov[2].iov_len) & 0xfffffff8);

    // IPv4ペイロードのサイズと先頭ポインタを設定
    int      remain_data_len = ntohs(p_ip4->tot_len) - iov[2].iov_len;
    char*    data_ptr        = ((char*)iov[2].iov_base) + iov[2].iov_len;
    uint16_t payload_offset  = 0;

    while(remain_data_len > 0){
        // 送信するペイロードのlength計算
        int data_len = min(max_payload_len, remain_data_len);

        // IPv4ペイロードの先頭アドレスを設定
        iov[3].iov_base = data_ptr + payload_offset;
        iov[3].iov_len  = data_len;

        // IPv4ヘッダを書き換え
        // (フラグメントビットの設定とパケットサイズ、チェックサムの変更)
        p_ip4->tot_len = htons(iov[2].iov_len + iov[3].iov_len);
        if(data_len < remain_data_len){
            // 後続データがある場合は、MFビットを立てる
            p_ip4->frag_off = htons(IP_MF  | (payload_offset >> 3));
        }
        else{
            // 後続データがない場合は、元のパケットのMFビットを継承
            p_ip4->frag_off = htons(frag_mf | (payload_offset >> 3));
        }
        p_ip4->check = 0;
        // チェックサムの再計算
        p_ip4->check = m46e_util_checksum((unsigned short*)p_ip4, iov[2].iov_len);

        // IPv6ヘッダのペイロード長を変更
        p_ip6->ip6_plen = p_ip4->tot_len;

        // 分割したパケットを送信
        ssize_t send_len;
        if((send_len=writev(send_dev->option.tunnel.fd, iov, 4)) < 0){
            m46e_logging(LOG_ERR, "fail to send IPv6 packet(fragment) :");
            m46e_inc_tunnel_v4_send_fragment_err(handler->stat_info);
            return;
        }
        else{
            DEBUG_LOG("forward %d bytes to IPv6(fragment)\n", send_len);
            m46e_inc_tunnel_v4_send_fragment_success(handler->stat_info);
        }

        // 残りペイロードを減算
        remain_data_len -= data_len;

        // ペイロードのオフセットを加算
        payload_offset += data_len;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Fragment NeededのICMPパケット送信関数
//!
//! Fragment NeededのICMPパケットを送信する。<br/>
//! ICMPヘッダのフォーマットは以下の通り。<br/>
//! @code
//!    0                   1                   2                   3   
//!    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 
//!   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//!   |   Type = 3    |   Code = 4    |           Checksum            |
//!   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//!   |           unused = 0          |         Next-Hop MTU          |
//!   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//!   |      Internet Header + 64 bits of Original Datagram Data      |
//!   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//! @endcode
//!
//!
//! @param [in]  handler       M46Eハンドラ
//! @param [in]  original_ip4  Fragment Needed送信のトリガとなったIPv4パケット
//! @param [in]  next_mtu      Nexthop MTUの値(IPv6 PMTUからIPv6ヘッダ長を引いた値)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void tunnel_send_frag_need_error(
    struct m46e_handler_t* handler,
    struct iphdr*           original_ip4,
    const uint16_t          next_mtu
)
{
    // ICMPエラーを返していいパケットかどうかチェック
    if(!tunnel_check_icmp_error_send(original_ip4)){
        DEBUG_LOG("Cannnot send Fragment Needed packat\n");
        return;
    }

    // 引数チェック
    if(original_ip4 == NULL){
        m46e_logging(LOG_WARNING, "fail to send Fragment Needed packet : parameter error\n");
        return;
    }

    int fd = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        m46e_logging(LOG_WARNING, "fail to send Fragment Needed packet : %s\n", strerror(errno));
        return;
    }

    // ICMPヘッダ設定
    struct icmphdr icmp_header;
    memset(&icmp_header, 0, sizeof(struct icmphdr));
    icmp_header.type         = ICMP_DEST_UNREACH;
    icmp_header.code         = ICMP_FRAG_NEEDED;
    icmp_header.checksum     = 0;
    icmp_header.un.frag.mtu  = htons(next_mtu);

    // ICMPチェックサムの計算
    struct iovec iov[2];
    iov[0].iov_base = &icmp_header;
    iov[0].iov_len  = sizeof(struct icmphdr);
    iov[1].iov_base = original_ip4;
    iov[1].iov_len  = (original_ip4->ihl*4) + 8;
    icmp_header.checksum = m46e_util_checksumv(iov, 2);

    // 送信先アドレス設定
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(struct sockaddr_in));
    sin.sin_family      = PF_INET;
    sin.sin_addr.s_addr = original_ip4->saddr;
    sin.sin_port        = 0;

    // 送信メッセージ生成
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name       = &sin;
    msg.msg_namelen    = sizeof(sin);
    msg.msg_iov        = iov;
    msg.msg_iovlen     = 2;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;

    // パケット送信
    ssize_t send_len;
    if((send_len=sendmsg(fd, &msg, 0)) < 0){
        m46e_logging(LOG_ERR, "fail to send Fragment Needed packet : %s\n", strerror(errno));
        m46e_inc_icmp_frag_needed_send_err(handler->stat_info);
    }
    else{
        DEBUG_LOG("sent Fragment Needed packet\n");
        m46e_inc_icmp_frag_needed_send_success(handler->stat_info);
    }
    close(fd);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ICMPエラーパケット送信可否判定
//!
//! ICMPエラーを返信可能なパケットかどうかをチェックする。<br/>
//! 以下の条件のいずれかが成立した場合はICMPエラーは返信不可。<br/>
//!  ・フラグメントされたパケットの先頭以外のパケット <br/>
//!  ・マルチキャスト・ブロードキャストアドレス宛のパケット <br/>
//!  ・Redirect以外のICMPエラーパケット
//!
//! @param [in]  p_ip4 ICMPエラー送信のトリガとなったIPv4パケット
//!
//! @retval true   ICMPエラーパケット返信可
//! @retval false  ICMPエラーパケット返信不可
///////////////////////////////////////////////////////////////////////////////
static bool tunnel_check_icmp_error_send(const struct iphdr* p_ip4)
{
    // 引数チェック
    if(p_ip4 == NULL){
        return false;
    }

    // フラグメントの先頭パケット以外にはICMPエラーは返さない
    if((ntohs(p_ip4->frag_off) & IP_OFFMASK) != 0){
        return false;
    }

    // マルチキャスト、ブロードキャストのパケットにはICMPエラーは返さない
    if(IN_MULTICAST(ntohl(p_ip4->daddr)) || (p_ip4->daddr == INADDR_BROADCAST)){
        return false;
    }

    if((p_ip4->protocol == IPPROTO_ICMP)){
        struct icmphdr* icmp_header = (struct icmphdr*)(((char*)p_ip4) + (p_ip4->ihl * 4));
        // リダイレクト以外のICMPエラーにはICMPエラーは返さない
        if((icmp_header->type != ICMP_REDIRECT) && !ICMP_INFOTYPE(icmp_header->type)){
            return false;
        }
    }

    return true;
}
