/******************************************************************************/
/* ファイル名 : m46eapp_mng_v4_route.c                                        */
/* 機能概要   : v4経路管理 ソースファイル                                     */
/* 修正履歴   : 2013.06.06 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
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

#include "m46eapp.h"
#include "m46eapp_log.h"
#include "m46eapp_netlink.h"
#include "m46eapp_list.h"
#include "m46eapp_rtnetlink.h"
#include "m46eapp_mng_com_route.h"
#include "m46eapp_mng_v4_route.h"
#include "m46eapp_sync_com_route.h"
#include "m46eapp_sync_v4_route.h"



///////////////////////////////////////////////////////////////////////////////
//! @brief v4経路情報情報出力関数
//!
//! IPv4 経路情報(エントリー情報)を出力する
//!
//! @param [in]     fd              出力先のディスクリプタ
//! @param [in]     route_info      IPv4 経路情報
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_print_route(int fd, struct m46e_v4_route_info_t *route_info)
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

    switch(route_info->type)
    {
        case RTN_UNICAST:
            sprintf(tmp2, "%15s", "RTN_UNICAST");
            break;
        case RTN_LOCAL:
            sprintf(tmp2, "%15s", "RTN_LOCAL");
            break;
        case RTN_BROADCAST:
            sprintf(tmp2, "%15s", "RTN_BROADCAST");
            break;
        case RTN_ANYCAST:
            sprintf(tmp2, "%15s", "RTN_ANYCAST");
            break;
        case RTN_MULTICAST:
            sprintf(tmp2, "%15s", "RTN_MULTICAST");
            break;
        case RTN_UNREACHABLE:
            sprintf(tmp2, "%15s", "RTN_UNREACHABLE");
            break;
        default:
            sprintf(tmp2, "OTHER(%8d)", route_info->type);
            break;
    }

    if( route_info->in_dst.s_addr ){
        inet_ntop(AF_INET, &route_info->in_dst, tmp, sizeof(tmp));
        sprintf(tmp3, "%15s/%-2d", tmp, route_info->mask);
    }else{
        sprintf(tmp3, "        0.0.0.0/0 ");
    }
    if( route_info->in_gw.s_addr ){
        inet_ntop(AF_INET, &route_info->in_gw, tmp, sizeof(tmp));
        sprintf(tmp4, "%15s", tmp);
    }else{
        sprintf(tmp4, "               ");
    }
    if( route_info->in_src.s_addr ){
        inet_ntop(AF_INET, &route_info->in_src, tmp, sizeof(tmp));
        sprintf(tmp5, "%15s", tmp);
    }else{
        sprintf(tmp5, "               ");
    }

    if( route_info->priority){
        sprintf(tmp6, "%5d", route_info->priority);
    }else{
        sprintf(tmp6, "     ");
    }

    /* Print device */
    soc = socket( PF_INET, SOCK_DGRAM, 0 );
    memset(&ifr,0,sizeof(ifr));
    ifr.ifr_ifindex = route_info->out_if_index;
    ioctl(soc, SIOCGIFNAME, &ifr);
    close(soc);
    sprintf(tmp7, "%13s(%-2d)", ifr.ifr_name, route_info->out_if_index);

    if( route_info->sync ) {
        sprintf(tmp8, "  *  ");
    } else {
        sprintf(tmp8, "     ");
    }

    dprintf(fd, "%s|%s|%s|%s|%s|%s|%s\n",  tmp8, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7);

}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル内部情報出力関数
//!
//! v4経路同期管理内のテーブルを出力する
//!
//! @param [in]     pmtud_handler   PMTU管理
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_route_print_v4table(struct m46e_handler_t* handler, int fd)
{
    int i;
    int soc;
    struct ifreq ifr;
    v4_route_info_table_t*  v4_route = handler->v4_route_info;

    DEBUG_SYNC_LOG("m46e_route_print_v4table start \n");

    soc = socket( PF_INET, SOCK_DGRAM, 0 );
    memset(&ifr,0,sizeof(ifr));
    ifr.ifr_ifindex = v4_route->tunnel_dev_idx;
    ioctl(soc, SIOCGIFNAME, &ifr);
    dprintf(fd, "\n");
    dprintf(fd, "-------------  v4 route ----------------\n");
    dprintf(fd, "max            = %d\n", v4_route->max);
    dprintf(fd, "num            = %d\n", v4_route->num);

    m46e_list* iter;
    int* index;
    m46e_list_for_each(iter, &v4_route->device_list) {
        index = iter->data;
        if(index != NULL){
            memset(&ifr,0,sizeof(ifr));
            ifr.ifr_ifindex = *index;
            ioctl(soc, SIOCGIFNAME, &ifr);
            dprintf(fd, "device index   = %s(%d)\n", ifr.ifr_name, *index);
        }
    }

    close(soc);

    dprintf(fd, "-----+---------------+------------------+---------------+---------------+-----+--------------------\n");
    dprintf(fd, " Sync|  Route  Type  | Dist v4 Addr/mask|   Gateway     | Src v4 addr   | Pri | Device name(index) \n");
    dprintf(fd, "-----+---------------+------------------+---------------+---------------+-----+--------------------\n");
    for (i = 0; i < v4_route->num; i++) {
        m46e_print_route(fd, &v4_route->table[i]);
    }
    dprintf(fd, "-----+---------------+------------------+---------------+---------------+-----+--------------------\n");
    dprintf(fd, "\n");

    DEBUG_SYNC_LOG("m46e_route_print_v4table end \n");
    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief v4経路情報領域作成関数
//!
//! 共有メモリにIPv4の経路情報用領域を確保する。
//!
//! @param [in,out] handler     M46Eハンドラ
//!
//! @retval true    正常終了
//! @retval false   異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_sync_route_initial_v4_table(struct m46e_handler_t* handler)
{
    int shm_id = 0;
    int count = 0;

    DEBUG_SYNC_LOG("m46e_sync_route_initial_v4_table start.\n");

    // 経路情報の最大エントリー数の取得
    count = handler->conf->general->route_entry_max;

    /////////////////////////////////////////////////////////
    // IPv4 経路情報 テーブルを共有メモリに作成
    /////////////////////////////////////////////////////////
//    shm_id = shmget(ftok(handler->conf->filename, 'D'),
//            sizeof(v4_route_info_table_t), IPC_CREAT);
    shm_id = shmget(IPC_PRIVATE, sizeof(v4_route_info_table_t), IPC_CREAT);
    if (shm_id == -1) {
        m46e_logging(LOG_ERR, "v4 route shared memory allocation faulure for statistics : %s\n", strerror(errno));
        return false;
    }

    handler->v4_route_info = (v4_route_info_table_t*)shmat(shm_id, NULL, 0);
    if (handler->v4_route_info == (void *)-1) {
        m46e_logging(LOG_ERR, "v4 route shared memory attach failure for statistics : %s\n", strerror(errno));
        return false;
    }

    // IPv4 経路情報 テーブルの初期化
    memset(handler->v4_route_info, 0, sizeof(v4_route_info_table_t));
    handler->v4_route_info->shm_id = shm_id;
    handler->v4_route_info->max = count;


    /////////////////////////////////////////////////////////
    // IPv4 経路情報を共有メモリに作成
    /////////////////////////////////////////////////////////
//    shm_id = shmget(ftok(handler->conf->filename, 'E'),
//              (sizeof(m46e_v4_route_info_t) * count), IPC_CREAT);
    shm_id = shmget(IPC_PRIVATE, (sizeof(m46e_v4_route_info_t) * count), IPC_CREAT);
    if (shm_id == -1) {
        m46e_logging(LOG_ERR, "v4 route shared memory allocation faulure for statistics : %s\n", strerror(errno));
        return false;
    }

    DEBUG_SYNC_LOG("v4 shmget size = %d\n", (sizeof(m46e_v4_route_info_t) * count));

    handler->v4_route_info->table = (m46e_v4_route_info_t*)shmat(shm_id, NULL, 0);
    if (handler->v4_route_info->table == (void *)-1) {
        m46e_logging(LOG_ERR, "v4 route shared memory attach failure for statistics : %s\n", strerror(errno));
        return false;
    }

    // v4経路情報の初期化
    memset(handler->v4_route_info->table, 0, (sizeof(m46e_v4_route_info_t) * count) );
    handler->v4_route_info->t_shm_id = shm_id;

    /////////////////////////////////////////////////////////
    // IPv4 経路表への排他用mutex変数の初期化
    /////////////////////////////////////////////////////////
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&handler->v4_route_info->mutex, &attr);


    DEBUG_SYNC_LOG("m46e_sync_route_initial_v4_table end.\n");

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief v4経路情報領域解放関数
//!
//! 共有メモリに確保しているv4経路情報用領域を解放する。
//!
//! @param [in] v4_route_info_table_t v4統計情報用領域のポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void  m46e_finish_v4_table(v4_route_info_table_t* v4_route_info_table)
{
    int ret;

    DEBUG_SYNC_LOG("m46e_finish_v4_table start.\n");

    /////////////////////////////////////////////////////////
    // IPv4 経路情報 共有メモリ破棄
    /////////////////////////////////////////////////////////
    ret = shmctl(v4_route_info_table->t_shm_id, IPC_RMID, NULL);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to destruct v4 route shared memory : %s\n", strerror(errno));
    }

    ret = shmdt(v4_route_info_table->table);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to detach v4 route shared memory : %s\n", strerror(errno));
    }


    /////////////////////////////////////////////////////////
    // 排他用のmutextの削除
    /////////////////////////////////////////////////////////
    pthread_mutex_destroy(&v4_route_info_table->mutex);


    /////////////////////////////////////////////////////////
    // IPv4 経路情報 テーブル 共有メモリ破棄
    /////////////////////////////////////////////////////////
    ret = shmctl(v4_route_info_table->shm_id, IPC_RMID, NULL);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to destruct v4 route shared memory : %s\n", strerror(errno));
    }

    ret = shmdt(v4_route_info_table);
    if (ret == -1) {
        m46e_logging(LOG_ERR, "fail to detach v4 route shared memory : %s\n", strerror(errno));
    }


    DEBUG_SYNC_LOG("m46e_finish_v4_table end.\n");
    return;
}

