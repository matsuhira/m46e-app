/******************************************************************************/
/* ファイル名 : m46eapp_command.h                                             */
/* 機能概要   : 内部コマンドクラス ヘッダファイル                             */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2013.07.08 Y.Shibata 動的定義変更機能追加                     */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_COMMAND_H__
#define __M46EAPP_COMMAND_H__

#include <stdbool.h>
#include "m46eapp_command_data.h"

struct m46e_handler_t;

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
bool m46e_command_init(struct m46e_handler_t* handler);
void m46e_command_init_parent(struct m46e_handler_t* handler);
void m46e_command_init_child(struct m46e_handler_t* handler);
bool m46e_command_wait_parent(struct m46e_handler_t* handler, enum m46e_command_code code);
bool m46e_command_wait_child(struct m46e_handler_t* handler, enum m46e_command_code code);
bool m46e_command_sync_parent(struct m46e_handler_t* handler, enum m46e_command_code code);
bool m46e_command_sync_child(struct m46e_handler_t* handler, enum m46e_command_code code);
int  m46e_command_send_request(struct m46e_handler_t* handler, struct m46e_command_t* command);
int  m46e_command_send_response(struct m46e_handler_t* handler, struct m46e_command_t* command);
int  m46e_command_recv_request(struct m46e_handler_t* handler, struct m46e_command_t* command);
int  m46e_command_recv_response(struct m46e_handler_t* handler, struct m46e_command_t* command);
int m46e_command_wait_child_with_result(struct m46e_handler_t* handler, enum m46e_command_code code);
bool m46e_command_sync_parent_with_result(struct m46e_handler_t* handler, enum m46e_command_code code, int ret);
bool m46e_sync_route_command_init(struct m46e_handler_t* handler);
void m46e_sync_route_command_init_child(struct m46e_handler_t* handler);
int m46e_send_sync_route_request_from_stub(struct m46e_handler_t* handler, struct m46e_command_t* command);
void m46e_sync_route_command_init_parent(struct m46e_handler_t* handler);
int m46e_send_sync_route_request_from_bb(struct m46e_handler_t* handler, struct m46e_command_t* command);

#endif // __M46EAPP_COMMAND_H__

