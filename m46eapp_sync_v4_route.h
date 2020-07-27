/******************************************************************************/
/* ファイル名 : m46eapp_sync_v4_route.h                                       */
/* 機能概要   : v4経路同期 ヘッダファイル                                     */
/* 修正履歴   : 2013.12.02 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_SYNC_V4_ROUTE_H__
#define __M46EAPP_SYNC_V4_ROUTE_H__


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
void* m46e_sync_route_stub_thread(void* arg);
void setInterfaceInfo(struct m46e_handler_t* handler);
void delAllInterfaceInfo(struct m46e_handler_t* handler);
bool addInterfaceInfo(struct m46e_handler_t* handler, int devidx);
bool delInterfaceInfo(struct m46e_handler_t* handler, int devidx);

#endif // __M46EAPP_SYNC_V4_ROUTE_H__

