/******************************************************************************/
/* ファイル名 : m46eapp_timer.c                                               */
/* 機能概要   : タイマ管理 ソースファイル                                     */
/* 修正履歴   : 2012.02.22 S.Yoshikawa 新規作成                               */
/*              2012.08.08 T.Maeda Phase4向けに全面改版                       */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h> 
#include <time.h>

#include "m46eapp_list.h"
#include "m46eapp_timer.h"
#include "m46eapp_log.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

////////////////////////////////////////////////////////////////////////////////
// タイマ内部管理用構造体
////////////////////////////////////////////////////////////////////////////////
//! タイマリスト用管理データ
struct timer_item
{
    timer_t         timerid;    ///< タイマID
    timer_cbfunc    cb;         ///< ユーザ登録コールバック関数
    void*           data;       ///< ユーザ登録データ
};
typedef struct timer_item timer_item;

//! タイマ管理クラス構造体
struct m46e_timer_t
{
    pthread_mutex_t mutex;        ///< タイマ管理用mutex
    m46e_list      timer_list;   ///< タイマリスト
};

//! タイマT.O時のコールバックデータ
struct timer_cb_data
{
    m46e_timer_t*  handler;  ///< タイマ管理ハンドラ
    timer_t         timerid;  ///< T.OしたタイマID
};

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static int         timer_add_item(m46e_timer_t* timer_handler, timer_item* item);
static timer_item* timer_del_item(m46e_timer_t* timer_handler, const timer_t timerid);
static void        timer_timeout_cb(union sigval st);

