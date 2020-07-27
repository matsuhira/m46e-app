/******************************************************************************/
/* ファイル名 : m46eapp_mng_v6_route.h                                        */
/* 機能概要   : v6経路同期 ヘッダファイル                                     */
/* 修正履歴   : 2013.07.19 Y.Shibata 新規作成                                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_MNG_V6_ROUTE_H__
#define __M46EAPP_MNG_V6_ROUTE_H__


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
bool m46e_sync_route_initial_v6_table(struct m46e_handler_t* handler);
void  m46e_finish_v6_table(v6_route_info_table_t* v6_route_info_table);

void m46e_print_route6(int fd, struct m46e_v6_route_info_t *route_info6);
void m46e_route_print_v6table(struct m46e_handler_t* handler, int fd);


#endif // __M46EAPP_MNG_V6_ROUTE_H__

