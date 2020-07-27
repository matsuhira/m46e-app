/******************************************************************************/
/* ファイル名 : m46eapp_statistics.h                                          */
/* 機能概要   : 統計情報管理 ヘッダファイル                                   */
/* 修正履歴   : 2011.12.20 M.Nagano 新規作成                                  */
/*              2012.07.23 T.Maeda Phase4向けに全面改版                       */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#ifndef __M46EAPP_STATISTICS_H__
#define __M46EAPP_STATISTICS_H__

#include <stdint.h>

////////////////////////////////////////////////////////////////////////////////
//! 仮想デバイス統計情報 構造体
////////////////////////////////////////////////////////////////////////////////
typedef struct _m46e_statistics_t
{
    //! 共有メモリID
    int shm_id;

    ////////////////////////////////////////////////////////////////////////////
    // ICMP関連
    ////////////////////////////////////////////////////////////////////////////
    //! ICMP Err too big(v6)受信数
    uint32_t icmp_pkt_toobig_recv_count;
    //! ICMP Err fragment needed(v4)送信数
    uint32_t icmp_fragneeded_send_count;
    //! ICMP Err fragment needed(v4)送信成功数
    uint32_t icmp_fragneeded_send_success_count;
    //! ICMP Err fragment needed(v4)送信失敗数
    uint32_t icmp_fragneeded_send_err_count;

    ////////////////////////////////////////////////////////////////////////////
    // IPv4トンネル関連
    ////////////////////////////////////////////////////////////////////////////
    //! IPv4パケット受信数
    uint32_t tunnel_v4_recieve_count;
    //! IPv4ユニキャストパケット受信数
    uint32_t tunnel_v4_recv_unicast_count;
    //! IPv4マルチキャストパケット受信数
    uint32_t tunnel_v4_recv_multicast_count;
    //! カプセル化パケット送信数
    uint32_t tunnel_v4_send_count;
    //! カプセル化パケット送信成功数
    uint32_t tunnel_v4_send_v6_success_count;
    //! カプセル化パケット送信失敗数
    uint32_t tunnel_v4_send_v6_err_count;
    //! カプセル化(フラグメント有)パケット送信数
    uint32_t tunnel_v4_send_fragneed_count;
    //! カプセル化(フラグメント有)パケット送信成功数
    uint32_t tunnel_v4_send_fragment_success_count;
    //! カプセル化(フラグメント有)パケット送信失敗数
    uint32_t tunnel_v4_send_fragment_err_count;
    //! ブロードキャストパケット受信数
    uint32_t tunnel_v4_err_broadcast_count;
    //! IPv4以外のプロトコルパケット受信数
    uint32_t tunnel_v4_err_other_proto_count;
    //! リンクローカルのマルチキャストパケット受信数
    uint32_t tunnel_v4_err_linklocal_multi_count;
    //! M46E-ASモードでのTCP/UDP以外のパケット受信数
    uint32_t tunnel_v4_err_as_not_support_proto_count;
    //! M46E-ASモードでのフラグメントパケット受信数
    uint32_t tunnel_v4_err_as_fragment_count;
    //! M46E-PRモードでのM46E-PR Tableの検索失敗数
    uint32_t tunnel_v4_err_pr_search_failure_count;
    //! M46E-PRモードでのIPv4マルチキャストパケット受信数
    uint32_t tunnel_v4_err_pr_multi_count;

    ////////////////////////////////////////////////////////////////////////////
    // IPv6トンネル関連
    ////////////////////////////////////////////////////////////////////////////
    //! IPv6パケット受信数
    uint32_t tunnel_v6_recieve_count;
    //! IPv4ユニキャストパケット(デカプセル化後)受信数
    uint32_t tunnel_v6_recv_unicast_count;
    //! IPv4マルチキャストパケット(デカプセル化後)受信数
    uint32_t tunnel_v6_recv_multicast_count;
    //! デカプセル化パケット送信数
    uint32_t tunnel_v6_send_count;
    //! デカプセル化パケット送信成功数
    uint32_t tunnel_v6_send_v4_success_count;
    //! デカプセル化パケット送信失敗数
    uint32_t tunnel_v6_send_v4_err_count;
    //! ブロードキャストパケット受信数
    uint32_t tunnel_v6_err_broadcast_count;
    //! TTL超過パケット(デカプセル化後)受信数
    uint32_t tunnel_v6_err_ttl_count;
    //! IPv6以外のプロトコルパケット受信数
    uint32_t tunnel_v6_err_other_proto_count;
    //! リンクローカルのマルチキャストパケット(デカプセル化後)受信数
    uint32_t tunnel_v6_err_linklocal_multi_count;
    //! NextHeaderがIPIP以外のパケット受信数
    uint32_t tunnel_v6_err_nxthdr_count;

} m46e_statistics_t;


