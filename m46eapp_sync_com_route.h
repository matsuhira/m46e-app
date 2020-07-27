/******************************************************************************/
/* ファイル名 : m46eapp_sync_com_route.h                                      */
/* 機能概要   : 経路同期 共通ヘッダファイル                                   */
/* 修正履歴   : 2013.06.06 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_SYNC_COM_ROUTE_H__
#define __M46EAPP_SYNC_COM_ROUTE_H__

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "m46eapp_command.h"


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int m46e_rtsync_set_route(struct m46e_handler_t* handler, struct m46e_route_sync_request_t* request);
bool m46e_sync_route(int family, int mode, struct m46e_handler_t* handler, void* route, void* info);
bool m46e_prefix_check(struct m46e_handler_t* handler, struct in6_addr* ipi6_addr);
int m46e_change_route_v6_to_v4( struct m46e_handler_t* handler, struct m46e_v6_route_info_t* route_v6,
        struct m46e_v4_route_info_t* route_v4);
int m46e_change_route_v4_to_v6( struct m46e_handler_t* handler, struct m46e_v4_route_info_t* route_v4,
        struct m46e_v6_route_info_t* route_v6);

#endif // __M46EAPP_SYNC_COM_ROUTE_H__

