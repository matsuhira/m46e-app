/******************************************************************************/
/* ファイル名 : m46eapp_pr.c                                                  */
/* 機能概要   : M46E Prefix Resolution ソースファイル                         */
/* 修正履歴   : 2013.08.01 Y.Shibata   新規作成                               */
/* 修正履歴   : 2013.08.22 H.Koganemaru M46E-PR機能拡張                       */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2014.01.21 M.Iwatsubo M46E-PR外部連携機能追加                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <limits.h>

#include "m46eapp.h"
#include "m46eapp_list.h"
#include "m46eapp_pr.h"
#include "m46eapp_log.h"
#include "m46eapp_command.h"
#include "m46eapp_pr_struct.h"
#include "m46eapp_network.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Table生成
//!
//! M46E-PR config情報からM46E-PR Tableの生成を行う。
//!
//! @param [in]  handler  M46Eハンドラ
//!
//! @return 生成したM46E-PR Tableクラスへのポインタ
///////////////////////////////////////////////////////////////////////////////
m46e_pr_table_t* m46e_pr_init_pr_table(struct m46e_handler_t* handler)
{
   // 引数チェック
   if(handler == NULL){
       m46e_logging(LOG_ERR, "Parameter Check NG.");
       return NULL;
   }

   if(handler->conf == NULL){
       return NULL;
   }

   if(handler->conf->pr_conf_table == NULL){
       return NULL;
   }

   m46e_pr_table_t* pr_table = NULL;
   pr_table = malloc(sizeof(m46e_pr_table_t));
   if(pr_table == NULL){
        m46e_logging(LOG_WARNING, "fail to allocate M46E-PR data.\n");
        return NULL;
   }

   m46e_list_init(&pr_table->entry_list);

   pr_table->num = 0;

    // 排他制御初期化
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&pr_table->mutex, &attr);

    // config情報TableからM46E-PR Tableへエントリーを追加
    m46e_list* iter;
    m46e_pr_entry_t* pr_entry;
    m46e_list_for_each(iter, &handler->conf->pr_conf_table->entry_list){
        m46e_pr_config_entry_t* pr_config_entry = iter->data;
        pr_entry = m46e_pr_conf2entry(handler, pr_config_entry);
        if(pr_entry == NULL){
            m46e_logging(LOG_ERR, "fail to convert entry.\n");
            return NULL;
        }
        m46e_pr_add_entry(pr_table, pr_entry);
    }

    return pr_table;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR情報管理(M46E-PR Table)終了関数
