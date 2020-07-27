/******************************************************************************/
/* ファイル名 : m46eapp_util.c                                                */
/* 機能概要   : 共通関数 ソースファイル                                       */
/* 修正履歴   : 2011.12.20 T.Maeda 新規作成                                   */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "m46eapp_util.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif


///////////////////////////////////////////////////////////////////////////////
//! @brief デバイスに設定されているIPv4アドレスを取得する関数
//!
//! 引数で指定されたデバイスのIPv4アドレスを取得する。
//!
//! @param [in]  dev  IPv4アドレスを取得するデバイス名
//! @param [out] addr 取得したIPv4アドレスの格納先
//!
//! @retval true  指定したデバイスにIPv4アドレスがある場合
//! @retval false 指定したデバイスにIPv4アドレスがない場合(デバイスが存在しない場合も含む)
///////////////////////////////////////////////////////////////////////////////
bool m46e_util_get_ipv4_addr(const char* dev, struct in_addr* addr)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          fd;

    // 引数チェック
    if((dev == NULL) || (addr == NULL)){
        return false;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(ifr));
    fd = -1;

    fd = socket(PF_INET, SOCK_DGRAM, 0);
    if(fd < 0){
        return false;
    }

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, dev, IFNAMSIZ-1);

    if(ioctl(fd, SIOCGIFADDR, &ifr) != 0){
        close(fd);
        return false;
    }

    close(fd);

    memcpy(addr, &(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr), sizeof(struct in_addr));

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイスに設定されているIPv4ネットマスクを取得する関数
//!
//! 引数で指定されたデバイスのIPv4ネットマスクを取得する。
//!
//! @param [in]  dev  IPv4ネットマスクを取得するデバイス名
//! @param [out] mask 取得したIPv4ネットマスクの格納先
//!
//! @retval true  処理成功
//! @retval false 処理失敗(デバイスが存在しない場合も含む)
///////////////////////////////////////////////////////////////////////////////
bool m46e_util_get_ipv4_mask(const char* dev, struct in_addr* mask)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          fd;

    // 引数チェック
    if((dev == NULL) || (mask == NULL)){
        return false;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(ifr));
    fd = -1;

    fd = socket(PF_INET, SOCK_DGRAM, 0);
    if(fd < 0){
        return false;
    }

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, dev, IFNAMSIZ-1);

    if(ioctl(fd, SIOCGIFNETMASK, &ifr) != 0){
        close(fd);
        return false;
    }

    close(fd);

    memcpy(mask, &(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr), sizeof(struct in_addr));

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief グローバルスコープのIPv6アドレスを取得する関数
//!
//! 引数で指定されたデバイスのグローバルスコープのIPv6アドレスを取得する。
//!
//! @param [in]  dev  IPv6アドレスを取得するデバイス名
//! @param [out] addr 取得したIPv6アドレスの格納先
//!
//! @retval true  指定したデバイスにグローバルスコープのIPv6アドレスがある場合
//! @retval false 指定したデバイスにグローバルスコープのIPv6アドレスがない場合
///////////////////////////////////////////////////////////////////////////////
bool m46e_util_get_ipv6_addr(const char* dev, struct in6_addr* addr)
{
    FILE* fp;
    int   if_index;
    int   plen;
    int   scope;
    int   dat_status;
    char  devname[IFNAMSIZ];
    bool  result;

    result = false;

    fp = fopen("/proc/net/if_inet6", "r");
    if(fp != NULL){
        while(fscanf(
            fp,
            "%08x%08x%08x%08x %02x %02x %02x %02x %20s\n",
            &addr->s6_addr32[0], &addr->s6_addr32[1], &addr->s6_addr32[2], &addr->s6_addr32[3],
            &if_index, &plen, &scope, &dat_status,
            devname
        ) != EOF){
            if(!strcmp(devname, dev) && (scope == 0)){
                addr->s6_addr32[0] = htonl(addr->s6_addr32[0]);
                addr->s6_addr32[1] = htonl(addr->s6_addr32[1]);
                addr->s6_addr32[2] = htonl(addr->s6_addr32[2]);
                addr->s6_addr32[3] = htonl(addr->s6_addr32[3]);
                result = true;
                break;
            } 
        }
        fclose(fp);
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MACブロードキャストチェック関数
//!
//! MACアドレスがブロードキャストアドレス(全てFF)かどうかをチェックする。
//!
//! @param [in]  mac_addr  MACアドレス
//!
//! @retval true  MACアドレスがブロードキャストアドレスの場合
//! @retval false MACアドレスがブロードキャストアドレスではない場合
///////////////////////////////////////////////////////////////////////////////
bool m46e_util_is_broadcast_mac(const unsigned char* mac_addr)
{
    // ローカル変数宣言
    int  i;
    bool result;

    // ローカル変数初期化
    i      = 0;
    result = true;
    
    // 引数チェック
    if(mac_addr == NULL){
        return false;
    }

    for(i=0; i<ETH_ALEN; i++){
        if(mac_addr[i] != 0xff){
           // アドレスが一つでも0xffでなければ結果をfalseにしてbreak
           result = false;
           break;
        }
    }

    return result;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief チェックサム計算関数
//!
//! 引数で指定されたコードとサイズからチェックサム値を計算して返す。
//!
//! @param [in]  buf  チェックサムを計算するコードの先頭アドレス
//! @param [in]  size チェックサムを計算するコードのサイズ
//!
//! @return 計算したチェックサム値
///////////////////////////////////////////////////////////////////////////////
unsigned short m46e_util_checksum(unsigned short *buf, int size)
{
    unsigned long sum = 0;

    while (size > 1) {
        sum += *buf++;
        size -= 2;
    }
    if (size){
        sum += *(u_int8_t *)buf;
    }

    sum  = (sum & 0xffff) + (sum >> 16);	/* add overflow counts */
    sum  = (sum & 0xffff) + (sum >> 16);	/* once again */

    return ~sum;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief チェックサム計算関数(複数ブロック対応)
//!
//! 引数で指定されたコードとサイズからチェックサム値を計算して返す。
//!
//! @param [in]  vec      チェックサムを計算するコード配列の先頭アドレス
//! @param [in]  vec_size チェックサムを計算するコード配列の要素数
//!
//! @return 計算したチェックサム値
///////////////////////////////////////////////////////////////////////////////
unsigned short m46e_util_checksumv(struct iovec* vec, int vec_size)
{
    int i;
    unsigned long sum = 0;

    for(i=0; i<vec_size; i++){
        unsigned short* buf  = (unsigned short*)vec[i].iov_base;
        int             size = vec[i].iov_len;

        while (size > 1) {
            sum += *buf++;
            size -= 2;
        }
        if (size){
            sum += *(u_int8_t *)buf;
        }
    }

    sum  = (sum & 0xffff) + (sum >> 16);	/* add overflow counts */
    sum  = (sum & 0xffff) + (sum >> 16);	/* once again */

    return ~sum;
}
