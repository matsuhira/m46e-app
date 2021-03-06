/******************************************************************************/
/* ファイル名 : m46eapp_stub_network.h                                        */
/* 機能概要   : Stubネットワーククラス ヘッダファイル                         */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_STUB_NETWORK_H__
#define __M46EAPP_STUB_NETWORK_H__

#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
pid_t m46e_stub_nw_clone(void* data);

#endif // __M46EAPP_STUB_NETWORK_H__