//!
//! テーブルの解放を行う。
//!
//! @param [in]  pr_handler M46E-PR情報管理
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void m46e_pr_destruct_pr_table(m46e_pr_table_t* pr_handler)
{
    // pr_handlerのNULLチェックは行わない。

    // 排他開始
    pthread_mutex_lock(&pr_handler->mutex);

    // M46E-PR Entry削除
    while(!m46e_list_empty(&pr_handler->entry_list)){
        m46e_list* node = pr_handler->entry_list.next;
        m46e_pr_entry_t* pr_entry = node->data;
        free(pr_entry);
        m46e_list_del(node);
        free(node);
        pr_handler->num--;
    }

    // 排他解除
    pthread_mutex_unlock(&pr_handler->mutex);

    // 排他制御終了
    pthread_mutex_destroy(&pr_handler->mutex);

    free(pr_handler);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief IPv4ネットワークアドレス変換関数
//!
//! cidrから引数のアドレスを、ネットワークアドレスへ変換する。
//!
//! @param [in]     inaddr  変換前v4address
//! @param [in]     cidr    変換に使用するv4netmask（CIDR形式）
//! @param [out]    outaddr 変換後v4address
//!
//! @return true        変換成功
//!         false       変換失敗
///////////////////////////////////////////////////////////////////////////////
bool m46eapp_pr_convert_network_addr(struct in_addr *inaddr, int cidr, struct in_addr *outaddr)
{
    struct in_addr  in_mask;

    // 引数チェック
    if ((inaddr == NULL) || (cidr < 0) || (cidr > 32) || (outaddr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_pr_convert_network_addr).");
        return false;
    }

    // IPv4のサブネットマスク長
    PR_CIDR2SUBNETMASK(cidr, in_mask);
    // IPv4アドレス(ネットワークアドレス)
    outaddr->s_addr = inaddr->s_addr & in_mask.s_addr;

    return true;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief IPv4ネットワークアドレスチェック関数
//!
//! cidrから引数のアドレスが、ネットワークアドレスか
//! （ホスト部のアドレスが0か）チェックする。
//!
//! @param [in]     addr    チェックするv4address
//! @param [in]     cidr    チェックに使用するv4netmask（CIDR形式）
//!
//! @return true        OK（ネットワークアドレスである）
//!         false       NG（ネットワークアドレスでない）
///////////////////////////////////////////////////////////////////////////////
bool m46eapp_pr_check_network_addr(struct in_addr *addr, int cidr)
{
    struct in_addr  in_mask;
    struct in_addr  in_net;

    // 引数チェック
    if ((addr == NULL) || (cidr < 0) || (cidr > 32) ) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_pr_check_network_addr).");
        return false;
    }

    // IPv4のサブネットマスク長
    PR_CIDR2SUBNETMASK(cidr, in_mask);
    // IPv4アドレス(ネットワークアドレス)
    in_net.s_addr = addr->s_addr & in_mask.s_addr;

    if (in_net.s_addr != addr->s_addr) {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Config Table出力関数（デバッグ用）
//!
//! M46E-PR Config Tableを出力する。
//!
//! @param [in] table   出力するM46E-PR Config Table
//!
//! @return none
///////////////////////////////////////////////////////////////////////////////
void m46e_pr_config_table_dump(const m46e_pr_config_table_t* table)
{
    char address[INET6_ADDRSTRLEN];
    char* strbool[] = { "disable", "enable"};

    // 引数チェック
    if (table == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_config_table_dump).");
        return;
    }

    DEBUG_LOG("table num = %d", table->num);

    m46e_list* iter;
    m46e_list_for_each(iter, &table->entry_list){
        m46e_pr_config_entry_t* entry = iter->data;
        DEBUG_LOG("%s v4address = %s/%d ",
                strbool[entry->enable],
                inet_ntop(AF_INET,  entry->v4addr, address, sizeof(address)), entry->v4cidr);
        DEBUG_LOG("v6address = %s/%d\n",
                inet_ntop(AF_INET6, entry->pr_prefix, address, sizeof(address)), entry->v6cidr);

    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Config Table追加関数
//!
//! M46E-PR Config Tableへ新規エントリーを追加する。
//! 新規エントリーは、テーブルの最後に追加する。
//! ※entryは、ヒープ領域(malloc関数などで確保したメモリ領域)を渡すこと。
//! エントリーのIPv4アドレスがネットワークアドレスがでない場合は、
//! 追加失敗とする。
//! テーブル内にIPv4アドレスとv4cidrが同一のエントリーが既にある場合は、
//! 追加失敗とする。
//!
//! @param [in/out] table   追加するM46E-PR Config Table
//! @param [in]     entry   追加するエントリー情報
//!
//! @return true        追加成功
//!         false       追加失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_add_config_entry(m46e_pr_config_table_t* table, m46e_pr_config_entry_t* entry)
{
    // 引数チェック
    if ((table == NULL) || (entry == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_add_config_entry).");
        return false;
    }

    // IPv4ネットワークアドレスチェック
    if (!m46eapp_pr_check_network_addr(entry->v4addr, entry->v4cidr)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(This is invalid address).");
        return false;
    }

    // 同一エントリー有無検索
    if(m46e_search_pr_config_table(table, entry->v4addr, entry->v4cidr) != NULL) {
        m46e_logging(LOG_ERR, "This entry is is already exists.");
        return false;
    }

    if (table->num < PR_MAX_ENTRY_NUM) {

        // 要素数のインクリメント
        table->num++;

        m46e_list* node = malloc(sizeof(m46e_list));
        if(node == NULL){
            m46e_logging(LOG_WARNING, "fail to allocate M46E-PR data.\n");
            return false;
        }

        m46e_list_init(node);
        m46e_list_add_data(node, entry);

        m46e_list_add_tail(&table->entry_list, node);

    } else {
        m46e_logging(LOG_INFO, "M46E-PR config table is enough. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Config Table削除関数
//!
//! M46E-PR Config Tableから指定されたエントリーを削除する。
//! ※エントリーが1個の場合、削除は失敗する。
//!
//! @param [in/out] table   削除するM46E-PR Config Table
//! @param [in]     addr    検索に使用するv4address
//! @param [in]     cidr    検索に使用するv4netmask（CIDR形式）
//!
//! @return true        削除成功
//!         false       削除失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_del_config_entry(m46e_pr_config_table_t* table, struct in_addr* addr, int cidr)
{
    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_del_config_entry).");
        return false;
    }

    // IPv4ネットワークアドレスチェック
    if (!m46eapp_pr_check_network_addr(addr, cidr)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(This is invalid address).");
        return false;
    }

    if (table->num > 1) {

        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_config_entry_t* tmp = iter->data;

            // アドレスとネットマスクが一致するエントリーを検索
            if ((addr->s_addr == tmp->v4addr->s_addr) && (cidr == tmp->v4cidr)) {
                DEBUG_LOG("match M46E-PR Table.\n");

                // 一致したエントリーを削除
                free(tmp->v4addr);
                free(tmp->pr_prefix);
                free(tmp);
                m46e_list_del(iter);
                free(iter);

                // 要素数のディクリメント
                table->num--;

                break;
            }
        }

        // 検索に失敗した場合、ログを残す
        if (iter == &table->entry_list) {
            char address[INET_ADDRSTRLEN];
            m46e_logging(LOG_INFO, "Don't match M46E-PR Table. address = %s/%d\n",
                    inet_ntop(AF_INET, addr, address, sizeof(address)), cidr);
            return false;
        }

    } else if (table->num == 1) {
        m46e_logging(LOG_INFO, "M46E-PR config table is only one entry.\n");
        return false;
    } else {
        m46e_logging(LOG_INFO, "M46E-PR config table is no-entry. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Table 有効/無効設定関数
//!
//! M46E-PR Tableから指定されたエントリーの有効/無効を設定する。
//!
//! @param [in/out] table   設定するM46E-PR Table
//! @param [in]     addr    検索に使用するv4address
//! @param [in]     cidr    検索に使用するv4netmask（CIDR形式）
//! @param [in]     enable  有効/無効
//!
//! @return true        設定成功
//!         false       設定失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_set_config_enable(m46e_pr_config_table_t* table, struct in_addr* addr, int cidr, bool enable)
{
    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_set_config_enable).");
        return false;
    }

    // IPv4ネットワークアドレスチェック
    if (!m46eapp_pr_check_network_addr(addr, cidr)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(This is invalid address).");
        return false;
    }

    if (table->num > 0) {

        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_config_entry_t* tmp = iter->data;

            // アドレスとネットマスクが一致するエントリーを検索
            if ((addr->s_addr == tmp->v4addr->s_addr) && (cidr == tmp->v4cidr)) {

                // 一致したエントリーの有効/無効フラグを変更
                tmp->enable = enable;
                break;
            }
        }

        // 検索に失敗した場合、ログを残す
        if (iter == &table->entry_list) {
            char address[INET_ADDRSTRLEN];
            m46e_logging(LOG_INFO, "Don't match M46E-PR Table. address = %s/%d\n",
                    inet_ntop(AF_INET, addr, address, sizeof(address)), cidr);
            return false;
        }

    } else {
        m46e_logging(LOG_INFO, "M46E-PR config table is no-entry. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR拡張 M46E-PR Config テーブル検索関数
//!
//! 送信先v4アドレスとv4cidrから、同一エントリーを検索する。
//!
//! @param [in] table   検索するM46E-PR Configテーブル
//! @param [in] addr    検索するv4アドレス
//! @param [in] cidr    検索するv4cidr
//!
//! @return m46e_pr_config_entry_tアドレス 検索成功
//!                                         (マッチした M46E-PR Config Entry情報)
//! @return NULL                            検索失敗
///////////////////////////////////////////////////////////////////////////////
m46e_pr_config_entry_t* m46e_search_pr_config_table(
        m46e_pr_config_table_t* table,
        struct in_addr* addr, int cidr
)
{
    m46e_pr_config_entry_t* entry = NULL;

    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_search_pr_config_table).");
        return NULL;
    }

    if (table->num > 0) {
        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_config_entry_t* tmp = iter->data;

            // アドレスとネットマスクが一致するエントリーを検索
            if ((addr->s_addr == tmp->v4addr->s_addr) && (cidr == tmp->v4cidr)) {
                entry = tmp;

                char address[INET_ADDRSTRLEN];
                DEBUG_LOG("Match M46E-PR Table address = %s/%d\n",
                        inet_ntop(AF_INET, tmp->v4addr, address, sizeof(address)),
                        tmp->v4cidr);

                break;
            }
        }

    } else {
        DEBUG_LOG("M46E-PR table is no-entry. num = %d\n", table->num);
    }

    return entry;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Table出力関数（デバッグ用）
//!
//! M46E-PR Tableを出力する。
//!
//! @param [in] table   出力するM46E-PR Table
//!
//! @return none
///////////////////////////////////////////////////////////////////////////////
void m46e_pr_table_dump(const m46e_pr_table_t* table)
{
    char address[INET6_ADDRSTRLEN];
    char* strbool[] = { "disable", "enable"};

    // 引数チェック
    if (table == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_table_dump).");
        return;
    }

    DEBUG_LOG("table num = %d", table->num);

    m46e_list* iter;
    m46e_list_for_each(iter, &table->entry_list){
        m46e_pr_entry_t* entry = iter->data;
        DEBUG_LOG("%s v4address = %s/%d",
                strbool[entry->enable],
                inet_ntop(AF_INET,  &entry->v4addr, address, sizeof(address)), entry->v4cidr);
        DEBUG_LOG("nemask = %s",
                inet_ntop(AF_INET,  &entry->v4mask, address, sizeof(address)));
        DEBUG_LOG("pr_prefix = %s/%d",
                inet_ntop(AF_INET6, &entry->pr_prefix, address, sizeof(address)), entry->v6cidr);
        DEBUG_LOG("pr_prefix+PlaneID = %s\n",
                inet_ntop(AF_INET6, &entry->pr_prefix_planeid, address, sizeof(address)));

    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Table追加関数
//!
//! M46E-PR Tableへ新規エントリーを追加する。
//! 新たなエントリーは、IPv4アドレスのサブネットマスク長を基準に、
//! 降順でソートして追加する。
//! 本関数内でM46E-PR Tableへアクセスするための排他の獲得と解放を行う。
//! ※entryは、ヒープ領域(malloc関数などで確保したメモリ領域)を渡すこと。
//! エントリーのIPv4アドレスがネットワークアドレスでない場合は、
//! 追加失敗とする。
//! テーブル内にIPv4アドレスとv4cidrが同一のエントリーが既にある場合は、
//! 追加失敗とする。
//!
//! @param [in/out] table   追加するM46E-PR Table
//! @param [in]     entry   追加するエントリー情報
//!
//! @return true        追加成功
//!         false       追加失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_add_entry(m46e_pr_table_t* table, m46e_pr_entry_t* entry)
{
    // 引数チェック
    if ((table == NULL) || (entry == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_add_entry).");
        return false;
    }

    // IPv4ネットワークアドレスチェック
    if (!m46eapp_pr_check_network_addr(&entry->v4addr, entry->v4cidr)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(This is invalid address).");
        return false;
    }

    // 同一エントリー有無検索
    if(m46e_search_pr_table(table, &entry->v4addr, entry->v4cidr) != NULL) {
        m46e_logging(LOG_ERR, "This entry is is already exists.");
        return false;
    }

    if (table->num < PR_MAX_ENTRY_NUM) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        // 要素数のインクリメント
        table->num++;

        m46e_list* node = malloc(sizeof(m46e_list));
        if(node == NULL){
            m46e_logging(LOG_WARNING, "fail to allocate M46E-PR data.\n");
            return false;
        }

        m46e_list_init(node);
        m46e_list_add_data(node, entry);

        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_entry_t* tmp = iter->data;

            // v4netmask長の大小判定
            if (entry->v4cidr >= tmp->v4cidr) {

                // リスト内のネットマスク長と同じか、大きいので
                // 取得したリストの前に追加する。
                m46e_list_add(iter->prev, node);
                break;
            }
        }

        // ネットマスクがリスト内で一番小さい場合、最後尾に追加
        if (iter == &table->entry_list) {
            m46e_list_add_tail(&table->entry_list, node);
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    } else {
        m46e_logging(LOG_INFO, "M46E-PR table is enough. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Table削除関数
//!
//! M46E-PR Tableから指定されたエントリーを削除する。
//! ※エントリーが1個の場合、削除は失敗する。
//! 本関数内でM46E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in/out] table   削除するM46E-PR Table
//! @param [in]     addr    検索に使用するv4address
//! @param [in]     cidr    検索に使用するv4netmask（CIDR形式）
//!
//! @return true        削除成功
//!         false       削除失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_del_entry(m46e_pr_table_t* table, struct in_addr* addr, int cidr)
{
    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_del_entry).");
        return false;
    }

    // IPv4ネットワークアドレスチェック
    if (!m46eapp_pr_check_network_addr(addr, cidr)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(This is invalid address).");
        return false;
    }

    if (table->num > 1) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_entry_t* tmp = iter->data;

            // アドレスとネットマスクが一致するエントリーを検索
            if ((addr->s_addr == tmp->v4addr.s_addr) && (cidr == tmp->v4cidr)) {

                // 一致したエントリーを削除
                free(tmp);
                m46e_list_del(iter);
                free(iter);

                // 要素数のディクリメント
                table->num--;

                break;
            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

        // 検索に失敗した場合、ログを残す
        if (iter == &table->entry_list) {
            char address[INET_ADDRSTRLEN];
            m46e_logging(LOG_INFO, "Don't match M46E-PR Table. address = %s/%d\n",
                    inet_ntop(AF_INET, addr, address, sizeof(address)), cidr);
            return false;
        }

    } else if (table->num == 1) {
        m46e_logging(LOG_INFO, "M46E-PR table is only one entry.\n");
        return false;
    } else {
        m46e_logging(LOG_INFO, "M46E-PR table is no-entry. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Table 有効/無効設定関数
//!
//! M46E-PR Tableから指定されたエントリーの有効/無効を設定する。
//! 本関数内でM46E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in/out] table   設定するM46E-PR Table
//! @param [in]     addr    検索に使用するv4address
//! @param [in]     cidr    検索に使用するv4netmask（CIDR形式）
//! @param [in]     enable  有効/無効
//!
//! @return true        設定成功
//!         false       設定失敗
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_set_enable(m46e_pr_table_t* table, struct in_addr* addr, int cidr, bool enable)
{
    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_set_enable).");
        return false;
    }

    // IPv4ネットワークアドレスチェック
    if (!m46eapp_pr_check_network_addr(addr, cidr)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(This is invalid address).");
        return false;
    }

    if (table->num > 0) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_entry_t* tmp = iter->data;

            // アドレスとネットマスクが一致するエントリーを検索
            if ((addr->s_addr == tmp->v4addr.s_addr) && (cidr == tmp->v4cidr)) {

                // 一致したエントリーの有効/無効フラグを変更
                tmp->enable = enable;

                break;
            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

        // 検索に失敗した場合、ログを残す
        if (iter == &table->entry_list) {
            char address[INET_ADDRSTRLEN];
            m46e_logging(LOG_INFO, "Don't match M46E-PR Table. address = %s/%d\n",
                    inet_ntop(AF_INET, addr, address, sizeof(address)), cidr);
            return false;
        }


    } else {
        m46e_logging(LOG_INFO, "M46E-PR table is no-entry. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR拡張 M46E-PR テーブル検索関数
//!
//! 送信先v4アドレスとv4cidrから、同一エントリーを検索する。
//! 本関数内でM46E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in] table   検索するM46E-PRテーブル
//! @param [in] addr    検索するv4アドレス
//! @param [in] cidr    検索するv4cidr
//!
//! @return m46e_pr_entry_tアドレス    検索成功(マッチした M46E-PR Entry情報)
//! @return NULL                        検索失敗
///////////////////////////////////////////////////////////////////////////////
m46e_pr_entry_t* m46e_search_pr_table(m46e_pr_table_t* table, struct in_addr* addr, int cidr)
{
    m46e_pr_entry_t* entry = NULL;

    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_search_pr_table).");
        return NULL;
    }

    if (table->num > 0) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_entry_t* tmp = iter->data;

            // アドレスとネットマスクが一致するエントリーを検索
            if ((addr->s_addr == tmp->v4addr.s_addr) && (cidr == tmp->v4cidr)) {
                entry = tmp;

                char address[INET_ADDRSTRLEN];
                DEBUG_LOG("Match M46E-PR Table address = %s/%d\n",
                        inet_ntop(AF_INET, &tmp->v4addr, address, sizeof(address)),
                        tmp->v4cidr);

                break;
            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    } else {
        DEBUG_LOG("M46E-PR table is no-entry. num = %d\n", table->num);
    }

    return entry;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR plefix+PlaneID生成関数
//!
//! M46E-PR prefix と PlaneID の値を元に
//! M46E-PR prefix + PlaneID のアドレスを生成する。
//!
//! @param [in]     inaddr      M46E-PR prefix
//! @param [in]     cidr        M46E-PR prefix長
//! @param [in]     plane_id    Plane ID(NULL許容)
//! @param [out]    outaddr     M46E-PR plefix+PlaneID 
//!
//! @retval true    正常終了
//! @retval false   異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_plane_prefix(
        struct in6_addr* inaddr,
        int cidr,
        char* plane_id,
        struct in6_addr* outaddr
)
{
    uint8_t*        src_addr;
    uint8_t*        dst_addr;
    char address[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if ((inaddr == NULL) || (plane_id == NULL) || (outaddr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_plane_prefix).");
        return false;
    }

    if(plane_id != NULL){
        // Plane IDが指定されている場合は、Plane IDで初期化する。
        strcat(address, "::");
        strcat(address, plane_id);
        strcat(address, ":0:0");

        // 値の妥当性は設定読み込み時にチェックしているので戻り値は見ない
        inet_pton(AF_INET6, address, outaddr);
    }
    else{
        // Plane IDが指定されていない場合はALL0で初期化する
        *outaddr = in6addr_any;
    }

    // plefix length分コピーする
    src_addr  = inaddr->s6_addr;
    dst_addr  = outaddr->s6_addr;
    for(int i = 0; (i < 16 && cidr > 0); i++, cidr-=CHAR_BIT){
        if(cidr >= CHAR_BIT){
            dst_addr[i] = src_addr[i];
        }
        else{
            dst_addr[i] = (src_addr[i] & (0xff << cidr)) | (dst_addr[i] & ~(0xff << cidr));
            break;
        }
    }

    DEBUG_LOG("pr_prefix + Plane ID = %s\n",
            inet_ntop(AF_INET6, outaddr, address, sizeof(address)));

    return true;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry 構造体変換関数
//!
//! M46E-PR config情報構造体をM46E-PR Entry 構造体へ変換する。
//! ※変換成功時に受け取ったm46e_pr_entry_tのアドレスは、
//! ※free関数で解放すること。
//!
//! @param [in]  handler    アプリケーションハンドラー
//!        [in]  conf       M46E-PR config情報
//!
//! @return m46e_pr_entry_tアドレス    変換処理正常終了
//! @return NULL                        変換処理異常終了
///////////////////////////////////////////////////////////////////////////////
m46e_pr_entry_t* m46e_pr_conf2entry(
        struct m46e_handler_t* handler,
        m46e_pr_config_entry_t* conf
)
{
    m46e_pr_entry_t* entry = NULL;

    // 引数チェック
    if ((handler == NULL) || (conf == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_conf2entry).");
        return NULL;
    }

    entry = malloc(sizeof(m46e_pr_entry_t));
    if(entry == NULL){
        m46e_logging(LOG_WARNING, "fail to allocate M46E-PR data.\n");
        return NULL;
    }

    // エントリーか有効/無効かを表すフラグ
    entry->enable = conf->enable;

    // IPv4のサブネットマスク長
    PR_CIDR2SUBNETMASK(conf->v4cidr, entry->v4mask);

    // IPv4アドレス
    entry->v4addr.s_addr = conf->v4addr->s_addr;

    // IPv4のCIDR
    entry->v4cidr = conf->v4cidr;

    // M46E-PR address prefix用のIPv6アドレス+Plane ID
    bool ret = m46e_pr_plane_prefix(
            conf->pr_prefix,
            conf->v6cidr,
            handler->conf->general->plane_id,
            &entry->pr_prefix_planeid);

    if(!ret) {
        m46e_logging(LOG_WARNING, "fail to create M46E-PR plefix+PlaneID.\n");
        free(entry);
        return NULL;
    }

    // M46E-PR address prefix用のIPv6アドレス(表示用)
    memcpy(&entry->pr_prefix, conf->pr_prefix, sizeof(struct in6_addr));

    // M46E-PR address prefixのサブネットマスク長(表示用)
    if( (entry->v4addr.s_addr == INADDR_ANY) && (entry->v4cidr == 0) ) {
        entry->v6cidr =  0;
    } else {
        entry->v6cidr =  96 + conf->v4cidr;
    }

    return entry;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR拡張 テーブル検索関数(Stub側)
//!
//! 送信先v4アドレスから、送信先M46E-PR Prefixを検索する。
//! ※マッチする経路が複数合った場合は、最長一致（ロンゲストマッチ）とする。
//! 本関数内でM46E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in] table   検索するM46E-PRテーブル
//! @param [in] addr    検索するv4アドレス
//!
//! @return m46e_pr_entry_tアドレス    検索成功(マッチした M46E-PR Entry情報)
//! @return NULL                        検索失敗
///////////////////////////////////////////////////////////////////////////////
m46e_pr_entry_t* m46e_pr_entry_search_stub(m46e_pr_table_t* table, struct in_addr* addr)
{
    m46e_pr_entry_t* entry = NULL;

    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_entry_search_stub).");
        return NULL;
    }

    if (table->num > 0) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_entry_t* tmp = iter->data;
            struct in_addr network;

            // disableはスキップ
            if (!tmp->enable) {
                continue;
            }

            // ネットワークアドレスの計算
            network.s_addr = addr->s_addr & tmp->v4mask.s_addr;
            // アドレスとネットマスクが一致するエントリーを検索
            if (network.s_addr == tmp->v4addr.s_addr) {
                entry = tmp;

                char address[INET_ADDRSTRLEN];
                DEBUG_LOG("Match M46E-PR Table address = %s\n",
                        inet_ntop(AF_INET, &tmp->v4addr, address, sizeof(address)));
                DEBUG_LOG("dist address = %s\n",
                        inet_ntop(AF_INET, addr, address, sizeof(address)));

                break;
            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    } else {
        DEBUG_LOG("M46E-PR table is no-entry. num = %d\n", table->num);
    }

    return entry;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief SA46-T PR Prefixチェック処理関数(Backbone側)
//!
//! Backbone側から受信したパケットの送信元アドレスに対して、
//! M46E-PR prefix + Plane ID +IPv4 networkアドレス部分をチェックする。
//! 本関数内でM46E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in]     table       検索するM46E-PRテーブル
//! @param [in]     addr        送信元アドレス
//!
//! @retval true  チェックOK(自plane内パケット)
//! @retval false チェックNG(自plane外パケット)
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_prefix_check( m46e_pr_table_t* table, struct in6_addr* addr)
{
    bool ret = false;
    struct in_addr network;

    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_prefix_check).");
        return false;
    }

    if (table->num > 0) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        m46e_list* iter;
        m46e_list_for_each(iter, &table->entry_list){
            m46e_pr_entry_t* tmp = iter->data;

            // M46E-PR prefix + Plane ID判定
            if (IS_EQUAL_M46E_PR_PREFIX(addr, &tmp->pr_prefix_planeid)) {
                network.s_addr = addr->s6_addr32[3] & tmp->v4mask.s_addr;

                // 後半32bitアドレス（IPv4アドレス）のネットワークアドレス判定
                if (network.s_addr == tmp->v4addr.s_addr) {
                    ret = true;

                    char address[INET6_ADDRSTRLEN];
                        DEBUG_LOG("Match M46E-PR Table address = %s\n",
                                inet_ntop(AF_INET6, &tmp->pr_prefix_planeid, address, sizeof(address)));
                    DEBUG_LOG("dist address = %s\n",
                            inet_ntop(AF_INET6, addr, address, sizeof(address)));

                    break;
                }

            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    } else {
        DEBUG_LOG("M46E-PR table is no-entry. num = %d\n", table->num);
        return false;
    }

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry 構造体変換関数(コマンド用)
//!
//! M46E-PR Commandデータ構造体をM46E-PR Entry 構造体へ変換する。
//! ※変換成功時に受け取ったm46e_pr_entry_tのアドレスは、
//! ※free関数で解放すること。
//!
//! @param [in]  handler    アプリケーションハンドラー
//!        [in]  data       M46E-PR Commandデータ
//!
//! @return m46e_pr_entry_tアドレス    変換処理正常終了
//! @return NULL                        変換処理異常終了
///////////////////////////////////////////////////////////////////////////////
m46e_pr_entry_t* m46e_pr_command2entry(
        struct m46e_handler_t* handler,
        struct m46e_pr_entry_command_data* data
)
{
    m46e_pr_entry_t* entry = NULL;

    // 引数チェック
    if ((handler == NULL) || (data == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_command2entry).");
        return NULL;
    }

    entry = malloc(sizeof(m46e_pr_entry_t));
    if(entry == NULL){
        m46e_logging(LOG_WARNING, "fail to allocate M46E-PR data.\n");
        return NULL;
    }

    // エントリーか有効/無効かを表すフラグ
    entry->enable = data->enable;

    // IPv4のサブネットマスク長
    PR_CIDR2SUBNETMASK(data->v4cidr, entry->v4mask);

    // IPv4アドレス
    entry->v4addr.s_addr = data->v4addr.s_addr;

    // IPv4のCIDR
    entry->v4cidr = data->v4cidr;

    // M46E-PR address prefix用のIPv6アドレス+Plane ID
    bool ret = m46e_pr_plane_prefix(
            &data->pr_prefix,
            data->v6cidr,
            handler->conf->general->plane_id,
            &entry->pr_prefix_planeid);

    if(!ret) {
        m46e_logging(LOG_WARNING, "fail to create M46E-PR plefix+PlaneID.\n");
        free(entry);
        return NULL;
    }

    // M46E-PR address prefix用のIPv6アドレス(表示用)
    memcpy(&entry->pr_prefix, &data->pr_prefix, sizeof(struct in6_addr));

    // M46E-PR address prefixのサブネットマスク長(表示用)
    if( (entry->v4addr.s_addr == INADDR_ANY) && (entry->v4cidr == 0) ) {
        entry->v6cidr =  0;
    } else {
        entry->v6cidr =  96 + data->v4cidr;
    }

    return entry;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Config Entry 構造体変換関数(コマンド用)
//!
//! M46E-PR Commandデータ構造体をM46E-PR Config 構造体へ変換する。
//! ※変換成功時に受け取ったm46e_pr_config_entry_tのアドレスは、
//! ※free関数で解放すること。
//!
//! @param [in]  handler    アプリケーションハンドラー
//!        [in]  data       M46E-PR Commandデータ
//!
//! @return m46e_pr_config_entry_tアドレス 変換処理正常終了
//! @return NULL                            変換処理異常終了
///////////////////////////////////////////////////////////////////////////////
m46e_pr_config_entry_t* m46e_pr_command2conf(
        struct m46e_handler_t* handler,
        struct m46e_pr_entry_command_data* data
)
{
    m46e_pr_config_entry_t* entry = NULL;

    // 引数チェック
    if ((handler == NULL) || (data == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46e_pr_command2conf).");
        return NULL;
    }

    entry = malloc(sizeof(m46e_pr_config_entry_t));
    if(entry == NULL){
        m46e_logging(LOG_WARNING, "fail to allocate M46E-PR data.\n");
        return NULL;
    }

    entry->v4addr = malloc(sizeof(struct in_addr));
    if(entry == NULL){
        m46e_logging(LOG_WARNING, "fail to allocate M46E-PR data.\n");
        return NULL;
    }

    entry->pr_prefix = malloc(sizeof(struct in6_addr));
    if(entry == NULL){
        m46e_logging(LOG_WARNING, "fail to allocate M46E-PR data.\n");
        return NULL;
    }

    // エントリーか有効/無効かを表すフラグ
    entry->enable = data->enable;

    // IPv4アドレス
    entry->v4addr->s_addr = data->v4addr.s_addr;

    // IPv4のCIDR
    entry->v4cidr = data->v4cidr;

    // M46E-PR address prefix用のIPv6アドレス
    memcpy(entry->pr_prefix, &data->pr_prefix, sizeof(struct in6_addr));

    // M46E-PR address prefixのサブネットマスク長
    entry->v6cidr =  data->v6cidr;

    return entry;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry追加コマンド契機M46E-PRテーブル追加関数
//!
//! 1.コマンドデータ形式からM46E-PR Entry形式に変換する。
//! 2.M46E-PR TableにM46E-PR Entryを登録する。
//! 3.当該M46E-PRエントリのIPv4ネットワーク経路を削除する。
//!
//! @param [in]     handler    M46Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(追加成功)
//!         false       NG(追加失敗)
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_add_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req)
{

    // ローカル変数
    char   v4addr[INET_ADDRSTRLEN]  = { 0 };
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }

    // Commandデータ形式からM46E-PR Entry形式に変換
    m46e_pr_entry_t* entry = m46e_pr_command2entry(handler, &req->pr_data);
    if(entry == NULL) {
        m46e_logging(LOG_ERR,
             "fail to translate command data format to M46E-PR Entry format : plane_id = %s network address = %s/%d M46E-PR prefix = %s/%d\n",
             handler->conf->general->plane_id,
             inet_ntop(AF_INET, &req->pr_data.v4addr, v4addr, INET_ADDRSTRLEN),
             req->pr_data.v4cidr,
             inet_ntop(AF_INET6, &req->pr_data.pr_prefix, v6addr, INET6_ADDRSTRLEN),
             req->pr_data.v6cidr);
        // ここでConsoleに要求コマンド失敗のエラーを返す。
        m46e_pr_print_error(req->pr_data.fd, M46E_PR_COMMAND_EXEC_FAILURE);

        return false;
    }
    else {
        // M46E-PR Tableに当該エントリが登録済みかチェック
        // 登録済みの場合はConsoleにエラー出力
        if(m46e_search_pr_table(handler->pr_handler, &entry->v4addr, entry->v4cidr) != NULL) {
            m46e_logging(LOG_ERR, "This entry is is already exists.");

            // ここでConsoleに要求コマンド失敗のエラーを返す。
            m46e_pr_print_error(req->pr_data.fd, M46E_PR_COMMAND_ENTRY_FOUND);
            return false;
        }
        // 形式変換OKなのでM46E-PR TableにM46E-PR Entryを追加
        if(!m46e_pr_add_entry(handler->pr_handler, entry)) {
            m46e_logging(LOG_ERR,
                 "fail to add M46E-PR Entry : plane_id = %s network address = %s/%d M46E-PR prefix = %s/%d\n",
                 handler->conf->general->plane_id,
                 inet_ntop(AF_INET, &req->pr_data.v4addr, v4addr, INET_ADDRSTRLEN),
                 req->pr_data.v4cidr,
                 inet_ntop(AF_INET6, &req->pr_data.pr_prefix, v6addr, INET6_ADDRSTRLEN),
                 req->pr_data.v6cidr);
            // ここでConsoleに要求コマンド失敗のエラーを返す。
            m46e_pr_print_error(req->pr_data.fd, M46E_PR_COMMAND_EXEC_FAILURE);

            return false;
        }
        else {
            // PRテーブルへの登録が成功した場合、活性化を伴う追加要求の場合は
            // 当該PR EntryのIPネットワーク経路を追加
            if(req->pr_data.enable == true) {
                m46e_network_add_route(
                    AF_INET,
                    handler->conf->tunnel->ipv4.ifindex,
                    &req->pr_data.v4addr,
                    req->pr_data.v4cidr,
                    NULL
                );
            }
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry削除コマンド契機M46E-PRテーブル削除関数
//!
//! 1.M46E-PR TableからM46E-PR Entryを削除する。
//! 2.当該M46E-PRエントリのIPv4ネットワーク経路を削除する。
//!
//! @param [in]     handler    M46Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(削除成功)
//!         false       NG(削除失敗)
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_del_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req)
{

    // ローカル変数
    char   v4addr[INET_ADDRSTRLEN]  = { 0 };
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }
    // M46E-PR Tableに当該エントリが登録されているかチェック
    // 登録されていない場合はConsoleにエラー出力
    if(m46e_search_pr_table(handler->pr_handler, &req->pr_data.v4addr, req->pr_data.v4cidr) == NULL) {
        m46e_logging(LOG_ERR, "This entry is not exists.");

        // ここでConsoleに要求コマンド失敗のエラーを返す。
        m46e_pr_print_error(req->pr_data.fd, M46E_PR_COMMAND_ENTRY_NOTFOUND);
        return false;
    }
    // M46E-PR TableからM46E-PR Entryを削除
    if(!m46e_pr_del_entry(handler->pr_handler, &req->pr_data.v4addr, req->pr_data.v4cidr)) {
        m46e_logging(LOG_ERR,
             "fail to del M46E-PR Entry : plane_id = %s network address = %s/%d M46E-PR prefix = %s/%d\n",
             handler->conf->general->plane_id,
             inet_ntop(AF_INET, &req->pr_data.v4addr, v4addr, INET_ADDRSTRLEN),
             req->pr_data.v4cidr,
             inet_ntop(AF_INET6, &req->pr_data.pr_prefix, v6addr, INET6_ADDRSTRLEN),
             req->pr_data.v6cidr);
        // ここでConsoleに要求コマンド失敗のエラーを返す。
        m46e_pr_print_error(req->pr_data.fd, M46E_PR_COMMAND_EXEC_FAILURE);

        return false;
    }
    else {
        //  PRテーブルから削除が成功した場合、活性化/非活性化に関わらず
        //  当該PR EntryのIPネットワークアドレス経路を削除
        m46e_network_del_route(
            AF_INET,
            handler->conf->tunnel->ipv4.ifindex,
            &req->pr_data.v4addr,
            req->pr_data.v4cidr,
            NULL
        );
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry全削除関数
//!
//! M46E-PR TableからM46E-PR Entryを全て削除する。
//!
//! @param [in]     handler    M46Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(削除成功)
//!         false       NG(削除失敗)
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_delall_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req)
{
	
    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }

    // 排他開始
    pthread_mutex_lock(&handler->pr_handler->mutex);

    // M46E-PR Entry全削除
    while(!m46e_list_empty(&handler->pr_handler->entry_list)){
        m46e_list* node = handler->pr_handler->entry_list.next;
        m46e_pr_entry_t* pr_entry = node->data;
        m46e_network_del_route(
            AF_INET,
            handler->conf->tunnel->ipv4.ifindex,
            &pr_entry->v4addr,
            pr_entry->v4cidr,
            NULL
        );
        free(pr_entry);
        m46e_list_del(node);
        free(node);
        handler->pr_handler->num--;
    }

    //リストの初期化
    m46e_list_init(&handler->pr_handler->entry_list);

    // 排他解除
    pthread_mutex_unlock(&handler->pr_handler->mutex);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry活性化コマンド契機M46E-PR Entry活性化関数
//!
//! 1.M46E-PR Tableの当該Entryを活性化する。
//!
//! @param [in]     handler    M46Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(活性化成功)
//!         false       NG(活性化失敗)
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_enable_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req)
{
    // ローカル変数
    char   v4addr[INET_ADDRSTRLEN]  = { 0 };
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }

    //PR Entryを活性化
    if(!m46e_pr_set_enable(handler->pr_handler, &req->pr_data.v4addr, req->pr_data.v4cidr, req->pr_data.enable)) {
        m46e_logging(LOG_ERR,
             "fail to enable M46E-PR Entry : plane_id = %s network address = %s/%d M46E-PR prefix = %s/%d\n",
             handler->conf->general->plane_id,
             inet_ntop(AF_INET, &req->pr_data.v4addr, v4addr, INET_ADDRSTRLEN),
             req->pr_data.v4cidr,
             inet_ntop(AF_INET6, &req->pr_data.pr_prefix, v6addr, INET6_ADDRSTRLEN),
             req->pr_data.v6cidr);
        // ここでConsoleに要求コマンド失敗のエラーを返す。
        m46e_pr_print_error(req->pr_data.fd, M46E_PR_COMMAND_EXEC_FAILURE);

        return false;
    }
    else {
        // 当該PR EntryのIPネットワーク経路を追加
        m46e_network_add_route(
            AF_INET,
            handler->conf->tunnel->ipv4.ifindex,
            &req->pr_data.v4addr,
            req->pr_data.v4cidr,
            NULL
        );
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry非活性化コマンド契機M46E-PR Entry非活性化関数
//!
//! 1.コマンドデータ形式からM46E-PR Entry形式に変換する。
//! 2.M46E-PR Tableの当該Entryを非活性化する。
//!
//! @param [in]     handler    M46Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(非活性化成功)
//!         false       NG(非活性化失敗)
///////////////////////////////////////////////////////////////////////////////
bool m46e_pr_disable_entry_pr_table(struct m46e_handler_t* handler, struct m46e_command_request_data* req)
{
    // ローカル変数
    char   v4addr[INET_ADDRSTRLEN]  = { 0 };
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }

    //PR Entryを非活性化
    if(!m46e_pr_set_enable(handler->pr_handler, &req->pr_data.v4addr, req->pr_data.v4cidr, req->pr_data.enable)) {
        m46e_logging(LOG_ERR,
             "fail to serch M46E-PR Entry : plane_id = %s network address = %s/%d M46E-PR prefix = %s/%d\n",
             handler->conf->general->plane_id,
             inet_ntop(AF_INET, &req->pr_data.v4addr, v4addr, INET_ADDRSTRLEN),
             req->pr_data.v4cidr,
             inet_ntop(AF_INET6, &req->pr_data.pr_prefix, v6addr, INET6_ADDRSTRLEN),
             req->pr_data.v6cidr);
        // ここでConsoleに要求コマンド失敗のエラーを返す。
        m46e_pr_print_error(req->pr_data.fd, M46E_PR_COMMAND_EXEC_FAILURE);

        return false;
    }
    else {
        // 当該PR EntryのIPネットワーク経路を追加
        m46e_network_del_route(
            AF_INET,
            handler->conf->tunnel->ipv4.ifindex,
            &req->pr_data.v4addr,
            req->pr_data.v4cidr,
            NULL
        );
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief ハッシュテーブル内部情報出力関数
////!
////! M46E-PR情報管理内のテーブルを出力する
////!
////! @param [in]     pr_handler      M46E-PR情報管理
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return なし
/////////////////////////////////////////////////////////////////////////////////
void m46e_pr_show_entry_pr_table(m46e_pr_table_t* pr_handler, int fd, char *plane_id)
{

    // ローカル変数初期化
    char   v4addr[INET_ADDRSTRLEN]  = { 0 };
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((pr_handler == NULL) || (plane_id == NULL)) {
        return;
    }

    pthread_mutex_lock(&pr_handler->mutex);

    // M46E-PR Table 表示
    dprintf(fd, "\n");
    dprintf(fd, " +------------------------------------------------------------------------------------------+\n");
    dprintf(fd, " /     M46E Prefix Resolution Table                                                        /\n");
    dprintf(fd, " +---+-----------+----------------------+---------+-----------------------------------------+\n");
    dprintf(fd, "     | Plane ID  | IPv4 Network Address | Netmask | M46E-PR Address Prefix                 |\n");
    dprintf(fd, " +---+-----------+----------------------+---------+-----------------------------------------+\n");

    m46e_list* iter;
    m46e_list_for_each(iter, &pr_handler->entry_list){
        m46e_pr_entry_t* pr_entry = iter->data;

            if(pr_entry != NULL){
                // enable/disable frag
                if(pr_entry->enable == true){
                    dprintf(fd, "   * |");
                } else {
                    dprintf(fd, "     |");
                }
                // plane id
                dprintf(fd, " %-9s |",plane_id);
                // IPv4 Network Address
                dprintf(fd, " %-20s |",inet_ntop(AF_INET, &pr_entry->v4addr, v4addr, sizeof(v4addr)));
                // Netmask
                dprintf(fd, " /%-6d |",pr_entry->v6cidr);
                // IPv6-PR Address Prefix
                dprintf(fd, " %-39s |\n",inet_ntop(AF_INET6, &pr_entry->pr_prefix, v6addr, sizeof(v6addr)));
            }
    }

    dprintf(fd, " +---+-----------+----------------------+---------+-----------------------------------------+\n");
    dprintf(fd, "  Note : [*] shows available entry for prefix resolution process.\n");
    dprintf(fd, "\n");

    pthread_mutex_unlock(&pr_handler->mutex);

    return;
}


///////////////////////////////////////////////////////////////////////////////
////! @brief M46E-PRモードエラー出力関数
////!
////! 引数で指定されたファイルディスクリプタにエラーを出力する。
////!
////! @param [in] fd           出力先のファイルディスクリプタ
////! @param [in] error_code   エラーコード
////!
////! @return なし
/////////////////////////////////////////////////////////////////////////////////
void m46e_pr_print_error(int fd, enum m46e_pr_command_error_code error_code)
{
   // ローカル変数宣言

    // ローカル変数初期化

    switch(error_code) {
    case M46E_PR_COMMAND_MODE_ERROR:
        dprintf(fd, "\n");
        dprintf(fd,"Requested command is available for M46E-PR mode only!\n");
        dprintf(fd, "\n");

        break;

    case M46E_PR_COMMAND_EXEC_FAILURE:
        dprintf(fd, "\n");
        dprintf(fd,"Sorry! Fail to execute your requested command. \n");
        dprintf(fd, "\n");

        break;

    case M46E_PR_COMMAND_ENTRY_FOUND:
        dprintf(fd, "\n");
        dprintf(fd,"Requested entry is already exist. \n");
        dprintf(fd, "\n");

        break;

    case M46E_PR_COMMAND_ENTRY_NOTFOUND:
        dprintf(fd, "\n");
        dprintf(fd,"Requested entry is not exist. \n");
        dprintf(fd, "\n");

        break;

    default:
        // ありえないルート

        break;

    }

    return;
}