///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
m46e_statistics_t* m46e_initial_statistics(const char* key_path);
void m46e_finish_statistics(m46e_statistics_t* statistics_info);
void m46e_printf_statistics_info_normal(m46e_statistics_t* statistics, int fd);
void m46e_printf_statistics_info_as(m46e_statistics_t* statistics, int fd);
void m46e_printf_statistics_info_pr(m46e_statistics_t* statistics, int fd);


///////////////////////////////////////////////////////////////////////////////
// カウントアップ用の関数はinlineで定義する
///////////////////////////////////////////////////////////////////////////////
inline void m46e_inc_icmp_pkt_toobig_recieve(m46e_statistics_t* statistics)
{
    statistics->icmp_pkt_toobig_recv_count++;
};

inline void m46e_inc_icmp_frag_needed_send_success(m46e_statistics_t* statistics)
{
    statistics->icmp_fragneeded_send_count++;
    statistics->icmp_fragneeded_send_success_count++;
};

inline void m46e_inc_icmp_frag_needed_send_err(m46e_statistics_t* statistics)
{
    statistics->icmp_fragneeded_send_count++;
    statistics->icmp_fragneeded_send_err_count++;
};

inline void m46e_inc_tunnel_v4_recieve(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_recieve_count++;
};

inline void m46e_inc_tunnel_v4_err_broadcast(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_err_broadcast_count++;
};

inline void m46e_inc_tunnel_v4_err_other_proto(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_err_other_proto_count++;
};

inline void m46e_inc_tunnel_v4_err_linklocal_multi(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_err_linklocal_multi_count++;
};

inline void m46e_inc_tunnel_v4_err_as_fragment(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_err_as_fragment_count++;
};

inline void m46e_inc_tunnel_v4_err_as_not_support_proto(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_err_as_not_support_proto_count++;
};

inline void m46e_inc_tunnel_v4_send_v6_err_count(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_send_v6_err_count++;
};

inline void m46e_inc_tunnel_v4_recv_multicast(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_recv_multicast_count++;
};

inline void m46e_inc_tunnel_v4_recv_unicast(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_recv_unicast_count++;
};

inline void m46e_inc_tunnel_v4_send_success(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_send_count++;
    statistics->tunnel_v4_send_v6_success_count++;
};

inline void m46e_inc_tunnel_v4_send_err(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_send_count++;
    statistics->tunnel_v4_send_v6_err_count++;
};

inline void m46e_inc_tunnel_v4_send_fragment_success(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_send_count++;
    statistics->tunnel_v4_send_fragment_success_count++;
};

inline void m46e_inc_tunnel_v4_send_fragment_err(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_send_count++;
    statistics->tunnel_v4_send_fragment_err_count++;
};

inline void m46e_inc_tunnel_v4_err_pr_search_failure(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_err_pr_search_failure_count++;
};

inline void m46e_inc_tunnel_v4_err_pr_multi(m46e_statistics_t* statistics)
{
    statistics->tunnel_v4_err_pr_multi_count++;
};

inline void m46e_inc_tunnel_v6_recieve(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_recieve_count++;
};

inline void m46e_inc_tunnel_v6_err_broadcast(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_err_broadcast_count++;
};

inline void m46e_inc_tunnel_v6_err_ttl(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_err_ttl_count++;
};

inline void m46e_inc_tunnel_v6_err_other_proto(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_err_other_proto_count++;
};

inline void m46e_inc_tunnel_v6_err_linklocal_multi(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_err_linklocal_multi_count++;
};

inline void m46e_inc_tunnel_v6_send_v4_err(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_send_count++;
    statistics->tunnel_v6_send_v4_err_count++;
};

inline void m46e_inc_tunnel_v6_send_v4_success(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_send_count++;
    statistics->tunnel_v6_send_v4_success_count++;
};

inline void m46e_inc_tunnel_v6_err_nxthdr_count(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_err_nxthdr_count++;
};

inline void m46e_inc_tunnel_v6_recv_multicast(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_recv_multicast_count++;
};

inline void m46e_inc_tunnel_v6_recv_unicast(m46e_statistics_t* statistics)
{
    statistics->tunnel_v6_recv_unicast_count++;
};


#endif // __M46EAPP_STATISTICS_H__
