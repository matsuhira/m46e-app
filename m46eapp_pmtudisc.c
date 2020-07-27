/******************************************************************************/
/* ファイル名 : m46eapp_pmtudisc.c                                            */
/* 機能概要   : Path MTU Discovery管理 ソースファイル                         */
/* 修正履歴   : 2012.02.22 S.Yoshikawa 新規作成                               */
/*              2012.08.08 T.Maeda Phase4向けに全面改版                       */
/*              2013.08.30 H.Koganemaru 動的定義変更機能追加                  */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <linux/rtnetlink.h>

#include "m46eapp.h"
#include "m46eapp_pmtudisc.h"
#include "m46eapp_config.h"
#include "m46eapp_util.h"
#include "m46eapp_log.h"
#include "m46eapp_timer.h"
#include "m46eapp_hashtable.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

//! デフォルトのPMTUテーブルのキー
#define PATH_MTU_DEFAULT_KEY   "default"

////////////////////////////////////////////////////////////////////////////////
// Path MTU管理用 内部構造体
////////////////////////////////////////////////////////////////////////////////
//! PMTUテーブルに登録するデータ構造体
struct path_mtu_data
{
    int        mtu;       ///< MTUサイズ
    timer_t    timerid;   ///< 対応するタイマID
};
typedef struct path_mtu_data path_mtu_data;

//! PMTU管理構造体
struct m46e_pmtud_t
{
    m46e_config_pmtud_t*  conf;             ///< PMTU関連設定
    int                    default_mtu;      ///< MTUサイズのデフォルト値
    pthread_mutex_t        mutex;            ///< PMTU用mutex
    m46e_hashtable_t*     table;            ///< PMTU管理テーブル
    m46e_timer_t*         timer_handler;    ///< PMTU管理用タイマハンドラ
};

//! PMTUタイマT.Oコールバックデータ
struct pmtu_timer_cb_data_t
{
    m46e_pmtud_t* handler;                    ///< PMTU管理
    char           dst_addr[INET6_ADDRSTRLEN]; ///< T.Oしたデータの送信先アドレス
};
typedef struct pmtu_timer_cb_data_t pmtu_timer_cb_data_t;

//! PMTUテーブル表示用データ
struct pmtu_print_data
{
    m46e_timer_t* timer_handler;  ///< タイマハンドラ
    int            fd;             ///< 表示データ書き込み先ディスクリプタ
};
typedef struct pmtu_print_data pmtu_print_data;

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static void pmtud_timeout_cb(const timer_t timerid, void* data);
static void pmtu_print_table_line(const char* key, const void* value, void* userdata);

///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery初期化関数
//!
//! Config情報の取得、テーブル作成を行う。
//!
//! @param [in]  config       config情報
//! @param [in]  default_mtu  MTUサイズのデフォルト値
//!
//! @return 生成したPMTU管理クラスへのポインタ
///////////////////////////////////////////////////////////////////////////////
m46e_pmtud_t*  m46e_init_pmtud(m46e_config_pmtud_t* config, int default_mtu)
{
    DEBUG_LOG("pmtud init\n");

    m46e_pmtud_t* handler = malloc(sizeof(m46e_pmtud_t));

    if(handler == NULL){
        return NULL;
    }

    // hash table作成(MTU長保持用)
    handler->table = m46e_hashtable_create(128);
    if(handler->table == NULL){
        free(handler);
        return NULL;
    }

    // デフォルトMTUをテーブルに格納
    path_mtu_data data = { .mtu = default_mtu, .timerid = NULL };
    bool res = m46e_hashtable_add(handler->table, PATH_MTU_DEFAULT_KEY, &data, sizeof(data), false, NULL, NULL);
    if(!res){
        m46e_hashtable_delete(handler->table);
        free(handler);
        return NULL;
    }

    // timer作成
    handler->timer_handler = m46e_init_timer();
    if(handler->timer_handler == NULL){
        m46e_hashtable_delete(handler->table);
        free(handler);
        return NULL;
    }

    // config情報保持
    handler->conf        = config;
    handler->default_mtu = default_mtu;

    // 排他制御初期化
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&handler->mutex, &attr);

    return handler;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery終了関数
