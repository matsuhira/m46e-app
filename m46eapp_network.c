/******************************************************************************/
/* ファイル名 : m46eapp_network.c                                             */
/* 機能概要   : ネットワーク関連関数 ソースファイル                           */
/* 修正履歴   : 2012.08.10 T.Maeda 新規作成                                   */
/*              2013.08.21 H.Koganemaru M46E-PR機能拡張                       */
/*              2013.08.30 H.Koganemaru 動的定義変更機能追加                  */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <asm/types.h>
#include <linux/if_tun.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "m46eapp_network.h"
#include "m46eapp_config.h"
#include "m46eapp_log.h"
#include "m46eapp_netlink.h"

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイス生成関数
//!
//! トンネルデバイスを生成する。
//!
//! @param [in]     name       生成するデバイス名
//! @param [in,out] tunnel_dev デバイス構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_create_tap(const char* name, struct m46e_device_t* tunnel_dev)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;

    // 引数チェック
    if(tunnel_dev == NULL){
        return -1;
    }
    if((name == NULL) || (strlen(name) == 0)){
        // デバイス名が未設定の場合はエラー
        return -1;
    }
    if((tunnel_dev->type != M46E_DEVICE_TYPE_TUNNEL_IPV4) &&
       (tunnel_dev->type != M46E_DEVICE_TYPE_TUNNEL_IPV6)){
        // typeがトンネルデバイス以外の場合はエラー
        return -1;
    }
    if((tunnel_dev->option.tunnel.mode != IFF_TUN) &&
       (tunnel_dev->option.tunnel.mode != IFF_TAP)){
        // modeがTUN/TAP以外の場合はエラー
        return -1;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(ifr));
    result   = -1;

    // 仮想デバイスオープン
    result = open("/dev/net/tun", O_RDWR);
    if(result < 0){
        m46e_logging(LOG_ERR, "tun device open error : %s\n", strerror(errno));
        return result;
    }
    else{
        // ファイルディスクリプタを構造体に格納
        tunnel_dev->option.tunnel.fd = result;
        // close-on-exec フラグを設定
        fcntl(tunnel_dev->option.tunnel.fd, F_SETFD, FD_CLOEXEC);
    }

    strncpy(ifr.ifr_name, name, IFNAMSIZ-1);

    // Flag: IFF_TUN   - TUN device ( no ether header )
    //       IFF_TAP   - TAP device
    //       IFF_NO_PI - no packet information
    ifr.ifr_flags = tunnel_dev->option.tunnel.mode | IFF_NO_PI;
    // 仮想デバイス生成
    result = ioctl(tunnel_dev->option.tunnel.fd, TUNSETIFF, &ifr);
    if(result < 0){
        m46e_logging(LOG_ERR, "ioctl(TUNSETIFF) error : %s\n", strerror(errno));
        return result;
    }

    // デバイス名が変わっているかもしれないので、設定後のデバイス名を再取得
    //strcpy(tunnel_dev->name, ifr.ifr_name);

    // デバイスのインデックス番号取得
    tunnel_dev->ifindex = if_nametoindex(ifr.ifr_name);

    // NOARPフラグを設定
    result = m46e_network_set_flags_by_name(ifr.ifr_name, IFF_NOARP);
    if(result != 0){
        m46e_logging(LOG_ERR, "%s fail to set noarp flags : %s\n", tunnel_dev->name, strerror(result));
        return -1;
    }

    // MTUの設定
    if(tunnel_dev->mtu > 0){
        result = m46e_network_set_mtu_by_name(ifr.ifr_name, tunnel_dev->mtu);
    }
    else{
        result = m46e_network_get_mtu_by_name(ifr.ifr_name, &tunnel_dev->mtu);
    }
    if(result != 0){
        m46e_logging(LOG_WARNING, "%s configure mtu error : %s\n", tunnel_dev->name, strerror(result));
    }

    // MACアドレスの設定
    if(tunnel_dev->hwaddr != NULL){
        result = m46e_network_set_hwaddr_by_name(ifr.ifr_name, tunnel_dev->hwaddr);
    }
    else{
        tunnel_dev->hwaddr = malloc(sizeof(struct ether_addr));
        result = m46e_network_get_hwaddr_by_name(ifr.ifr_name, tunnel_dev->hwaddr);
    }
    if(result != 0){
        m46e_logging(LOG_ERR, "%s configure hwaddr error : %s\n", tunnel_dev->name, strerror(result));
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MAC-VLANデバイス生成関数
//!
//! MAC-VLANデバイスを生成する。
//!
//! @param [in]     name       生成するデバイス名
//! @param [in,out] device     デバイス構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_create_macvlan(const char* name, m46e_device_t* device)
{
    struct nlmsghdr*   nlmsg;
    struct ifinfomsg*  ifinfo;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;
    int                ifindex;
    struct rtattr*     nest1;
    struct rtattr*     nest2;

    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        m46e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s", strerror(errno));
        m46e_netlink_close(sock_fd);
        return errno;
    }

    // 対応する物理デバイスのインデックス取得
    ifindex = if_nametoindex(device->physical_name);
    if(ifindex == 0){
        m46e_logging(LOG_ERR, "Physical device not found : %s", device->physical_name);
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENODEV;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifinfo = (struct ifinfomsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifinfo->ifi_family = AF_UNSPEC;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_NEWLINK;

    nest1 = m46e_netlink_attr_begin(nlmsg, NETLINK_SNDBUF, IFLA_LINKINFO);
    if(nest1 == NULL){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    char dev_kind[] = "macvlan";
    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFLA_INFO_KIND, dev_kind, sizeof(dev_kind));
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    nest2 = m46e_netlink_attr_begin(nlmsg, NETLINK_SNDBUF, IFLA_INFO_DATA);
    if(nest2 == NULL){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    int mode = device->option.macvlan.mode;
    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFLA_MACVLAN_MODE, &mode, sizeof(mode));
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    m46e_netlink_attr_end(nlmsg, nest2);

    m46e_netlink_attr_end(nlmsg, nest1);

    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFLA_LINK, &ifindex, sizeof(ifindex));
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFLA_IFNAME, name, strlen(name)+1);
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = m46e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    m46e_netlink_close(sock_fd);
    free(nlmsg);

    // デバイスのインデックス番号取得
    device->ifindex = if_nametoindex(name);
    if(device->ifindex == 0){
        device->ifindex = -1;
        return ENODEV;
    }

    // MTUの設定
    if(device->mtu > 0){
        ret = m46e_network_set_mtu_by_name(name, device->mtu);
    }
    else{
        ret = m46e_network_get_mtu_by_name(name, &device->mtu);
    }
    if(ret != 0){
        m46e_logging(LOG_WARNING, "%s configure mtu error : %s\n", device->name, strerror(ret));
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 物理デバイス生成関数
//!
//! 物理デバイスのインデックス番号の取得と、MTU長の設定(取得)をおこなう。
//!
//! @param [in]     name       デバイス名
//! @param [in,out] device     デバイス構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_create_physical(const char* name, m46e_device_t* device)
{
    int result;

    // デバイスのインデックス番号取得
    device->ifindex = if_nametoindex(name);
    if(device->ifindex == 0){
        device->ifindex = -1;
        return ENODEV;
    }

    // MTUの設定
    if(device->mtu > 0){
        result = m46e_network_set_mtu_by_name(name, device->mtu);
    }
    else{
        result = m46e_network_get_mtu_by_name(name, &device->mtu);
    }
    if(result != 0){
        m46e_logging(LOG_WARNING, "%s configure mtu error : %s\n", device->name, strerror(result));
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス削除関数
//!
//! インデックス番号に対応するデバイスを削除する。
//!
//! @param [in]  ifindex    削除するデバイスのインデックス番号
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_device_delete_by_index(const int ifindex)
{
    struct nlmsghdr*   nlmsg;
    struct ifinfomsg*  ifinfo;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        m46e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s", strerror(errno));
        m46e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifinfo = (struct ifinfomsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifinfo->ifi_family = AF_UNSPEC;
    ifinfo->ifi_index  = ifindex;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_DELLINK;

    ret = m46e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    m46e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス移動関数
//!
//! インデックス番号に対応するデバイスのネットワーク空間を移動する。
//!
//! @param [in]  ifindex    移動するデバイスのインデックス番号
//! @param [in]  pid        移動先のネットワーク空間のプロセスID
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_device_move_by_index(const int ifindex, const pid_t pid)
{
    struct nlmsghdr*   nlmsg;
    struct ifinfomsg*  ifinfo;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        m46e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s", strerror(errno));
        m46e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifinfo = (struct ifinfomsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifinfo->ifi_family = AF_UNSPEC;
    ifinfo->ifi_index  = ifindex;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_NEWLINK;

    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFLA_NET_NS_PID, &pid, sizeof(pid));
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = m46e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    m46e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス名変更関数
//!
//! インデックス番号に対応するデバイスのデバイス名を変更する。
//!
//! @param [in]  ifindex    変更するデバイスのインデックス番号
//! @param [in]  newname    新しいデバイス名
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_device_rename_by_index(const int ifindex, const char* newname)
{
    struct nlmsghdr*   nlmsg;
    struct ifinfomsg*  ifinfo;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        m46e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s", strerror(errno));
        m46e_netlink_close(sock_fd);
        return ENOMEM;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifinfo = (struct ifinfomsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifinfo->ifi_family = AF_UNSPEC;
    ifinfo->ifi_index  = ifindex;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_NEWLINK;

    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFLA_IFNAME, newname, strlen(newname)+1);
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = m46e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    m46e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MTU長取得関数(デバイス名)
//!
//! デバイス名に対応するデバイスのMTU長を取得する。
//!
//! @param [in]  ifname    デバイス名
//! @param [out] mtu       取得したMTU長の格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_get_mtu_by_name(const char* ifname, int* mtu)
{
    // ローカル変数宣言
    struct ifreq ifr;  
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        m46e_logging(LOG_ERR, "ifname is NULL\n");
        return EINVAL;
    }
    if(mtu == NULL){
        m46e_logging(LOG_ERR, "mtu is NULL\n");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        m46e_logging(LOG_ERR, "socket open error : %s\n", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    result = ioctl(sock, SIOCGIFMTU, &ifr);
    if(result != 0) {
        m46e_logging(LOG_ERR, "ioctl(SIOCGIFMTU) error : %s\n", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    *mtu = ifr.ifr_mtu;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MTU長取得関数(インデックス番号)
//!
//! インデックス番号に対応するデバイスのMTU長を取得する。
//!
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [out] mtu       取得したMTU長の格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_get_mtu_by_index(const int ifindex, int* mtu)
{
    // ローカル変数宣言
    char ifname[IFNAMSIZ];  

    // ローカル変数初期化
    memset(ifname, 0, sizeof(ifname));

    if(if_indextoname(ifindex, ifname) == NULL){
        m46e_logging(LOG_ERR, "if_indextoname error : %s\n", strerror(errno));
        return errno;
    }
    return m46e_network_get_mtu_by_name(ifname, mtu);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MTU長設定関数(デバイス名)
//!
//! デバイス名に対応するデバイスのMTU長を設定する。
//!
//! @param [in]  ifname    デバイス名
//! @param [in]  mtu       設定するMTU長
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_set_mtu_by_name(const char* ifname, const int mtu)
{
    // ローカル変数宣言
    struct ifreq ifr;  
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        m46e_logging(LOG_ERR, "ifname is NULL\n");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        m46e_logging(LOG_ERR, "socket open error : %s\n", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    ifr.ifr_mtu = mtu;

    result = ioctl(sock, SIOCSIFMTU, &ifr);
    if(result != 0) {
        m46e_logging(LOG_ERR, "ioctl(SIOCSIFMTU) error : %s\n", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MTU長設定関数(インデックス番号)
//!
//! インデックス番号に対応するデバイスのMTU長を設定する。
//!
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  mtu       設定するMTU長
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_set_mtu_by_index(const int ifindex, const int mtu)
{
    // ローカル変数宣言
    char ifname[IFNAMSIZ];  

    // ローカル変数初期化
    memset(ifname, 0, sizeof(ifname));

    if(if_indextoname(ifindex, ifname) == NULL){
        m46e_logging(LOG_ERR, "if_indextoname error : %s\n", strerror(errno));
        return errno;
    }
    return m46e_network_set_mtu_by_name(ifname, mtu);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MACアドレス取得関数(デバイス名)
//!
//! デバイス名に対応するデバイスのMACアドレスを取得する。
//!
//! @param [in]  ifname    デバイス名
//! @param [out] hwaddr    取得したMACアドレスの格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_get_hwaddr_by_name(const char* ifname, struct ether_addr* hwaddr)
{
    // ローカル変数宣言
    struct ifreq ifr;  
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        m46e_logging(LOG_ERR, "ifname is NULL\n");
        return EINVAL;
    }
    if(hwaddr == NULL){
        m46e_logging(LOG_ERR, "hwaddr is NULL\n");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        m46e_logging(LOG_ERR, "socket open error : %s\n", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    result = ioctl(sock, SIOCGIFHWADDR, &ifr);
    if(result != 0) {
        m46e_logging(LOG_ERR, "ioctl(SIOCGIFHWADDR) error : %s\n", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    memcpy(hwaddr->ether_addr_octet, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MACアドレス取得関数(インデックス番号)
//!
//! インデックス番号に対応するデバイスのMACアドレスを取得する。
//!
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [out] hwaddr    取得したMACアドレスの格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_get_hwaddr_by_index(const int ifindex, struct ether_addr* hwaddr)
{
    // ローカル変数宣言
    char ifname[IFNAMSIZ];  

    // ローカル変数初期化
    memset(ifname, 0, sizeof(ifname));

    if(if_indextoname(ifindex, ifname) == NULL){
        m46e_logging(LOG_ERR, "if_indextoname error : %s\n", strerror(errno));
        return errno;
    }
    return m46e_network_get_hwaddr_by_name(ifname, hwaddr);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MACアドレス設定関数(デバイス名)
//!
//! デバイス名に対応するデバイスのMACアドレスを設定する。
//!
//! @param [in]  ifname    デバイス名
//! @param [in]  hwaddr    設定するMACアドレス
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_set_hwaddr_by_name(const char* ifname, const struct ether_addr* hwaddr)
{
    // ローカル変数宣言
    struct ifreq ifr;  
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        m46e_logging(LOG_ERR, "ifname is NULL\n");
        return EINVAL;
    }
    if(hwaddr == NULL){
        m46e_logging(LOG_ERR, "hwaddr is NULL\n");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        m46e_logging(LOG_ERR, "socket open error : %s\n", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, hwaddr->ether_addr_octet, ETH_ALEN);

    result = ioctl(sock, SIOCSIFHWADDR, &ifr);
    if(result != 0) {
        m46e_logging(LOG_ERR, "ioctl(SIOCSIFHWADDR) error : %s\n", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
//! @brief MACアドレス設定関数(インデックス番号)
//!
//! インデックス番号に対応するデバイスのMACアドレスを設定する。
//!
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  hwaddr    設定するMACアドレス
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_set_hwaddr_by_index(const int ifindex, const struct ether_addr* hwaddr)
{
    // ローカル変数宣言
    char ifname[IFNAMSIZ];  

    // ローカル変数初期化
    memset(ifname, 0, sizeof(ifname));

    if(if_indextoname(ifindex, ifname) == NULL){
        m46e_logging(LOG_ERR, "if_indextoname error : %s\n", strerror(errno));
        return errno;
    }
    return m46e_network_set_hwaddr_by_name(ifname, hwaddr);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief フラグ取得関数(デバイス名)
//!
//! デバイス名に対応するデバイスのフラグを取得する。
//!
//! @param [in]  ifname    デバイス名
//! @param [out] flags     取得したフラグの格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_get_flags_by_name(const char* ifname, short* flags)
{
    // ローカル変数宣言
    struct ifreq ifr;  
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        m46e_logging(LOG_ERR, "ifname is NULL\n");
        return EINVAL;
    }
    if(flags == NULL){
        m46e_logging(LOG_ERR, "flags is NULL\n");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        m46e_logging(LOG_ERR, "socket open error : %s\n", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    result = ioctl(sock, SIOCGIFFLAGS, &ifr);
    if(result != 0) {
        m46e_logging(LOG_ERR, "ioctl(SIOCGIFFLAGS) error : %s\n", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    *flags = ifr.ifr_flags;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief フラグ取得関数(インデックス番号)
//!
//! インデックス番号に対応するデバイスのフラグを取得する。
//!
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [out] flags     取得したフラグの格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_get_flags_by_index(const int ifindex, short* flags)
{
    // ローカル変数宣言
    char ifname[IFNAMSIZ];

    // ローカル変数初期化
    memset(ifname, 0, sizeof(ifname));

    if(if_indextoname(ifindex, ifname) == NULL){
        m46e_logging(LOG_ERR, "if_indextoname error : %s\n", strerror(errno));
        return errno;
    }
    return m46e_network_get_flags_by_name(ifname, flags);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief フラグ設定関数(デバイス名)
//!
//! デバイス名に対応するデバイスのフラグを設定する。
//!
//! @param [in]  ifname    デバイス名
//! @param [in]  flags     設定するフラグ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_set_flags_by_name(const char* ifname, const short flags)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        m46e_logging(LOG_ERR, "ifname is NULL\n");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        m46e_logging(LOG_ERR, "socket open error : %s\n", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    // 現在の状態を取得
    result = m46e_network_get_flags_by_name(ifr.ifr_name, &ifr.ifr_flags);
    if(result != 0){
        m46e_logging(LOG_ERR, "fail to get current flags : %s\n", strerror(result));
        return result;
    }

    if(flags < 0){
        // 負値の場合はフラグを落とす
        // ifr.ifr_flags &= ~(-flags);
        ifr.ifr_flags &= flags;
    }
    else{
        // 正値の場合はフラグを上げる
        ifr.ifr_flags |= flags;
    }

    result = ioctl(sock, SIOCSIFFLAGS, &ifr);
    if(result != 0) {
        m46e_logging(LOG_ERR, "ioctl(SIOCSIFFLAGS) error : %s\n", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
//! @brief フラグ設定関数(インデックス番号)
//!
//! インデックス番号に対応するデバイスのフラグを設定する。
//!
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  flags     設定するフラグ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_set_flags_by_index(const int ifindex, const short flags)
{
    // ローカル変数宣言
    char ifname[IFNAMSIZ];

    // ローカル変数初期化
    memset(ifname, 0, sizeof(ifname));

    if(if_indextoname(ifindex, ifname) == NULL){
        m46e_logging(LOG_ERR, "if_indextoname error : %s\n", strerror(errno));
        return errno;
    }
    return m46e_network_set_flags_by_name(ifname, flags);
}

//////////////////////////////////////////////////////////////////////////////
//! @brief IPアドレス設定関数
//!
//! インデックス番号に対応するデバイスのIPアドレスを設定する。
//!
//! @param [in]  family    設定するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  addr      設定するIPアドレス
//!                        (IPv4の場合はin_addr、IPv6の場合はin6_addr構造体)
//! @param [in]  prefixlen 設定するIPアドレスのプレフィックス長
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_add_ipaddr(
    const int   family,
    const int   ifindex,
    const void* addr,
    const int   prefixlen
)
{
    struct nlmsghdr*   nlmsg;
    struct ifaddrmsg*  ifaddr;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        m46e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s", strerror(errno));
        m46e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifaddr = (struct ifaddrmsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifaddr->ifa_family     = family;
    ifaddr->ifa_index      = ifindex;
    ifaddr->ifa_prefixlen  = prefixlen;
    ifaddr->ifa_scope      = 0;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_NEWADDR;

    int addrlen = (family == AF_INET) ? sizeof(struct in_addr) : sizeof(struct in6_addr);
    
    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_LOCAL, addr, addrlen);
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_ADDRESS, addr, addrlen);
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    if(family == AF_INET){
        struct in_addr baddr = *((struct in_addr*)addr);
        baddr.s_addr |= htonl(INADDR_BROADCAST >> prefixlen);
        ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_BROADCAST, &baddr, addrlen);
        if(ret != RESULT_OK){
            m46e_logging(LOG_ERR, "Netlink add attrubute error");
            m46e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    ret = m46e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    m46e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
//! @brief 経路設定関数
//!
//! インデックス番号に対応するデバイスを経由する経路を設定する。
//! デフォルトゲートウェイを追加する場合はdstをNULLしてgwを設定する。
//! connectedの経路を追加する場合は、gwをNULLに設定する。
//!
//! @param [in]  family    設定するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  dst       設定する経路の送信先アドレス
//!                        (IPv4の場合はin_addr、IPv6の場合はin6_addr構造体)
//! @param [in]  prefixlen 設定する経路のプレフィックス長
//! @param [in]  gw        設定する経路のゲートウェイアドレス
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_add_route(
    const int   family,
    const int   ifindex,
    const void* dst,
    const int   prefixlen,
    const void* gw
)
{
    struct nlmsghdr*   nlmsg;
    struct rtmsg*      rt;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        m46e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s", strerror(errno));
        m46e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    rt = (struct rtmsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    rt->rtm_family   = family;
    rt->rtm_table    = RT_TABLE_MAIN;
    rt->rtm_scope    = RT_SCOPE_UNIVERSE;
    rt->rtm_protocol = RTPROT_STATIC;
    rt->rtm_type     = RTN_UNICAST;
    rt->rtm_dst_len  = prefixlen;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_NEWROUTE;

    int addrlen = (family == AF_INET) ? sizeof(struct in_addr) : sizeof(struct in6_addr);

    if(dst != NULL){
        ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_DST, dst, addrlen);
        if(ret != RESULT_OK){
            m46e_logging(LOG_ERR, "Netlink add attrubute error");
            m46e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    if(gw != NULL){
        ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_GATEWAY, gw, addrlen);
        if(ret != RESULT_OK){
            m46e_logging(LOG_ERR, "Netlink add attrubute error");
            m46e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_OIF, &ifindex, sizeof(ifindex));
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = m46e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    m46e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
//! @brief 経路削除関数
//!
//! インデックス番号に対応するデバイスを経由する経路を削除する。
//! デフォルトゲートウェイを削除する場合はdstをNULLしてgwを設定する。
//! connectedの経路を削除する場合は、gwをNULLに設定する。
//!
//! @param [in]  family    削除するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  dst       削除する経路の送信先アドレス
//!                        (IPv4の場合はin_addr、IPv6の場合はin6_addr構造体)
//! @param [in]  prefixlen 削除する経路のプレフィックス長
//! @param [in]  gw        削除する経路のゲートウェイアドレス
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_del_route(
    const int   family,
    const int   ifindex,
    const void* dst,
    const int   prefixlen,
    const void* gw
)
{
    struct nlmsghdr*   nlmsg;
    struct rtmsg*      rt;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = m46e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        m46e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        m46e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s", strerror(errno));
        m46e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    rt = (struct rtmsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    rt->rtm_family   = family;
    rt->rtm_table    = RT_TABLE_MAIN;
    rt->rtm_scope    = RT_SCOPE_UNIVERSE;
    rt->rtm_protocol = RTPROT_STATIC;
    rt->rtm_type     = RTN_UNICAST;
    rt->rtm_dst_len  = prefixlen;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_EXCL | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_DELROUTE;

    int addrlen = (family == AF_INET) ? sizeof(struct in_addr) : sizeof(struct in6_addr);

    if(dst != NULL){
        ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_DST, dst, addrlen);
        if(ret != RESULT_OK){
            m46e_logging(LOG_ERR, "Netlink add attrubute error");
            m46e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    if(gw != NULL){
        ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_GATEWAY, gw, addrlen);
        if(ret != RESULT_OK){
            m46e_logging(LOG_ERR, "Netlink add attrubute error");
            m46e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    ret = m46e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_OIF, &ifindex, sizeof(ifindex));
    if(ret != RESULT_OK){
        m46e_logging(LOG_ERR, "Netlink add attrubute error");
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = m46e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        m46e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    m46e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
//! @brief デフォルトゲートウェイ設定関数
//!
//! インデックス番号に対応するデバイスを経由する
//! デフォルトゲートウェイを設定する。
//!
//! @param [in]  family    設定するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  gw        デフォルトゲートウェイアドレス
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_add_gateway(
    const int   family,
    const int   ifindex,
    const void* gw
)
{
    return m46e_network_add_route(family, ifindex, NULL, 0, gw);
}

//////////////////////////////////////////////////////////////////////////////
//! @brief デフォルトゲートウェイ削除関数
//!
//! インデックス番号に対応するデバイスを経由する
//! デフォルトゲートウェイを削除する。
//!
//! @param [in]  family    設定するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  gw        デフォルトゲートウェイアドレス
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_network_del_gateway(
    const int   family,
    const int   ifindex,
    const void* gw
)
{
    return m46e_network_del_route(family, ifindex, NULL, 0, gw);
}

