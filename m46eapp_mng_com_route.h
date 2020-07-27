/******************************************************************************/
/* ファイル名 : m46eapp_mng_com_route.h                                       */
/* 機能概要   : 経路管理 共通ヘッダファイル                                   */
/* 修正履歴   : 2013.06.06 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_MNG_COM_ROUTE_H__
#define __M46EAPP_MNG_COM_ROUTE_H__

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "m46eapp_command.h"


////////////////////////////////////////////////////////////////////////////////
// 外部マクロ定義
////////////////////////////////////////////////////////////////////////////////
//!経路同期：経路追加要求
#define RTSYNC_ROUTE_ADD 0
//!経路同期：経路削除要求
#define RTSYNC_ROUTE_DEL 1

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int m46e_get_route_entry(int family, void* route_info);
int m46e_update_route_info(int type, int family, struct rtmsg  *rtm, struct rtattr **tb, void *data);
int m46e_add_route(int family, void* route, void* entry);
int m46e_del_route(int family, void* route, void* entry);
int m46e_search_route(int family, void* route, void* entry);
int m46e_get_route_number(int family, void* route, void* entry);
int m46e_set_route_info(int family, struct rtmsg  *rtm, struct rtattr **tb, void *route_info);
void m46e_del_route_by_device(struct m46e_handler_t* handler, int devidx);


#endif //__M46EAPP_MNG_COM_ROUTE_H__

