/******************************************************************************/
/* ファイル名 : m46eapp_setup.h                                               */
/* 機能概要   : ネットワーク設定クラス ヘッダファイル                         */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_SETUP_H__
#define __M46EAPP_SETUP_H__

struct m46e_handler_t;

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int m46e_setup_plane_prefix(struct m46e_handler_t* handler);
int m46e_create_network_device(struct m46e_handler_t* handler);
int m46e_delete_network_device(struct m46e_handler_t* handler);
int m46e_move_network_device(struct m46e_handler_t* handler);
int m46e_setup_stub_network(struct m46e_handler_t* handler);
int m46e_start_stub_network(struct m46e_handler_t* handler);
int m46e_setup_backbone_network(struct m46e_handler_t* handler);
int m46e_start_backbone_network(struct m46e_handler_t* handler);
int m46e_backbone_startup_script(struct m46e_handler_t* handler);
int m46e_stub_startup_script(struct m46e_handler_t* handler);
int m46e_set_mac_of_physicalDevice_to_localAddr(struct m46e_handler_t* handler);
int m46e_set_mac_of_physicalDevice(struct m46e_handler_t* handler);

#endif // __M46EAPP_SETUP_H__