//!
//! テーブルの解放を行う。
//!
//! @param [in]  pmtud_handler PMTU管理
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_end_pmtud(m46e_pmtud_t* pmtud_handler){

    DEBUG_LOG("pmtud end\n");
 
    // 排他開始
    pthread_mutex_lock(&pmtud_handler->mutex);

    // timer解除
    m46e_end_timer(pmtud_handler->timer_handler);

    // hash table削除
    m46e_hashtable_delete(pmtud_handler->table);

    // 排他解除
    pthread_mutex_unlock(&pmtud_handler->mutex);
    
    // 排他制御終了
    pthread_mutex_destroy(&pmtud_handler->mutex); 

    free(pmtud_handler);

    return;
}
///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery再起動関数
//!
//! テーブルの再起動を行う。
//!
//! @param [in]  pmtud_handler PMTU管理
//! @param [in]  default_mtu  MTUサイズのデフォルト値
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
m46e_pmtud_t*  m46e_restart_pmtud(m46e_pmtud_t* pmtud_handler,
                                                   int default_mtu, int type)
{
    DEBUG_LOG("pmtud restart\n");

    // 排他開始
    pthread_mutex_lock(&pmtud_handler->mutex);

    // timer解除
    m46e_end_timer(pmtud_handler->timer_handler);

    // hash table削除
    m46e_hashtable_delete(pmtud_handler->table);

    // hash table作成(MTU長保持用)
    pmtud_handler->table = m46e_hashtable_create(128);
    if(pmtud_handler->table == NULL){
        m46e_logging(LOG_ERR, "pmtud_table set failed.\n");
        // 排他解除
        pthread_mutex_unlock(&pmtud_handler->mutex);
        return NULL;
    }

    // デフォルトMTUをテーブルに格納
    path_mtu_data data = { .mtu = default_mtu, .timerid = NULL };
    bool res = m46e_hashtable_add(pmtud_handler->table, PATH_MTU_DEFAULT_KEY, &data, sizeof(data), false, NULL, NULL);
    if(!res){
        m46e_hashtable_delete(pmtud_handler->table);
        m46e_logging(LOG_ERR, "pmtud_mtu set failed.\n");
        // 排他解除
        pthread_mutex_unlock(&pmtud_handler->mutex);
        return NULL;
    }

    // timer作成
    pmtud_handler->timer_handler = m46e_init_timer();
    if(pmtud_handler->timer_handler == NULL){
        m46e_hashtable_delete(pmtud_handler->table);
        m46e_logging(LOG_ERR, "pmtud_timer set failed.\n");
        // 排他解除
        pthread_mutex_unlock(&pmtud_handler->mutex);
        return NULL;
    }

    // handler情報を更新
    pmtud_handler->conf->type = type;

    // 排他解除
    pthread_mutex_unlock(&pmtud_handler->mutex);

    return pmtud_handler;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU 設定関数
//!
//! 受信したICMPv6エラーパケット(packet too big)のMTU長を保持する
//!
//! @param [in]     pmtud_handler PMTU管理
//! @param [in]     dst           オリジナルパケットの宛先アドレス
//! @param [in]     pmtu          ICMPメッセージ内のMTU長
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_path_mtu_set(
    m46e_pmtud_t*     pmtud_handler,
    struct in6_addr*   dst,
    int                pmtu
)
{
    char dst_addr[INET6_ADDRSTRLEN];
    int  result;

    DEBUG_LOG("pmtu discovery. pmtu = %d\n",pmtu);

    if(pmtud_handler->conf->type == M46E_PMTUD_TYPE_NONE){
        // Path MTU無しの場合は何もせずにリターン
        return;
    }

    // 排他開始
    pthread_mutex_lock(&pmtud_handler->mutex);

    if(pmtud_handler->conf->type == M46E_PMTUD_TYPE_HOST){
        // ホスト毎の保持の場合、IPv6アドレスを文字列変換
        inet_ntop(AF_INET6, dst, dst_addr, sizeof(dst_addr));
    }
    else{
        // それ以外(トンネル毎)の場合はデフォルトキーで検索
        strcpy(dst_addr, PATH_MTU_DEFAULT_KEY);
    }

    // pmtuサイズチェック
    pmtu = max(pmtu, IPV6_MIN_MTU);
    
    // pmtu保持テーブルから対象の情報を取得
    path_mtu_data* pmtu_data = m46e_hashtable_get(pmtud_handler->table, dst_addr);

    if(pmtu_data != NULL){
        // 一致する情報がある場合

        if(pmtu < pmtu_data->mtu){
            // MTU値が現在値よりも小さいのでMTU値を更新してタイマを再設定
            DEBUG_LOG("pmtu_info chg. dst(%s) pmtu(%d->%d)\n", dst_addr, pmtu_data->mtu, pmtu);
            // 保持データを変更
            pmtu_data->mtu = pmtu;

            if(pmtu_data->timerid != NULL){
                // タイマが起動中の場合は再設定
                result = m46e_timer_reset(
                    pmtud_handler->timer_handler,
                    pmtu_data->timerid,
                    pmtud_handler->conf->expire_time
                );
            }
            else{
                // タイマが起動中で無い場合はタイマ起動
                pmtu_timer_cb_data_t* cb_data = malloc(sizeof(pmtu_timer_cb_data_t));
                if(cb_data != NULL){
                    cb_data->handler = pmtud_handler;
                    strcpy(cb_data->dst_addr, dst_addr);

                    result = m46e_timer_register(
                        pmtud_handler->timer_handler,
                        pmtud_handler->conf->expire_time,
                        pmtud_timeout_cb,
                        cb_data,
                        &pmtu_data->timerid
                    );
                }
                else{
                    m46e_logging(LOG_WARNING, "fail to allocate timer callback data\n");
                    result = -1;
                }
            }
            if(result != 0){
                m46e_logging(LOG_WARNING, "fail to reset timer\n");
                // タイマを停止に設定
                pmtu_data->timerid = NULL;
            }
        }
        else{
            // MTU値が現在値よりも大きいので無視。
            DEBUG_LOG("Received PMTU(%d) is larger than saved PMTU(%d).\n", pmtu, pmtu_data->mtu);
        }
    }
    else{
        // 一致する情報がない場合

        // 新規追加データ設定
        path_mtu_data data = { .mtu = pmtu, .timerid = NULL };

        // タイマアウト時に通知される情報のメモリ確保
        pmtu_timer_cb_data_t* cb_data = malloc(sizeof(pmtu_timer_cb_data_t));
        if(cb_data != NULL){
            cb_data->handler = pmtud_handler;
            strcpy(cb_data->dst_addr, dst_addr);

            // PMTU保持タイマ登録
            result = m46e_timer_register(
                pmtud_handler->timer_handler,
                pmtud_handler->conf->expire_time,
                pmtud_timeout_cb,
                cb_data,
                &data.timerid
            );
        }
        else{
            m46e_logging(LOG_WARNING, "fail to allocate timer callback data\n");
            result = -1;
        }

        if(result == 0){
            // データ追加処理
            if(m46e_hashtable_add(pmtud_handler->table, dst_addr, &data, sizeof(data), false, NULL, NULL)){
                DEBUG_LOG("pmtu_info add. dst(%s) pmtu(%d) timer(%p)\n", dst_addr, data.mtu, data.timerid);
                result = 0;
            }
            else{
                m46e_logging(LOG_WARNING, "pmtud_data add failed.\n");
                // 既存のタイマ解除
                m46e_timer_cancel(pmtud_handler->timer_handler, data.timerid, NULL);
                result = -1;
            }
        }

        if(result != 0){
            m46e_logging(LOG_INFO, "pmtud_timer set failed.\n");
            // コールバックデータ解放
            free(cb_data);
        }
    }

    // 排他解除
    pthread_mutex_unlock(&pmtud_handler->mutex);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief PMTU値取得関数
//!
//! PMTU値を取得する
//!
//! @param [in]     pmtud_handler    PMTU管理
//! @param [in]     v6daddr          宛先アドレス
//!
//! @return  Path MTU値
///////////////////////////////////////////////////////////////////////////////
int m46e_path_mtu_get(
    m46e_pmtud_t*         pmtud_handler,
    const struct in6_addr* v6daddr
)
{
    char            dst_addr[INET6_ADDRSTRLEN];
    path_mtu_data*  data;
    int             result;

    // パラメタチェック
    if((pmtud_handler == NULL) || (v6daddr == NULL)){
        m46e_logging(LOG_INFO, "pmtud_get param failed.\n");
        DEBUG_LOG("pmtud_get param failed.\n");
        return -1;
    }

    // 排他開始
    pthread_mutex_lock(&pmtud_handler->mutex);
    
    result = -1;

    // テーブル情報ログ出力
    //_D_(m46e_pmtu_print_table(pmtud_handler, STDOUT_FILENO);)

    switch(pmtud_handler->conf->type){
    case M46E_PMTUD_TYPE_HOST: // ホスト毎の場合
        // IPv6アドレス文字列変換
        inet_ntop(AF_INET6, v6daddr, dst_addr, sizeof(dst_addr));
        
        // v6アドレスをkeyにデータ検索
        data = m46e_hashtable_get(pmtud_handler->table, dst_addr);
        if(data != NULL){
            result = data->mtu;
            DEBUG_LOG("pmtu_info get. dst(%s)--->pmtu %d\n", dst_addr, result);
            break;
        }

        // 見つからなかった場合はデフォルトで再度検索するので、このまま継続
 
    default:
        // デフォルトキーでデータ検索
        data = m46e_hashtable_get(pmtud_handler->table, PATH_MTU_DEFAULT_KEY);
        if(data != NULL){
            result = data->mtu;
            DEBUG_LOG("pmtu_info get. dst(%s)--->pmtu %d\n", PATH_MTU_DEFAULT_KEY, result);
            break;
        }

        break;
    }

    if((0 < result) && (result < IPV6_MIN_MTU)){
        result = IPV6_MIN_MTU;
        DEBUG_LOG("pmtu_info change. IPV6_MIN_MTU---> %d\n", result);
    }
    DEBUG_LOG("result pmtu = %d\n", result);

    // 排他解除
    pthread_mutex_unlock(&pmtud_handler->mutex);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief PMTU保持タイムアウトコールバック関数
//!
//! PMTU値を初期値に戻す
//!
//! @param [in]     timerid   タイマ登録時に払い出されたタイマID
//! @param [in]     data      タイマ登録時に設定したデータ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void pmtud_timeout_cb(const timer_t timerid, void* data)
{
    pmtu_timer_cb_data_t* cb_data = (pmtu_timer_cb_data_t*)data;

    if(cb_data == NULL){
        DEBUG_LOG("path mtu timeout param error.\n");
        return;
    }

    // 排他開始
    pthread_mutex_lock(&cb_data->handler->mutex);

    path_mtu_data* pmtu_data = m46e_hashtable_get(cb_data->handler->table, cb_data->dst_addr);

    if(data != NULL){
        if(!strcmp(PATH_MTU_DEFAULT_KEY, cb_data->dst_addr)){
            // デフォルトの場合はデータを削除せずに初期値に戻す
            pmtu_data->mtu     = cb_data->handler->default_mtu;
            pmtu_data->timerid = NULL;
        }
        else{
            // 期限切れ対象データを削除
            DEBUG_LOG("pmtu_info del. dst(%s)\n", cb_data->dst_addr);
            if(pmtu_data->timerid != timerid){
                // timeridの値が異なる場合、警告表示(データは一応削除する)
                m46e_logging(LOG_WARNING, "callback timerid different in path mtu table\n");
                m46e_logging(LOG_WARNING,
                    "  callback = %d, path mtu table = %d, addr = %s\n",
                    timerid, pmtu_data->timerid, cb_data->dst_addr
                );
            }
            m46e_hashtable_remove(cb_data->handler->table, cb_data->dst_addr, NULL);
        }
    }
    else{
        m46e_logging(LOG_WARNING, "path mtu data is not found. addr = %s\n", cb_data->dst_addr);
    }

    // 排他解除
    pthread_mutex_unlock(&cb_data->handler->mutex);
    
    free(cb_data);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル内部情報出力関数
//!
//! PMTU管理内のテーブルを出力する
//!
//! @param [in]     pmtud_handler   PMTU管理
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_pmtu_print_table(m46e_pmtud_t* pmtud_handler, int fd)
{
    pthread_mutex_lock(&pmtud_handler->mutex);

    pmtu_print_data data = {
        .timer_handler = pmtud_handler->timer_handler,
        .fd            = fd
    };

    dprintf(fd, "\n");
    dprintf(fd, "                   Dst Addr                   | Path MTU | remain time \n");
    dprintf(fd, "----------------------------------------------+----------+-------------\n");
    m46e_hashtable_foreach(pmtud_handler->table, pmtu_print_table_line, &data);
    dprintf(fd, "\n");

    pthread_mutex_unlock(&pmtud_handler->mutex);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル出力コールバック関数
//!
//! ハッシュテーブルログ出力時にコールバック登録する関数
//!
//! @param [in]     key       テーブルに登録されているキー
//! @param [in]     value     キーに対応する値
//! @param [in]     userdata  コールバック登録時に指定したユーザデータ
//!                           (pmtu_print_data構造体)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void pmtu_print_table_line(const char* key, const void* value, void* userdata)
{
    // 引数チェック
    if(value == NULL || userdata == NULL){
        return;
    }

    pmtu_print_data* print_data = (pmtu_print_data*)userdata;
    path_mtu_data*   data       = (path_mtu_data*)value;

    // 残り時間出力
    struct itimerspec tmspec;
    if(data->timerid != NULL){
        m46e_timer_get(print_data->timer_handler, data->timerid, &tmspec);
    }
    else{
        tmspec.it_value.tv_sec = -1;
    }

    // mtu長出力
    dprintf(print_data->fd, "%-46s|%10d|%12ld\n", key, data->mtu, tmspec.it_value.tv_sec);

    return;
}

