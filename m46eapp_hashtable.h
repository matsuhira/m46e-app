/******************************************************************************/
/* ファイル名 : m46eapp_hashtable.h                                           */
/* 機能概要   : ハッシュテーブルクラス ヘッダファイル                         */
/* 修正履歴   : 2012.02.20 T.Maeda 新規作成                                   */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_HASHTABLE_H__
#define __M46EAPP_HASHTABLE_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

//! ハッシュテーブルの要素構造体
typedef struct _m46e_hash_cell_t m46e_hash_cell_t;
//! ハッシュテーブル構造体
typedef struct _m46e_hashtable_t m46e_hashtable_t;

//! ユーザ指定ハッシュ要素コピー関数
typedef void* (*m46e_hash_copy_func)(const void* src, const size_t size);
//! ユーザ指定ハッシュ要素削除関数
typedef void  (*m46e_hash_delete_func)(void* obj);
//! ユーザ指定ハッシュ要素出力関数
typedef void  (*m46e_hash_foreach_cb)(const char* key, const void* value, void* userdata);

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
m46e_hashtable_t* m46e_hashtable_create(const uint32_t table_size);
void  m46e_hashtable_delete(m46e_hashtable_t* table);
bool  m46e_hashtable_add(m46e_hashtable_t* table, const char* key, const void* value, const size_t value_size, const bool overwrite, m46e_hash_copy_func copy_func, m46e_hash_delete_func delete_func);
bool  m46e_hashtable_remove(m46e_hashtable_t* table, const char* key, void** value);
void* m46e_hashtable_get(m46e_hashtable_t* table, const char* key);
void  m46e_hashtable_clear(m46e_hashtable_t* table);
void  m46e_hashtable_foreach(m46e_hashtable_t* table, m46e_hash_foreach_cb callback, void* userdata);

#endif // __M46EAPP_HASHTABLE_H__
