/******************************************************************************/
/* ファイル名 : m46eapp_dynamic_setting.h                                     */
/* 機能概要   : 動的定義変更 ヘッダファイル                                   */
/* 修正履歴   : 2013.07.08 Y.Shibata 新規作成                                 */
/*              2013.09.04 H.Koganemaru 動的定義変更機能追加                  */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __M46EAPP_DYNAMIC_SETTING_H__
#define __M46EAPP_DYNAMIC_SETTING_H__

#include <stdbool.h>
#include <m46eapp.h>

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
extern void m46eapp_set_flag_restart(bool flg);
extern bool m46eapp_get_flag_restart(void);
extern bool m46eapp_backbone_add_device(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_backbone_del_device(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_stub_add_device(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_stub_del_device(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_backbone_set_debug_log(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_stub_set_debug_log(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_set_pmtud_expire(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_set_pmtud_type_bb(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_set_pmtud_type_stub(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_set_force_fragment(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_backbone_set_default_gw(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_stub_set_default_gw(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_backbone_set_tunnel_mtu(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_stub_set_tunnel_mtu(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_backbone_set_device_mtu(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_stub_set_device_mtu(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd);
extern bool m46eapp_backbone_exec_cmd(struct m46e_handler_t* handler, struct m46e_command_t* command);
extern bool m46eapp_stub_exec_cmd(struct m46e_handler_t* handler, struct m46e_command_t* command);

#endif // __M46EAPP_DYNAMIC_SETTING_H__

