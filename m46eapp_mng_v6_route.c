/******************************************************************************/
/* ファイル名 : m46eapp_sync_v6_route.c                                       */
/* 機能概要   : v6経路同期 ソースファイル                                     */
/* 修正履歴   : 2013.07.19 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/shm.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "m46eapp.h"
#include "m46eapp_log.h"
#include "m46eapp_netlink.h"
#include "m46eapp_rtnetlink.h"
#include "m46eapp_mng_com_route.h"
#include "m46eapp_mng_v6_route.h"
#include "m46eapp_sync_com_route.h"
#include "m46eapp_sync_v6_route.h"

///////////////////////////////////////////////////////////////////////////////
//! @brief v6経路情報情報出力関数
//!
//! 6Pv4 経路情報(エントリー情報)を出力する
//!
//! @param [in]     fd              出力先のディスクリプタ
//! @param [in]     route_info6     IPv6 経路情報
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_print_route6(int fd, struct m46e_v6_route_info_t *route_info6)
{
    char tmp[100];
    char tmp2[100];
    char tmp3[100];
    char tmp4[100];
    char tmp5[100];
    char tmp6[100];
    char tmp7[100];
    char tmp8[100];
    struct ifreq ifr;
    int soc;

    switch(route_info6->type)
    {
        case RTN_UNICAST:
            sprintf(tmp2, "%13s", "RTN_UNICAST");
            break;
        case RTN_LOCAL:
            sprintf(tmp2, "%13s", "RTN_LOCAL");
            break;
        case RTN_BROADCAST:
            sprintf(tmp2, "%13s", "RTN_BROADCAST");
            break;
        case RTN_ANYCAST:
            sprintf(tmp2, "%13s", "RTN_ANYCAST");
            break;
        case RTN_MULTICAST:
            sprintf(tmp2, "%13s", "RTN_MULTICAST");
            break;
        case RTN_UNREACHABLE:
            sprintf(tmp2, "%13s", "RTN_UNREACHABLE");
            break;
        default:
            sprintf(tmp2, "OTHER(%d)", route_info6->type);
            break;
    }

    if(!IN6_IS_ADDR_UNSPECIFIED(&route_info6->in_dst) ){
        inet_ntop(AF_INET6, &route_info6->in_dst, tmp, sizeof(tmp));
        sprintf(tmp3, "%40s/%-3d", tmp, route_info6->mask);
    }else{
        sprintf(tmp3, "                                      ::/0  ");
    }
    if( !IN6_IS_ADDR_UNSPECIFIED(&route_info6->in_gw) ){
        inet_ntop(AF_INET6, &route_info6->in_gw, tmp, sizeof(tmp));
        sprintf(tmp4, "%40s", tmp);
    }else{
        sprintf(tmp4, "                                        ");
    }
    if(!IN6_IS_ADDR_UNSPECIFIED(&route_info6->in_src) ){
        inet_ntop(AF_INET6, &route_info6->in_src, tmp, sizeof(tmp));
        sprintf(tmp5, "%40s", tmp);
    }else{
        sprintf(tmp5, "                                        ");
    }

    if( route_info6->priority){
        sprintf(tmp6, "%5d", route_info6->priority);
    }else{
        sprintf(tmp6, "     ");
    }

    /* Print device */
    soc =socket( PF_INET, SOCK_DGRAM, 0 );
    memset(&ifr,0,sizeof(ifr));
    ifr.ifr_ifindex = route_info6->out_if_index;
    ioctl(soc, SIOCGIFNAME, &ifr);
    close(soc);
    sprintf(tmp7, "%14s(%d)", ifr.ifr_name, route_info6->out_if_index);

    if( route_info6->sync ) {
        sprintf(tmp8, "  *  ");
    } else {
        sprintf(tmp8, "     ");
    }

    dprintf(fd, "%s|%s|%s|%s|%s|%s|%s\n",  tmp8, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル内部情報出力関数
//!
//! PMTU管理内のテーブルを出力する
//!
//! @param [in]     pmtud_handler   PMTU管理
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_route_print_v6table(struct m46e_handler_t* handler, int fd)
{
    int i;
    int soc;
    struct ifreq ifr;

    v6_route_info_table_t*  v6_route = handler->v6_route_info;

    DEBUG_SYNC_LOG("m46e_route_print_v6table start \n");

    soc = socket( PF_INET, SOCK_DGRAM, 0 );
    memset(&ifr,0,sizeof(ifr));
    ifr.ifr_ifindex = v6_route->tunnel_dev_idx;
    ioctl(soc, SIOCGIFNAME, &ifr);
    close(soc);

    dprintf(fd, "-------------  v6 route ----------------\n");
    dprintf(fd, "max            = %d\n", v6_route->max);
    dprintf(fd, "num            = %d\n", v6_route->num);

    dprintf(fd, "-----+-------------+--------------------------------------------+----------------------------------------+----------------------------------------+-----+--------------------\n");
    dprintf(fd, " Sync| Route Type  |   Dist v6 Addr/mask                        |     Gateway                            |   Src v6 addr                          | Pri | Device name(index) \n");
    dprintf(fd, "-----+-------------+--------------------------------------------+----------------------------------------+----------------------------------------+-----+--------------------\n");

    for (i = 0; i < v6_route->num; i++) {
        m46e_print_route6(fd, &v6_route->table[i]);
    }
    dprintf(fd, "-----+-------------+--------------------------------------------+----------------------------------------+----------------------------------------+-----+--------------------\n");

    DEBUG_SYNC_LOG("m46e_route_print_v6table end \n");
    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief v6経路情報領域作成関数
//!
//! 共有メモリにIPv6の経路情報用領域を確保する。
//!
//! @param [in,out] handler     M46Eハンドラ
//!
//! @retval true    正常終了
//! @retval false   異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_sync_route_initial_v6_table(struct m46e_handler_t* handler)
{
    int shm_id = 0;
    int count = 0;

    DEBUG_SYNC_LOG("m46e_sync_route_initial_v6_table start.\n");

    // 経路情報の最大エントリー数の取得
    count = handler->conf->general->route_entry_max;

//    shm_id = shmget(ftok(handler->conf->filename, 'B'),
//                    sizeof(v6_route_info_table_t), IPC_CREAT);
    shm_id = shmget(IPC_PRIVATE, sizeof(v6_route_info_table_t), IPC_CREAT);
    if (shm_id == -1) {
        m46e_logging(LOG_ERR, "v6 route shared memory allocation faulure for statistics : %s\n", strerror(errno));
        return false;
    }

    handler->v6_route_info = (v6_route_info_table_t*)shmat(shm_id, NULL, 0);
    if (handler->v6_route_info == (void *)-1) {
        m46e_logging(LOG_ERR, "v6 route shared memory attach failure for statistics : %s\n", strerror(errno));
        return false;
    }

    // 初期化
    memset(handler->v6_route_info, 0, sizeof(v6_route_info_table_t));
    handler->v6_route_info->shm_id = shm_id;
    handler->v6_route_info->max = count;
    m46e_logging(LOG_INFO, "v6 route shared memory ID=%d \n", shm_id);

//    shm_id = shmget(ftok(handler->conf->filename, 'C'),
//            (sizeof(m46e_v6_route_info_t) * count), IPC_CREAT);
    shm_id = shmget(IPC_PRIVATE, (sizeof(m46e_v6_route_info_t) * count), IPC_CREAT);
    if (shm_id == -1) {
        m46e_logging(LOG_ERR, "v6 route shared memory allocation faulure for statistics : %s\n", strerror(errno));
        return false;
    }

    DEBUG_SYNC_LOG("v6 shmget size = %d\n", (sizeof(m46e_v6_route_info_t) * count));

    handler->v6_route_info->table = (m46e_v6_route_info_t*)shmat(shm_id, NULL, 0);
    if (handler->v6_route_info->table == (void *)-1) {
        m46e_logging(LOG_ERR, "v6 route shared memory attach failure for statistics : %s\n", strerror(errno));
        return false;
    }

    // v6経路情報の初期化
    memset(handler->v6_route_info->table, 0, ((sizeof(m46e_v6_route_info_t))*count));
    handler->v6_route_info->t_shm_id = shm_id;

    // 排他制御初期化
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&handler->v6_route_info->mutex, &attr);

    m46e_logging(LOG_INFO, "v6 route shared memory ID=%d \n", shm_id);

    DEBUG_SYNC_LOG("m46e_sync_route_initial_v6_table end.\n");

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief v6経路情報領域解放関数
//!
//! 共有メモリに確保しているv6経路情報用領域を解放する。
//!
//! @param [in] v6_route_info_table_t v6統計情報用領域のポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void  m46e_finish_v6_table(v6_route_info_table_t* v6_route_info_table)
{
    int ret;

    DEBUG_SYNC_LOG("m46e_finish_v6_table start.\n");

    // 共有メモリ破棄
    ret = shmctl(v6_route_info_table->t_shm_id, IPC_RMID, NULL);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to destruct v6 route shared memory : %s\n", strerror(errno));
    }

    ret = shmdt(v6_route_info_table->table);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to detach v6 route shared memory : %s\n", strerror(errno));
    }

    // 排他制御終了
    pthread_mutex_destroy(&v6_route_info_table->mutex);

    ret = shmctl(v6_route_info_table->shm_id, IPC_RMID, NULL);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to destruct v6 route shared memory : %s\n", strerror(errno));
    }

    ret = shmdt(v6_route_info_table);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to detach v6 route shared memory : %s\n", strerror(errno));
    }


    DEBUG_SYNC_LOG("m46e_finish_v6_table end.\n");

    return;
}
