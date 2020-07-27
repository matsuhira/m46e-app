/******************************************************************************/
/* ファイル名 : m46eapp_statistics.c                                          */
/* 機能概要   : 統計情報管理 ソースファイル                                   */
/* 修正履歴   : 2011.12.20 M.Nagano 新規作成                                  */
/*              2012.07.23 T.Maeda Phase4向けに全面改版                       */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2013.11.17 H.Koganemaru 統計情報出力イメージ修正              */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/shm.h>
#include <syslog.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>

#include "m46eapp_statistics.h"
#include "m46eapp_log.h"

///////////////////////////////////////////////////////////////////////////////
//! @brief 統計情報領域作成関数
//!
//! 共有メモリに統計情報用領域を確保する。
//!
//! @param [in] key_path 共有メモリ作成のキー。
//!             設定ファイルの絶対パス名を使用する。
//!
//! @return 作成した統計情報領域のポインタ
///////////////////////////////////////////////////////////////////////////////
m46e_statistics_t*  m46e_initial_statistics(const char* key_path)
{
    int shm_id = 0;

    // 統計情報が作成されていない場合に、新規に共有メモリに作成する
    shm_id = shmget(ftok(key_path, 'a'), sizeof(m46e_statistics_t), IPC_CREAT);
    if (shm_id == -1) {
        m46e_logging(LOG_ERR, "shared memory allocation faulure for statistics : %s\n", strerror(errno));
        return NULL;
    }

    m46e_statistics_t* statistics_info = (m46e_statistics_t*)shmat(shm_id, NULL, 0);
    if (statistics_info == (void *)-1) {
        m46e_logging(LOG_ERR, "shared memory attach failure for statistics : %s\n", strerror(errno));
        return NULL;
    }

    memset(statistics_info, 0, sizeof(m46e_statistics_t));

    statistics_info->shm_id = shm_id;

    m46e_logging(LOG_INFO, "shared memory ID=%d \n", shm_id);

    return statistics_info;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 統計情報領域解放関数
//!
//! 共有メモリに確保している統計情報用領域を解放する。
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void  m46e_finish_statistics(m46e_statistics_t* statistics_info)
{
    int ret;

    // 共有メモリ破棄
    ret = shmctl(statistics_info->shm_id, IPC_RMID, NULL);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to destruct shared memory : %s\n", strerror(errno));
    }

    ret = shmdt(statistics_info);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to detach shared memory : %s\n", strerror(errno));
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 受信パケット合計数取得関数
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//!
//! @return 受信パケット合計数
///////////////////////////////////////////////////////////////////////////////
static inline uint32_t statistics_get_total_recv(m46e_statistics_t* statistics_info)
{
    uint32_t result = 0;

    result += statistics_info->tunnel_v4_recieve_count;
    result += statistics_info->tunnel_v6_recieve_count;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 送信パケット合計数取得関数
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//!
//! @return 送信パケット合計数
///////////////////////////////////////////////////////////////////////////////
static inline uint32_t statistics_get_total_send(m46e_statistics_t* statistics_info)
{
    uint32_t result = 0;

    result += statistics_info->tunnel_v4_send_count;
    result += statistics_info->tunnel_v6_send_count;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ドロップパケット合計数取得関数
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//!
//! @return ドロップパケット合計数
///////////////////////////////////////////////////////////////////////////////
static inline uint32_t statistics_get_total_drop(m46e_statistics_t* statistics_info)
{
    uint32_t result = 0;

    result += statistics_info->tunnel_v4_err_broadcast_count;
    result += statistics_info->tunnel_v4_err_other_proto_count;
    result += statistics_info->tunnel_v4_err_pr_search_failure_count;
    result += statistics_info->tunnel_v4_err_pr_multi_count;
    result += statistics_info->tunnel_v4_err_linklocal_multi_count;
    result += statistics_info->tunnel_v4_err_as_fragment_count;
    result += statistics_info->tunnel_v4_err_as_not_support_proto_count;
    result += statistics_info->tunnel_v6_err_broadcast_count;
    result += statistics_info->tunnel_v6_err_ttl_count;
    result += statistics_info->tunnel_v6_err_other_proto_count;
    result += statistics_info->tunnel_v6_err_linklocal_multi_count;
    result += statistics_info->tunnel_v6_err_nxthdr_count;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief エラーパケット合計数取得関数
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//!
//! @return エラーパケット合計数
///////////////////////////////////////////////////////////////////////////////
static inline uint32_t statistics_get_total_error(m46e_statistics_t* statistics_info)
{
    uint32_t result = 0;

    result += statistics_info->tunnel_v4_send_v6_err_count;
    result += statistics_info->tunnel_v4_send_fragment_err_count;
    result += statistics_info->tunnel_v6_send_v4_err_count;
    result += statistics_info->icmp_fragneeded_send_err_count;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 統計情報出力関数(M46E 通常モード)
//!
//! 統計情報を引数で指定されたディスクリプタへ出力する。
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//! @param [in] fd              統計情報出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_printf_statistics_info_normal(m46e_statistics_t* statistics_info, int fd)
{
    // 統計情報をファイルへ出力する
    dprintf(fd, "【M46E】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     total recieve count             : %d\n", statistics_get_total_recv(statistics_info));
    dprintf(fd, "     total send count                : %d\n", statistics_get_total_send(statistics_info));
    dprintf(fd, "     total drop count                : %d\n", statistics_get_total_drop(statistics_info));
    dprintf(fd, "     total error count               : %d\n", statistics_get_total_error(statistics_info));
    dprintf(fd, "\n");
    dprintf(fd, "\n");
    dprintf(fd, "【TUNNEL IPV4】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count                   : %d \n", statistics_info->tunnel_v4_recieve_count);
    dprintf(fd, "       unicast(forward)              : %d \n", statistics_info->tunnel_v4_recv_unicast_count);
    dprintf(fd, "       multicast(forward)            : %d \n", statistics_info->tunnel_v4_recv_multicast_count);
    dprintf(fd, "       broadcast(drop)               : %d \n", statistics_info->tunnel_v4_err_broadcast_count);
    dprintf(fd, "       not IPv4 protocol(drop)       : %d \n", statistics_info->tunnel_v4_err_other_proto_count);
    dprintf(fd, "       link local multicast(drop)    : %d \n", statistics_info->tunnel_v4_err_linklocal_multi_count);
    dprintf(fd, "     send count                      : %d \n", statistics_info->tunnel_v4_send_count);
    dprintf(fd, "       send success                  : %d \n", statistics_info->tunnel_v4_send_v6_success_count);
    dprintf(fd, "       send error                    : %d \n", statistics_info->tunnel_v4_send_v6_err_count);
    dprintf(fd, "       send(fragment) success        : %d \n", statistics_info->tunnel_v4_send_fragment_success_count);
    dprintf(fd, "       send(fragment) error          : %d \n", statistics_info->tunnel_v4_send_fragment_err_count);
    dprintf(fd, "\n");
    dprintf(fd, "\n");
    dprintf(fd, "【TUNNEL IPV6】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count                   : %d \n", statistics_info->tunnel_v6_recieve_count);
    dprintf(fd, "       encap unicast(forward)        : %d \n", statistics_info->tunnel_v6_recv_unicast_count);
    dprintf(fd, "       encap multicast(forward)      : %d \n", statistics_info->tunnel_v6_recv_multicast_count);
    dprintf(fd, "       broadcast(drop)               : %d \n", statistics_info->tunnel_v6_err_broadcast_count);
    dprintf(fd, "       not IPv6 protocol(drop)       : %d \n", statistics_info->tunnel_v6_err_other_proto_count);
    dprintf(fd, "       ttl over(drop)                : %d \n", statistics_info->tunnel_v6_err_ttl_count);
    dprintf(fd, "       link local multicast(drop)    : %d \n", statistics_info->tunnel_v6_err_linklocal_multi_count);
    dprintf(fd, "       invalid next header(drop)     : %d \n", statistics_info->tunnel_v6_err_nxthdr_count);
    dprintf(fd, "     send count                      : %d \n", statistics_info->tunnel_v6_send_count);
    dprintf(fd, "       send success                  : %d \n", statistics_info->tunnel_v6_send_v4_success_count);
    dprintf(fd, "       send error                    : %d \n", statistics_info->tunnel_v6_send_v4_err_count);
    dprintf(fd, "\n");
    dprintf(fd, "【ICMP】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count\n");
    dprintf(fd, "       IPv6 icmp packet too big      : %d \n", statistics_info->icmp_pkt_toobig_recv_count);
    dprintf(fd, "     send count\n");
    dprintf(fd, "       IPv4 icmp fragment needed     : %d \n", statistics_info->icmp_fragneeded_send_count);
    dprintf(fd, "         send success                : %d \n", statistics_info->icmp_fragneeded_send_success_count);
    dprintf(fd, "         send error                  : %d \n", statistics_info->icmp_fragneeded_send_err_count);
    dprintf(fd, "\n");

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 統計情報出力関数(M46E-ASモード)
//!
//! 統計情報を引数で指定されたディスクリプタへ出力する。
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//! @param [in] fd              統計情報出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_printf_statistics_info_as(m46e_statistics_t* statistics_info, int fd)
{
    // 統計情報をファイルへ出力する
    dprintf(fd, "【M46E-AS】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     total recieve count             : %d\n", statistics_get_total_recv(statistics_info));
    dprintf(fd, "     total send count                : %d\n", statistics_get_total_send(statistics_info));
    dprintf(fd, "     total drop count                : %d\n", statistics_get_total_drop(statistics_info));
    dprintf(fd, "     total error count               : %d\n", statistics_get_total_error(statistics_info));
    dprintf(fd, "\n");
    dprintf(fd, "\n");
    dprintf(fd, "【TUNNEL IPV4】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count                   : %d \n", statistics_info->tunnel_v4_recieve_count);
    dprintf(fd, "       unicast(forward)              : %d \n", statistics_info->tunnel_v4_recv_unicast_count);
    dprintf(fd, "       multicast(forward)            : %d \n", statistics_info->tunnel_v4_recv_multicast_count);
    dprintf(fd, "       broadcast(drop)               : %d \n", statistics_info->tunnel_v4_err_broadcast_count);
    dprintf(fd, "       not IPv4 protocol(drop)       : %d \n", statistics_info->tunnel_v4_err_other_proto_count);
    dprintf(fd, "       link local multicast(drop)    : %d \n", statistics_info->tunnel_v4_err_linklocal_multi_count);
    dprintf(fd, "       fragment(drop)                : %d \n", statistics_info->tunnel_v4_err_as_fragment_count);
    dprintf(fd, "       not TCP/UDP(drop)             : %d \n", statistics_info->tunnel_v4_err_as_not_support_proto_count);
    dprintf(fd, "     send count                      : %d \n", statistics_info->tunnel_v4_send_count);
    dprintf(fd, "       send success                  : %d \n", statistics_info->tunnel_v4_send_v6_success_count);
    dprintf(fd, "       send error                    : %d \n", statistics_info->tunnel_v4_send_v6_err_count);
    dprintf(fd, "       send(fragment) success        : %d \n", statistics_info->tunnel_v4_send_fragment_success_count);
    dprintf(fd, "       send(fragment) error          : %d \n", statistics_info->tunnel_v4_send_fragment_err_count);
    dprintf(fd, "\n");
    dprintf(fd, "\n");
    dprintf(fd, "【TUNNEL IPV6】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count                   : %d \n", statistics_info->tunnel_v6_recieve_count);
    dprintf(fd, "       encap unicast(forward)        : %d \n", statistics_info->tunnel_v6_recv_unicast_count);
    dprintf(fd, "       encap multicast(forward)      : %d \n", statistics_info->tunnel_v6_recv_multicast_count);
    dprintf(fd, "       broadcast(drop)               : %d \n", statistics_info->tunnel_v6_err_broadcast_count);
    dprintf(fd, "       not IPv6 protocol(drop)       : %d \n", statistics_info->tunnel_v6_err_other_proto_count);
    dprintf(fd, "       ttl over(drop)                : %d \n", statistics_info->tunnel_v6_err_ttl_count);
    dprintf(fd, "       link local multicast(drop)    : %d \n", statistics_info->tunnel_v6_err_linklocal_multi_count);
    dprintf(fd, "       invalid next header(drop)     : %d \n", statistics_info->tunnel_v6_err_nxthdr_count);
    dprintf(fd, "     send count                      : %d \n", statistics_info->tunnel_v6_send_count);
    dprintf(fd, "       send success                  : %d \n", statistics_info->tunnel_v6_send_v4_success_count);
    dprintf(fd, "       send error                    : %d \n", statistics_info->tunnel_v6_send_v4_err_count);
    dprintf(fd, "\n");
    dprintf(fd, "【ICMP】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count\n");
    dprintf(fd, "       IPv6 icmp packet too big      : %d \n", statistics_info->icmp_pkt_toobig_recv_count);
    dprintf(fd, "     send count\n");
    dprintf(fd, "       IPv4 icmp fragment needed     : %d \n", statistics_info->icmp_fragneeded_send_count);
    dprintf(fd, "         send success                : %d \n", statistics_info->icmp_fragneeded_send_success_count);
    dprintf(fd, "         send error                  : %d \n", statistics_info->icmp_fragneeded_send_err_count);
    dprintf(fd, "\n");

    return;
}
///////////////////////////////////////////////////////////////////////////////
//! @brief 統計情報出力関数(M46E-PRモード)
//!
//! 統計情報を引数で指定されたディスクリプタへ出力する。
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//! @param [in] fd              統計情報出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_printf_statistics_info_pr(m46e_statistics_t* statistics_info, int fd)
{
    // 統計情報をファイルへ出力する
    dprintf(fd, "【M46E-PR】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     total recieve count             : %d\n", statistics_get_total_recv(statistics_info));
    dprintf(fd, "     total send count                : %d\n", statistics_get_total_send(statistics_info));
    dprintf(fd, "     total drop count                : %d\n", statistics_get_total_drop(statistics_info));
    dprintf(fd, "     total error count               : %d\n", statistics_get_total_error(statistics_info));
    dprintf(fd, "\n");
    dprintf(fd, "\n");
    dprintf(fd, "【TUNNEL IPV4】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count                   : %d \n", statistics_info->tunnel_v4_recieve_count);
    dprintf(fd, "       unicast(forward)              : %d \n", statistics_info->tunnel_v4_recv_unicast_count);
    dprintf(fd, "       broadcast(drop)               : %d \n", statistics_info->tunnel_v4_err_broadcast_count);
    dprintf(fd, "       not IPv4 protocol(drop)       : %d \n", statistics_info->tunnel_v4_err_other_proto_count);
    dprintf(fd, "       multicast(drop)               : %d \n", statistics_info->tunnel_v4_err_pr_multi_count);
    dprintf(fd, "       destination unknown(drop)     : %d \n", statistics_info->tunnel_v4_err_pr_search_failure_count);
    dprintf(fd, "     send count                      : %d \n", statistics_info->tunnel_v4_send_count);
    dprintf(fd, "       send success                  : %d \n", statistics_info->tunnel_v4_send_v6_success_count);
    dprintf(fd, "       send error                    : %d \n", statistics_info->tunnel_v4_send_v6_err_count);
    dprintf(fd, "       send(fragment) success        : %d \n", statistics_info->tunnel_v4_send_fragment_success_count);
    dprintf(fd, "       send(fragment) error          : %d \n", statistics_info->tunnel_v4_send_fragment_err_count);
    dprintf(fd, "\n");
    dprintf(fd, "\n");
    dprintf(fd, "【TUNNEL IPV6】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count                   : %d \n", statistics_info->tunnel_v6_recieve_count);
    dprintf(fd, "       encap unicast(forward)        : %d \n", statistics_info->tunnel_v6_recv_unicast_count);
    dprintf(fd, "       broadcast(drop)               : %d \n", statistics_info->tunnel_v6_err_broadcast_count);
    dprintf(fd, "       not IPv6 protocol(drop)       : %d \n", statistics_info->tunnel_v6_err_other_proto_count);
    dprintf(fd, "       ttl over(drop)                : %d \n", statistics_info->tunnel_v6_err_ttl_count);
    dprintf(fd, "       invalid next header(drop)     : %d \n", statistics_info->tunnel_v6_err_nxthdr_count);
    dprintf(fd, "     send count                      : %d \n", statistics_info->tunnel_v6_send_count);
    dprintf(fd, "       send success                  : %d \n", statistics_info->tunnel_v6_send_v4_success_count);
    dprintf(fd, "       send error                    : %d \n", statistics_info->tunnel_v6_send_v4_err_count);
    dprintf(fd, "\n");
    dprintf(fd, "【ICMP】\n");
    dprintf(fd, "\n");
    dprintf(fd, "   packet count\n");
    dprintf(fd, "     recieve count\n");
    dprintf(fd, "       IPv6 icmp packet too big      : %d \n", statistics_info->icmp_pkt_toobig_recv_count);
    dprintf(fd, "     send count\n");
    dprintf(fd, "       IPv4 icmp fragment needed     : %d \n", statistics_info->icmp_fragneeded_send_count);
    dprintf(fd, "         send success                : %d \n", statistics_info->icmp_fragneeded_send_success_count);
    dprintf(fd, "         send error                  : %d \n", statistics_info->icmp_fragneeded_send_err_count);
    dprintf(fd, "\n");

    return;
}
