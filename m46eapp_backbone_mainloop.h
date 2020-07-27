/******************************************************************************/
/* ファイル名 : m46eapp_backbone_mainloop.h                                   */
/* 機能概要   : Backboneネットワークメインループ関数 ヘッダファイル           */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_BACKBONE_MAINLOOP_H__
#define __M46EAPP_BACKBONE_MAINLOOP_H__

struct m46e_handler_t;

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int m46e_backbone_mainloop(struct m46e_handler_t* handler);

#endif // __M46EAPP_BACKBONE_MAINLOOP_H__
