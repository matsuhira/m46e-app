/******************************************************************************/
/* ファイル名 : m46eapp_timer.h                                               */
/* 機能概要   : タイマ管理ヘッダファイル                                      */
/* 修正履歴   : 2012.03.06 S.Yoshikawa 新規作成                               */
/*              2012.08.08 T.Maeda Phase4向けに全面改版                       */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_TIMER_H__
#define __M46EAPP_TIMER_H__

#include <time.h>

////////////////////////////////////////////////////////////////////////////////
// Timer管理構造体
////////////////////////////////////////////////////////////////////////////////
typedef struct m46e_timer_t m46e_timer_t;

//! タイムアウト時コールバック関数
typedef void (*timer_cbfunc)(const timer_t timerid, void* data);

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
// タイマ初期化関数
m46e_timer_t* m46e_init_timer(void);

// タイマ終了関数
void m46e_end_timer(m46e_timer_t* timer_handler);

// タイマ登録関数
int m46e_timer_register(m46e_timer_t* timer_handler, const time_t expire_sec, timer_cbfunc cb, void* data, timer_t* timerid);

// タイマ停止関数
int m46e_timer_cancel(m46e_timer_t* timer_handler, const timer_t timerid, void** data);

// タイマ再設定関数
int m46e_timer_reset(m46e_timer_t* timer_handler, const timer_t timerid, const long time);

// タイマ情報取得関数
int m46e_timer_get(m46e_timer_t* timer_handler, const timer_t timerid, struct itimerspec* curr_value);

#endif // __M46EAPP_TIMER_H__
