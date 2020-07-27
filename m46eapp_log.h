/******************************************************************************/
/* ファイル名 : m46eapp_log.h                                                 */
/* 機能概要   : ログ管理 ヘッダファイル                                       */
/* 修正履歴   : 2012.01.05 M.Nagano 新規作成                                  */
/*              2012.07.11 T.Maeda  Phase4向けに全面改版                      */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#ifndef __M46EAPP_LOG_H__
#define __M46EAPP_LOG_H__

#include <syslog.h>
#include <stdbool.h>
#include <stdarg.h>

// 経路同期用デバッグマクロを追加

// デバッグログ用マクロ
// デバッグログではファイル名とライン数をメッセージの前に自動的に表示する
#ifdef DEBUG_SYNC
    #define   DEBUG_SYNC_LOG(...) _DEBUG_SYNC_LOG(__FILE__, __LINE__, __VA_ARGS__)
    #define  _DEBUG_SYNC_LOG(FILE, LINE, ...) __DEBUG_SYNC_LOG(FILE, LINE, __VA_ARGS__)
    #define __DEBUG_SYNC_LOG(FILE, LINE, ...) m46e_logging(LOG_DEBUG, FILE "(" #LINE ") " __VA_ARGS__)
#else
    #define   DEBUG_SYNC_LOG(...)
#endif

    #define   DEBUG_LOG(...) _DEBUG_LOG(__FILE__, __LINE__, __VA_ARGS__)
    #define  _DEBUG_LOG(FILE, LINE, ...) __DEBUG_LOG(FILE, LINE, __VA_ARGS__)
    #define __DEBUG_LOG(FILE, LINE, ...) m46e_logging(LOG_DEBUG, FILE "(" #LINE ") " __VA_ARGS__)

// デバッグ用マクロ
#ifdef DEBUG_SYNC
    #define _DS_(x) x
#else
    #define _DS_(x)
#endif

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
void m46e_initial_log(const char* name, const bool debuglog);
void m46e_logging(const int priority, const char *message, ...);


#endif // __M46EAPP_LOG_H__
