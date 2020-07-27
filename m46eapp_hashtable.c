/******************************************************************************/
/* ファイル名 : m46eapp_hashtable.h                                           */
/* 機能概要   : ハッシュテーブルクラス ソースファイル                         */
/* 修正履歴   : 2012.02.20 T.Maeda 新規作成                                   */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m46eapp_hashtable.h"
#include "m46eapp_log.h"

////////////////////////////////////////////////////////////////////////////////
//! ハッシュテーブルの要素構造体
////////////////////////////////////////////////////////////////////////////////
struct _m46e_hash_cell_t
{
    char*                   key;         ///< キー
    void*                   value;       ///< データ
    m46e_hash_delete_func  delete_func; ///< データ削除用関数
    m46e_hash_cell_t*      prev;        ///< 前のセルへのチェイン
    m46e_hash_cell_t*      next;        ///< 次のセルへのチェイン
};

////////////////////////////////////////////////////////////////////////////////
//! ハッシュテーブル構造体
////////////////////////////////////////////////////////////////////////////////
struct _m46e_hashtable_t
{
    m46e_hash_cell_t** cells;       ///! 各要素の配列
    uint32_t            count;       ///! 格納されている要素数
    uint32_t            table_size;  ///! テーブルのサイズ
};


////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static uint32_t hashtable_calc_hash(m46e_hashtable_t* table, const char* key);

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル 生成関数
//!
//! ハッシュテーブルのコンストラクタ。
//! 引数のテーブルサイズで指定されたサイズでハッシュテーブルを生成する。<br/>
//! テーブルの解放には必ずm46e_hashtable_create関数を使用すること。
//!
//! @param [in]     table_size   生成するハッシュテーブルのサイズ
//!
//! @return 生成したハッシュテーブルへのポインタ
///////////////////////////////////////////////////////////////////////////////
m46e_hashtable_t* m46e_hashtable_create(const uint32_t table_size)
{
    // ローカル変数宣言
    m46e_hashtable_t* result;

    // 引数チェック
    if(table_size == 0){
        return NULL;
    }

    result = (m46e_hashtable_t*)malloc(sizeof(m46e_hashtable_t));
    if(result != NULL){
        result->cells = (m46e_hash_cell_t**)calloc(table_size, sizeof(m46e_hash_cell_t*));
        if(result->cells == NULL){
            free(result);
            result = NULL;
        }
        else{
            result->count      = 0;
            result->table_size = table_size;
        }
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル 解放関数
//!
//! ハッシュテーブルのデストラクタ。
//!
//! @param [in]  table   削除するハッシュテーブル
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_hashtable_delete(m46e_hashtable_t* table)
{
    // 引数チェック
    if(table != NULL){
        m46e_hashtable_clear(table);
        free(table->cells);
    }
    free(table);
}


///////////////////////////////////////////////////////////////////////////////
//! @brief 要素追加関数
//!
//! 指定されたキーとバリューをハッシュテーブルに追加する。<br/>
//! copy_funcが指定されている場合、ハッシュテーブルにはcopy_funcの戻り値を格納する。
//! copy_funcが指定されていない場合、ハッシュテーブルにはvalue_size分allocした
//! 領域にvalueをmemcpyしたものを格納する。
//! delete_funcは要素の削除時にvalueのデストラクタとして使用する。
//! delete_funcがNULLの場合、valueはfree関数で解放する。
//!
//! @param [in]  table         ハッシュテーブル
//! @param [in]  key           追加する要素のキー
//! @param [in]  value         追加する要素のバリュー
//! @param [in]  value_size    valueのサイズ(byte)
//! @param [in]  overwrite     同じキーを持つデータが既に格納されている場合に
//!                            上書きするかどうか。
//!                            (true:上書きする、false:上書きしない)
//! @param [in]  copy_func     valueのコピー用関数(コピーコンストラクタ)
//! @param [in]  delete_func   valueの削除用関数(デストラクタ)
//!
//! @retval true   要素を追加した。
//! @retval false  要素を追加しなかった (メモリ確保失敗、キーの重複など)
///////////////////////////////////////////////////////////////////////////////
bool m46e_hashtable_add(
    m46e_hashtable_t*      table,
    const char*             key,
    const void*             value,
    const size_t            value_size,
    const bool              overwrite,
    m46e_hash_copy_func    copy_func,
    m46e_hash_delete_func  delete_func
)
{
    // ローカル変数宣言
    m46e_hash_cell_t* cell_p;
    uint32_t           bucket;

    // 引数チェック
    if(table == NULL){
        return false;
    }
    if((key == NULL) || strlen(key) == 0){
        return false;
    }
    if((value == NULL) || value_size == 0){
        return false;
    }

    /* 同じキーを持つデータがないか確認する */
    if(m46e_hashtable_get(table, key) != NULL){
        if(overwrite){
            m46e_hashtable_remove(table, key, NULL);
        }
        else{
            /* 同じキーが使われているので、追加できない */
            return false;
        }
    }

    /* チェインのための領域を確保する */
    cell_p = (m46e_hash_cell_t*)malloc(sizeof(m46e_hash_cell_t));
    if(cell_p == NULL){
        return false;
    }

    // キーの文字列を複製して格納
    cell_p->key = strdup(key);
    if(cell_p->key == NULL){
        free(cell_p);
        return false;
    }

    // バリューを複製して格納
    if(copy_func != NULL){
        // コピー関数が指定されている場合はそれを呼び出す。
        cell_p->value = copy_func(value, value_size);
        if(cell_p->value == NULL){
            free(cell_p->key);
            free(cell_p);
            return false;
        }
    }
    else{
        // コピー関数が指定されていない場合は、size分allocしてmemcpyする。
        cell_p->value = malloc(value_size);
        if(cell_p->value != NULL){
            memcpy(cell_p->value, value, value_size);
        }
        else{
            free(cell_p->key);
            free(cell_p);
            return false;
        }
    }

    // 格納先のバケット(配列の添字)を計算
    bucket = hashtable_calc_hash(table, key);

    // その他のメンバの設定
    cell_p->delete_func        = delete_func;
    cell_p->prev               = NULL;
    cell_p->next               = table->cells[bucket];
    if(table->cells[bucket] != NULL){
        table->cells[bucket]->prev = cell_p;
    }
    table->cells[bucket]       = cell_p;

    // 要素数をインクリメント
    table->count++;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 要素削除関数
//!
//! 指定されたキーに該当する要素をハッシュテーブルから削除する。<br/>
//! removed_valueにNULL以外の値が指定されている場合、削除した要素のvalueを
//! そのまま格納する。(この場合、value自体の解放は呼出元の責任でおこなうこと)
//! removed_valueがNULLの場合、valueの解放は本関数内でおこなわれる。
//!
//! @param [in]  table         ハッシュテーブル
//! @param [in]  key           削除する要素のキー
//! @param [out] removed_value 削除した要素のバリューを格納する器
//!
//! @retval true   要素を削除した。
//! @retval false  要素を削除しなかった (キーに対応する要素が無い)
///////////////////////////////////////////////////////////////////////////////
bool m46e_hashtable_remove(
    m46e_hashtable_t* table,
    const char*        key,
          void**       removed_value
)
{
    // ローカル変数宣言
    bool               result;
    m46e_hash_cell_t* cell_p;
    uint32_t           bucket;

    // 引数チェック
    if(table == NULL){
        return false;
    }
    if((key == NULL) || strlen(key) == 0){
        return false;
    }

    // ローカル変数初期化
    result = false;
    bucket = hashtable_calc_hash(table, key);

    // 連結リストを辿る
    for(cell_p=table->cells[bucket]; cell_p!=NULL; cell_p=cell_p->next){
        if(!strcmp(key, cell_p->key)){
            // 連結リストのポインタを再設定
            if(cell_p->prev != NULL){
                cell_p->prev->next = cell_p->next;
            }
            if(cell_p->next != NULL){
                cell_p->next->prev = cell_p->prev;
            }

            // 削除する要素のキーを解放
            free(cell_p->key);

            // 削除する要素のバリューを解放
            if(removed_value != NULL){
                // 削除した要素のバリュー格納ポインタが指定されている場合は
                // バリューを解放せず、ポインタをそのまま設定する。
                *removed_value = cell_p->value;
            }
            else{
                // 削除した要素のバリュー格納ポインタが指定されていない場合は
                // バリューを解放する。
                if(cell_p->delete_func != NULL){
                    // 削除用関数が指定されている場合は、それを呼び出す。
                    cell_p->delete_func(cell_p->value);
                }
                else{
                    // 削除用関数が指定されていない場合は、そのままfreeする。
                    free(cell_p->value);
                }
            }
            // 連結リストの先頭であれば、リスト先頭アドレスを再設定
            if(cell_p == table->cells[bucket]){
                table->cells[bucket] = cell_p->next;
            }
            // 要素自体を解放
            free(cell_p);
            // 要素数をデクリメント
            table->count--;
            // 処理結果をtrueに設定
            result = true;
            break;
        }
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 要素検索関数
//!
//! 指定されたキーに該当する要素をハッシュテーブルから検索する。<br/>
//!
//! @param [in]  table         ハッシュテーブル
//! @param [in]  key           検索する要素のキー
//!
//! @return keyに該当する要素のvalue。要素が存在しない場合はNULL。
///////////////////////////////////////////////////////////////////////////////
void* m46e_hashtable_get(
    m46e_hashtable_t* table,
    const char*        key
)
{
    // ローカル変数宣言
    void*              result;
    m46e_hash_cell_t* cell_p;
    uint32_t           bucket;

    // 引数チェック
    if(table == NULL){
        return NULL;
    }
    if((key == NULL) || strlen(key) == 0){
        return NULL;
    }

    // ローカル変数初期化
    result = NULL;
    bucket = hashtable_calc_hash(table, key);
    
    /* 連結リストを辿る */
    for(cell_p=table->cells[bucket]; cell_p!=NULL; cell_p=cell_p->next){
        if(!strcmp(key, cell_p->key)){
            result = cell_p->value;
            break;
        }
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief テーブル初期化関数
//!
//! テーブルから全要素を削除する。<br/>
//!
//! @param [in]  table         ハッシュテーブル
//!
//! @return なし。
///////////////////////////////////////////////////////////////////////////////
void m46e_hashtable_clear(m46e_hashtable_t* table)
{
    // ローカル変数宣言
    m46e_hash_cell_t* cell_p;
    m46e_hash_cell_t* next_p;

    // 引数チェック
    if(table == NULL){
        return;
    }

    /* malloc関数で領域が確保されていたら、解放する */
    for(int i=0; i<table->table_size; ++i){
        cell_p = table->cells[i];

        /* next を辿ってから、領域を解放しなければならない */
        while(cell_p != NULL){
            next_p = cell_p->next;
            m46e_hashtable_remove(table, cell_p->key, NULL);
            cell_p = next_p;
        }
        table->cells[i] = NULL;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュ値計算関数
//!
//! keyからハッシュ値を計算する。<br/>
//! ハッシュ値はJavaのString#hashCode()と同じロジックで計算した結果を
//! テーブルサイズで割ったもの。<br/>
//! [参考] JavaのString#hashCodeアルゴリズム<br/>
//! @code
//!    s[0]*31^(n-1) + s[1]*31^(n-2) + ... + s[n-1]
//! @endcode
//!
//! @param [in]  table     ハッシュテーブル
//! @param [in]  key       ハッシュ値を求めるキー
//!
//! @return なし。
///////////////////////////////////////////////////////////////////////////////
uint32_t hashtable_calc_hash(
    m46e_hashtable_t* table,
    const char*        key
)
{
    // ローカル変数宣言
    uint32_t hash;
    int      len;

    // 引数チェック
    if((table == NULL) || (key == NULL)){
        return 0;
    }

    // ローカル変数初期化
    hash = 0;
    len  = strlen(key);

    for (int i = 0; i < len; i++){
        hash = 31 * hash + key[i];
    }

    return hash % table->table_size;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル全要素巡回関数
//!
//! テーブルの全要素に対してコールバック関数を呼び出す。<br/>
//!
//! @param [in]  table       ハッシュテーブル
//! @param [in]  callback    コールバック関数
//! @param [in]  userdata    コールバック関数の引数に渡すデータ
//!
//! @return なし。
///////////////////////////////////////////////////////////////////////////////
void m46e_hashtable_foreach(m46e_hashtable_t* table, m46e_hash_foreach_cb callback, void* userdata)
{
    // ローカル変数宣言
    m46e_hash_cell_t* cell_p;

    // 引数チェック
    if(table == NULL || callback == NULL){
        return;
    }

    for(int i=0; i<table->table_size; i++){
        cell_p = table->cells[i];
        while(cell_p != NULL){
            callback(cell_p->key, cell_p->value, userdata);
            cell_p = cell_p->next;
        }
    }
}