///////////////////////////////////////////////////////////////////////////////
//! @brief タイマ管理クラス コンストラクタ
//!
//! タイマ管理クラスの生成と初期化をおこなう。
//!
//! @param なし
//!
//! @return 生成したタイマ管理クラス
///////////////////////////////////////////////////////////////////////////////
m46e_timer_t* m46e_init_timer()
{
    DEBUG_LOG("timer init\n");

    m46e_timer_t* timer_handler = malloc(sizeof(m46e_timer_t));
    if(timer_handler != NULL){
        // タイマリスト初期化
        m46e_list_init(&timer_handler->timer_list);
        // 排他制御初期化
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
        pthread_mutex_init(&timer_handler->mutex, &attr);
    }

    return timer_handler;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief タイマ管理クラス デストラクタ
//!
//! タイマ管理クラスの解放をおこなう。
//! タイマリスト内に残っている全タイマを削除し、
//! 登録されているユーザデータの解放もおこなう。
//!
//! @param [in] timer_handler 解放するタイマ管理クラス
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_end_timer(m46e_timer_t* timer_handler)
{
    DEBUG_LOG("timer end\n");

    // 排他開始
    pthread_mutex_lock(&timer_handler->mutex);

    // タイマリストに格納されているタイマを全て解除して
    // データも全て解放する
    while(!m46e_list_empty(&timer_handler->timer_list)){
        m46e_list* node = timer_handler->timer_list.next;
        timer_item* item = node->data;
        // タイマ情報がNULL以外の場合、タイマ削除
        if(item != NULL){
            timer_delete(item->timerid);
            free(item->data);
        }
        free(item);
        m46e_list_del(node);
        free(node);
    }

    // 排他解除
    pthread_mutex_unlock(&timer_handler->mutex);

    // 排他制御終了
    pthread_mutex_destroy(&timer_handler->mutex); 
    
    free(timer_handler);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief タイマ登録関数
//!
//! タイマの登録をおこなう
//!
//! @param [in]  timer_handler   登録先タイマ管理クラス
//! @param [in]  expire_sec      タイムアウト時間(秒)
//! @param [in]  func            タイムアウト時のcallback関数
//! @param [in]  data            callback関数の引数データ(不要な場合はNULL)
//! @param [out] timerid         登録時にシステムから払い出されたタイマID
//!
//! @retval 0      正常終了
//! @retval 0以外  異常終了
///////////////////////////////////////////////////////////////////////////////
int m46e_timer_register(
    m46e_timer_t* timer_handler,
    const time_t   expire_sec,
    timer_cbfunc   func,
    void*          data,
    timer_t*       timerid
)
{
    struct sigevent   evp;                 // 動作設定構造体
    struct itimerspec ispec;               // タイマ仕様
    
    // 排他開始
    pthread_mutex_lock(&timer_handler->mutex);

    // 変数初期化
    memset(&evp,   0, sizeof(struct sigevent));
    memset(&ispec, 0, sizeof(struct itimerspec));

    // タイマ構造体のメモリ確保（コールバック時の引数となる）
    struct timer_cb_data* cb_data = malloc(sizeof(struct timer_cb_data));
    if(data == NULL){
        DEBUG_LOG("timer callback data malloc error.\n"); 
        //排他解除
        pthread_mutex_unlock(&timer_handler->mutex);
        return -1;
    }
    cb_data->handler = timer_handler;

    // タイマ作成
    evp.sigev_notify = SIGEV_THREAD;              // SIGEV_THREADを指定(sival_ptrに指定する関数を実行)
    evp.sigev_signo  = SIGRTMIN + 1;              // リアルタイムタイマーを指定
    evp.sigev_notify_function = timer_timeout_cb; // 関数ポインタ
    evp.sigev_value.sival_ptr = cb_data;          // 関数呼び出し時の引数

    if(timer_create(CLOCK_MONOTONIC, &evp, &cb_data->timerid) < 0) {
        // エラー処理
        m46e_logging(LOG_INFO, "timer create error : %s\n", strerror(errno));
        free(data);
        //排他解除
        pthread_mutex_unlock(&timer_handler->mutex);
        return -1;
    }

    // タイマ設定
    ispec.it_value.tv_sec     = expire_sec;    // 実行開始までの時間(0指定だと動作しない)
    ispec.it_value.tv_nsec    = 0;
    ispec.it_interval.tv_sec  = 0;             // 実行間隔(ワンショットタイマーであれば0)
    ispec.it_interval.tv_nsec = 0;

    if(timer_settime(cb_data->timerid, 0, &ispec, NULL) < 0){
        // エラー処理
        m46e_logging(LOG_INFO, "timer set error : %s\n", strerror(errno));
        timer_delete(cb_data->timerid);
        free(cb_data);
        //排他解除
        pthread_mutex_unlock(&timer_handler->mutex);
        return -1;
    };

    // タイマリスト格納用のデータ作成
    timer_item* item = malloc(sizeof(timer_item));
    if(item == NULL){
        DEBUG_LOG("timer list item malloc error.\n"); 
        timer_delete(cb_data->timerid);
        free(cb_data);
        //排他解除
        pthread_mutex_unlock(&timer_handler->mutex);
        return -1;
    }
    // タイマ構造体メンバ設定
    item->timerid = cb_data->timerid;
    item->cb      = func;
    item->data    = data;

    // タイマ情報追加
    if(timer_add_item(timer_handler, item) < 0){
        DEBUG_LOG("timer set max error.\n"); 
        timer_delete(cb_data->timerid);
        free(cb_data);
        free(item);
        //排他解除
        pthread_mutex_unlock(&timer_handler->mutex);
        return -1;
    }

    // タイマIDの出力設定
    if(timerid != NULL){
        *timerid = cb_data->timerid;
    }

    //排他解除
    pthread_mutex_unlock(&timer_handler->mutex);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief タイマ削除関数
//!
//! タイマの削除を行う。
//! ユーザデータが呼び元で必要なければdataにはNULLを設定する。
//! 但し、NULLを指定した場合でも、登録されたユーザデータは解放しないので
//! 登録されたユーザデータの解放は呼び元が責任を持っておこなうこと。
//!
//! @param [in]   timer_handler   削除先タイマ管理クラス
//! @param [in]   timerid         登録時に払い出されたタイマID
//! @param [out]  data            登録時に引き渡したユーザデータの格納先。
//!
//! @retval 0   タイマを削除した
//! @retval -1  タイマを削除しなかった(指定されたタイマIDが存在しない)
///////////////////////////////////////////////////////////////////////////////
int m46e_timer_cancel(m46e_timer_t* timer_handler, const timer_t timerid, void** data)
{
    int result;

    // 排他開始
    pthread_mutex_lock(&timer_handler->mutex);

    // タイマ削除
    if(timer_delete(timerid) == -1){
        DEBUG_LOG("timer delete error %s\n", strerror(errno)); 
    }

    // IDをキーにして内部データを取得する
    timer_item* item = timer_del_item(timer_handler, timerid);
    if(item != NULL){
        if(data != NULL){
            *data = item->data;
        }
        free(item);
        result = 0;
    }
    else{
        result = -1;
    }

    //排他解除
    pthread_mutex_unlock(&timer_handler->mutex);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief タイマ再設定関数
//!
//! タイマの再設定をおこなう。
//!
//! @param [in]  timer_handler   再設定先タイマ管理クラス
//! @param [in]  timerid         登録時に払い出されたタイマID
//! @param [in]  time            タイムアウト時間(秒)
//!
//! @retval 0     タイマを再設定した
//! @retval 0以外 タイマを再設定しなかった(指定されたタイマIDが存在しない)
///////////////////////////////////////////////////////////////////////////////
int m46e_timer_reset(
    m46e_timer_t* timer_handler,
    const timer_t  timerid,
    const long     time
)
{
    struct itimerspec ispec;               // タイマ仕様
    
    // 排他開始
    pthread_mutex_lock(&timer_handler->mutex);

    // 変数初期化
    memset(&ispec, 0, sizeof(struct itimerspec));

    // タイマ設定
    ispec.it_value.tv_sec     = time;    // 実行開始までの時間(0指定だと動作しない)
    ispec.it_value.tv_nsec    = 0;
    ispec.it_interval.tv_sec  = 0;       // 実行間隔(ワンショットタイマーであれば0)
    ispec.it_interval.tv_nsec = 0;

    int result = timer_settime(timerid, 0, &ispec, NULL);

    if(result < 0){
        // エラー処理
        m46e_logging(LOG_INFO, "timer set error : %s\n", strerror(errno));
     }

    //排他解除
    pthread_mutex_unlock(&timer_handler->mutex);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief タイマ残り時間取得関数
//!
//! タイマ満了までの残り時間の取得をおこなう
//!
//! @param [in]  timer_handler   取得先タイマ管理クラス
//! @param [in]  timerid         登録時に払い出されたタイマID
//! @param [out] curr_value      タイマ満了までの残り時間
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
int m46e_timer_get(
    m46e_timer_t*     timer_handler,
    const timer_t      timerid,
    struct itimerspec* curr_value
)
{
    // タイマ取得
    return timer_gettime(timerid, curr_value);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief タイマリスト追加関数
//!
//! 内部のタイマリストにデータを追加する。
//!
//! @param [in]  timer_handler   追加先タイマ管理クラス
//! @param [in]  item            追加するデータ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int timer_add_item(m46e_timer_t* timer_handler, timer_item* item)
{
    int result;

    DEBUG_LOG("%s start\n", __func__);

    m46e_list* node = malloc(sizeof(m46e_list));
    if(node != NULL){
        m46e_list_init(node);
        m46e_list_add_data(node, item);
        m46e_list_add_tail(&timer_handler->timer_list, node);
        result = 0;
    }
    else{
        m46e_logging(LOG_WARNING, "fail to add timer item to timer list\n");
        result = -1;
    }

    DEBUG_LOG("%s end. return %d\n", __func__, result);
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief タイマリスト削除関数
//!
//! 内部のタイマリストからデータを削除する。
//!
//! @param [in]  timer_handler   削除先タイマ管理クラス
//! @param [in]  timerid         削除するデータのタイマID
//!
//! @return 削除したデータ(タイマIDに対応するデータが無ければNULL)
///////////////////////////////////////////////////////////////////////////////
static timer_item* timer_del_item(m46e_timer_t* timer_handler, const timer_t timerid)
{
    timer_item* result;

    DEBUG_LOG("%s start\n", __func__);

    m46e_list* node = NULL;
    m46e_list* iter;
    m46e_list_for_each(iter, &timer_handler->timer_list){
        timer_item* item = iter->data;
        if((item != NULL) && (timerid == item->timerid)){
            node = iter;
            break;
        }
    }

    if(node != NULL){
        m46e_list_del(node);
        result = node->data;
        free(node);
    }
    else{
        result = NULL;
    }

    DEBUG_LOG("%s end. return %p\n", __func__, result);
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief タイムアウト時のコールバック関数
//!
//! システムタイマT.O時に呼ばれるコールバック関数。
//! タイマIDに対応するユーザコールバック関数を呼び出す
//!
//! @param [in] st   タイマ登録時に設定した情報ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void timer_timeout_cb(union sigval st)
{
    struct timer_cb_data* cb_data = (struct timer_cb_data*)st.sival_ptr;

    if(cb_data == NULL){
        DEBUG_LOG("timeout param error.\n"); 
        return;
    }

    // 排他開始
    pthread_mutex_lock(&cb_data->handler->mutex);

    // タイマ削除
    if(timer_delete(cb_data->timerid) == -1){
        DEBUG_LOG("timer delete error %s\n", strerror(errno));
    }

    timer_item* item = timer_del_item(cb_data->handler, cb_data->timerid);

    //排他解除
    pthread_mutex_unlock(&cb_data->handler->mutex);

    // 登録しているコールバック関数呼び出し
    if((item != NULL) && item->cb != NULL){
        item->cb(item->timerid, item->data);
    }
    
    free(item);
    free(cb_data);

    return;
}

