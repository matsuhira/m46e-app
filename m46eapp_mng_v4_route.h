/******************************************************************************/
/* ファイル名 : m46eapp_mng_v4_route.h                                        */
/* 機能概要   : v4経路管理 ヘッダファイル                                     */
/* 修正履歴   : 2013.06.06 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_MNG_V4_ROUTE_H__
#define __M46EAPP_MNG_V4_ROUTE_H__


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
bool m46e_sync_route_initial_v4_table(struct m46e_handler_t* handler);
void m46e_finish_v4_table(v4_route_info_table_t* v4_route_info_table);

void m46e_print_route(int fd, struct m46e_v4_route_info_t *route_info);
void m46e_route_print_v4table(struct m46e_handler_t* handler, int fd);

#endif // __M46EAPP_MNG_V4_ROUTE_H__

