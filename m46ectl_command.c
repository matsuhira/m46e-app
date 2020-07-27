/******************************************************************************/
/* ファイル名 : m46ectl_command.c                                             */
/* 機能概要   : 内部コマンドクラス ソースファイル                             */
/* 修正履歴   : 2013.07.08 Y.Shibata 新規作成                                 */
/*              2013.09.12 H.Koganemaru 動的定義変更機能追加                  */
/*              2013.10.03 Y.Shibata  M46E-PR拡張機能                         */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <regex.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>

#include "m46ectl_command.h"
#include "m46eapp_dynamic_setting.h"
#include "m46eapp_log.h"
#include "m46eapp_command_data.h"
#include "m46eapp_socket.h"
#include "m46eapp_pr.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

///////////////////////////////////////////////////////////////////////////////
//! Stub側汎用コマンド機能での受付許可コマンド
//  以下の配列に許可するコマンド用の正規表現を２つ追加する
//  １つのコマンドにつき２つの正規表現を追加する。
//  １つ目の正規表現はコマンド名以降にオプションが続くパターン
//  ２つ目の正規表現はコマンド名以降にオプションが続かないパターン
///////////////////////////////////////////////////////////////////////////////
static char* command_name[] =  {
    "^([ \t]*)([^ \t]*)(ifconfig)[ \t]+",   // ifconfig以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ifconfig)$",        // ifconfig以降にオプションが続かないパターン
    "^([ \t]*)([^ \t]*)(ip)[ \t]+",         // ip以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ip)$",              // ip以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(arp)[ \t]+",        // arp以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(arp)$",             // arp以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ethtool)[ \t]+",    // arp以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ethtool)$",         // arp以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(iptables)[ \t]+",   // iptables以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(iptables)$",        // iptables以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ip6tables)[ \t]+",  // ip6tables以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ip6tables)$",       // ip6tables以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ipmaddr)[ \t]+",    // ipmaddr以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ipmaddr)$",         // ipmaddr以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(route)[ \t]+",      // route以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(route)$",           // route以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ps)[ \t]+",         // ps以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(ps)$",              // ps以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(netstat)[ \t]+",    // netstat以降にオプションが続くパターン
    "^([ \t]*)([^ \t]*)(netstat)$",         // netstat以降にオプションが続くパターン
    NULL,
};

//! オプション引数構造体定義
struct opt_arg {
    char* main;   ///< メインコマンド文字列
    char* sub;    ///< サブコマンド文字列
    int   code;   ///< コマンドコード
};

//! コマンド引数
static const struct opt_arg opt_args[] = {
    {"add",     "pr",  M46E_ADD_PR_ENTRY},
    {"del",     "pr",  M46E_DEL_PR_ENTRY},
    {"enable",  "pr",  M46E_ENABLE_PR_ENTRY},
    {"disable", "pr",  M46E_DISABLE_PR_ENTRY},
    {"del",     "pr",  M46E_DEL_PR_ENTRY},
    {NULL,       NULL, M46E_COMMAND_MAX}
};

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をbool値に変換する
//!
//! 引数で指定された文字列がyes or noの場合に、yesならばtrueに、
//! noならばfalseに変換して出力パラメータに格納する。
//!
//! @param [in]  str     変換対象の文字列
//! @param [out] output  変換結果の出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がbool値でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_bool(const char* str, bool* output)
{
    // ローカル変数定義
    bool result;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(str, OPT_BOOL_ON)){
        *output = true;
    }
    else if(!strcasecmp(str, OPT_BOOL_OFF)){
        *output = false;
    }
    else if(!strcasecmp(str, OPT_BOOL_ENABLE)){
        *output = true;
    }
    else if(!strcasecmp(str, OPT_BOOL_DISABLE)){
        *output = false;
    }
    else if(!strcasecmp(str, OPT_BOOL_NONE)){
        *output = false;
    }
    else{
        result = false;
    }

    return result;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列を整数値に変換する
//!
//! 引数で指定された文字列が整数値で、且つ最小値と最大値の範囲に
//! 収まっている場合に、数値型に変換して出力パラメータに格納する。
//!
//! @param [in]  str     変換対象の文字列
//! @param [out] output  変換結果の出力先ポインタ
//! @param [in]  min     変換後の数値が許容する最小値
//! @param [in]  max     変換後の数値が許容する最大値
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列が整数値でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_int(const char* str, int* output, const int min, const int max)
{
    // ローカル変数定義
    bool  result;
    int   tmp;
    char* endptr;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        _D_(printf("parse_int Parameter Check NG.\n");)
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = 0;
    endptr = NULL;

    tmp = strtol(str, &endptr, 10);

    if((tmp == LONG_MIN || tmp == LONG_MAX) && (errno != 0)){
        // strtol内でエラーがあったのでエラー
        result = false;
    }
    else if(endptr == str){
        // strtol内でエラーがあったのでエラー
        result = false;
    }
    else if(tmp > max){
        // 最大値よりも大きいのでエラー
        _D_(printf("parse_int Parameter too big.\n");)
        result = false;
    }
    else if(tmp < min) {
        // 最小値よりも小さいのでエラー
        _D_(printf("parse_int Parameter too small.\n");)
        result = false;
    }
    else if (*endptr != '\0') {
        // 最終ポインタが終端文字でない(=文字列の途中で変換が終了した)のでエラー
        result = false;
    }
    else {
        // ここまでくれば正常に変換できたので、出力変数に格納
        *output = tmp;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をIPv4アドレス型に変換する
//!
//! 引数で指定された文字列がIPv4アドレスのフォーマットの場合に、
//! IPv4アドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//! @param [out] prefixlen  プレフィックス長出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がIPv4アドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_ipv4address(const char* str, struct in_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;


    // 引数チェック
    if((str == NULL) || (output == NULL)){
        _D_(printf("parse_ipv4address Parameter Check NG.\n");)
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = strdup(str);

    if(tmp == NULL){
        return false;
    }

    token = strtok(tmp, "/");
    if(tmp){
        if(inet_pton(AF_INET, token, output) <= 0){
            _D_(printf("parse_ipv4address parse NG.\n");)
            result = false;
        }
        else{
            result = true;
        }
    }

    if(result && (prefixlen != NULL)){
        token = strtok(NULL, "/");
        if(token == NULL){
            *prefixlen = 0;
        }
        else{
            result = parse_int(token, prefixlen, OPT_IPV4_NETMASK_MIN, OPT_IPV4_NETMASK_MAX);
        }
    }

    free(tmp);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列(IPv4 ネットワークアドレス)をIPv4アドレス型に変換する
//! ※M46E-PRのみ
//! 引数で指定された文字列がIPv4アドレスのフォーマットの場合に、
//! IPv4アドレス型に変換して出力パラメータに格納する。
//! (default gateway:0.0.0.0, CIDR:0 設定用)
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//! @param [out] prefixlen  プレフィックス長出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がIPv4アドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_ipv4address_pr(const char* str, struct in_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;

    DEBUG_LOG("%s start", __func__);

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = strdup(str);

    if(tmp == NULL){
        return false;
    }

    token = strtok(tmp, "/");
    if(tmp){
        if(inet_pton(AF_INET, token, output) <= 0){
            result = false;
        }
        else{
            result = true;
        }
    }

    if(result && (prefixlen != NULL)){
        token = strtok(NULL, "/");
        if(token == NULL){
            *prefixlen = -1;
            result = false;
        }
        else{
            result = parse_int(token, prefixlen, CMD_IPV4_NETMASK_MIN, CMD_IPV4_NETMASK_MAX);
        }
    }

    free(tmp);

    DEBUG_LOG("%s end. return %s", __func__, result?"true":"false");
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をIPv6アドレス型に変換する
//!
//! 引数で指定された文字列がIPv6アドレスのフォーマットの場合に、
//! IPv6アドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//! @param [out] prefixlen  プレフィックス長出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がIPv6アドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_ipv6address(const char* str, struct in6_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;

    DEBUG_LOG("%s start", __func__);

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = strdup(str);

    if(tmp == NULL){
        return false;
    }

    token = strtok(tmp, "/");
    if(tmp){
        if(inet_pton(AF_INET6, token, output) <= 0){
            result = false;
        }
        else{
            result = true;
        }
    }

    if(result && (prefixlen != NULL)){
        token = strtok(NULL, "/");
        if(token == NULL){
            *prefixlen = -1;
            result = false;
        }
        else{
            result = parse_int(token, prefixlen, CONFIG_IPV6_PREFIX_MIN, CONFIG_IPV6_PREFIX_MAX);
        }
    }

    free(tmp);

    DEBUG_LOG("%s end. return %s", __func__, result?"true":"false");
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief IPv4アドレスからプレフィックス長を計算する。
//!
//! 引数で指定されたIPv4アドレスからアドレスクラスに基づいた
//! プレフィックス長(ネットマスク)を計算して戻り値として返す。
//!
//! @param [in]  addr     IPv4アドレス
//!
//! @return プレフィックス長
///////////////////////////////////////////////////////////////////////////////
static int config_ipv4_prefix(const struct in_addr* addr)
{
    // ローカル変数宣言
    int netmask;

    // 引数チェック
    if(addr == NULL){
        _D_(printf("config_ipv4_prefix Parameter Check NG.\n");)
        return -1;
    }

    if(IN_CLASSA(ntohl(addr->s_addr))){
        netmask = OPT_IPV4_NETMASK_MAX - IN_CLASSA_NSHIFT;
    }
    else if(IN_CLASSB(ntohl(addr->s_addr))){
        netmask = OPT_IPV4_NETMASK_MAX - IN_CLASSB_NSHIFT;
    }
    else if(IN_CLASSC(ntohl(addr->s_addr))){
        netmask = OPT_IPV4_NETMASK_MAX - IN_CLASSC_NSHIFT;
    }
    else{
        // ありえない
        netmask = -1;
    }

    return netmask;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をMACアドレス型に変換する
//!
//! 引数で指定された文字列がMACアドレスのフォーマットの場合に、
//! MACアドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がMACアドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_macaddress(const char* str, struct ether_addr* output)
{
    // ローカル変数定義
    bool result;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        _D_(printf("parse_macaddress Parameter Check NG.\n");)
        return false;
    }

    if(ether_aton_r(str, output) == NULL){
        _D_(printf("parse_macaddress parse NG.\n");)
        result = false;
    }
    else{
        result = true;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief KEY/VALUE行判定関数
//!
//! 引数で指定された文字列がKEY/VALUE行に該当するかどうかをチェックし、
//! KEY/VALUE行の場合は、出力パラメータにKEY/VALUE値を格納する。
//!
//! @param [in]  opt_str  コマンドオプションパラメータ
//! @param [out] kv        KEY/VALUE値格納先ポインタ
//!
//! @retval true  引数の文字列がKEY/VALUE行である
//! @retval false 引数の文字列がKEY/VALUE行でない
///////////////////////////////////////////////////////////////////////////////
static bool opt_is_keyvalue(const char* opt_str, opt_keyvalue_t* kv)
{
    // ローカル変数宣言
    regex_t    preg;
    size_t     nmatch = 3;
    regmatch_t pmatch[nmatch];
    bool       result;

    // 引数チェック
    if((opt_str == NULL) || (kv == NULL)){
        _D_(printf("opt_is_keyvalue Parameter Check NG.\n");)
        return false;
    }

    // ローカル変数初期化
    result = true;

    if (regcomp(&preg, OPT_REGEX, REG_EXTENDED|REG_NEWLINE) != 0) {
        _D_(printf("regex compile failed.\n");)
        return false;
    }

    if (regexec(&preg, opt_str, nmatch, pmatch, 0) == REG_NOMATCH) {
        _D_(printf("No match.\n");)
        result = false;
    }
    else {
        sprintf(kv->key,   "%.*s", (pmatch[1].rm_eo-pmatch[1].rm_so), &opt_str[pmatch[1].rm_so]);
        sprintf(kv->value, "%.*s", (pmatch[2].rm_eo-pmatch[2].rm_so), &opt_str[pmatch[2].rm_so]);
        _D_(printf("Match. key=\"%s\", value=\"%s\"\n", kv->key, kv->value);)
    }

    regfree(&preg);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス操作オプションの解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  kv         コマンドオプション
//! @param [out] command    コマンドデータ構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool opt_parse_device(
    const opt_keyvalue_t*  kv,
         struct m46e_command_t* command
)
{
    // ローカル変数宣言
    bool result = true;
    struct m46e_device_data* device = NULL;

    // 引数チェック
    if((kv == NULL) || (command == NULL)){
        _D_(printf("opt_parse_device Parameter Check NG.\n");)
        return false;
    }

    // ローカル変数初期化
    device = &(command->req.dev_data);

    if(!strcasecmp(OPT_PHYSICAL_DEV, kv->key)){
        _D_(printf("Match %s.\n", OPT_PHYSICAL_DEV);)
        memcpy(device->s_physical.name, kv->value, IFNAMSIZ);
        device->s_physical.is_set = true;
    }
    else if(!strcasecmp(OPT_DEV_NAME, kv->key)){
        _D_(printf("Match %s.\n", OPT_DEV_NAME);)
        memcpy(device->s_virtual.name, kv->value, IFNAMSIZ);
        device->s_virtual.is_set = true;
    }
    else if(!strcasecmp(OPT_IPV4_ADDRESS, kv->key)){
        _D_(printf("Match %s.\n", OPT_IPV4_ADDRESS);)
        result = parse_ipv4address(kv->value,
                &device->s_v4address.address,
                &device->s_v4netmask.netmask);
        if(result && (device->s_v4netmask.netmask == 0)){
            // プレフィックスが指定されていない場合はアドレスクラスから計算する
            device->s_v4netmask.netmask = config_ipv4_prefix(&device->s_v4address.address);
        }
        if(result) {
            device->s_v4address.is_set = true;
            device->s_v4netmask.is_set = true;
        } else {
            printf("fail to parse %s\n", OPT_IPV4_ADDRESS);
        }
    }
    else if(!strcasecmp(OPT_IPV4_GATEWAY, kv->key)){
        _D_(printf("Match %s.\n", OPT_IPV4_GATEWAY);)
        result = parse_ipv4address(kv->value, &device->s_v4gateway.gateway, NULL);
        if (result) {
            device->s_v4gateway.is_set = true;
        } else {
            printf("fail to parse %s\n", OPT_IPV4_GATEWAY);
        }
    }
    else if(!strcasecmp(OPT_HWADDR, kv->key)){
        _D_(printf("Match %s.\n", OPT_HWADDR);)
        result = parse_macaddress(kv->value, &device->s_hwaddr.hwaddr);
        if (result) {
            device->s_hwaddr.is_set = true;
        } else {
            printf("fail to parse %s\n", OPT_HWADDR);
        }
    }
    else if(!strcasecmp(OPT_MTU, kv->key)){
        _D_(printf("Match %s.\n", OPT_MTU);)
        result = parse_int(kv->value, &device->s_mtu.mtu, OPT_DEVICE_MTU_MIN, OPT_DEVICE_MTU_MAX);
        if (result) {
            device->s_mtu.is_set = true;
        } else {
            printf("fail to parse %s\n", OPT_MTU);
        }
    }
    else{
        // 不明なキーなのでスキップ
        printf("unknown option : %s\n", kv->key);
        result = false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバッグログモード設定オプションの解析関数
//!
//! 引数で指定されたコマンドオプションを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  opt1       コマンドオプション
//! @param [out] command    コマンドデータ構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool opt_parse_dbglog(char* opt[], struct m46e_command_t* command)
{
    // ローカル変数宣言
    bool result = true;

    // 引数チェック
    if((opt == NULL) || (command == NULL)){
        _D_(printf("opt_parse_dbglog Parameter Check NG.\n");)
        return false;
    }
    // コマンド解析
    result = parse_bool(opt[0], &command->req.dlog.mode);
    if(!result){
        return false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery モード値設定オプションの解析関数
//!
//! 引数で指定されたコマンドオプションを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  num        オプションの数
//! @param [in]  opt1       コマンドオプション
//! @param [out] command    コマンドデータ構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool opt_parse_pmtumd(char* opt[], struct m46e_command_t* command)
{
    // ローカル変数宣言
    bool result = true;

    // 引数チェック
    if((opt == NULL) || (command == NULL)){
        _D_(printf("opt_parse_pmtutm Type Check NG.\n");)
        return false;
    }

    result = parse_int(opt[0],(int*)&command->req.pmtu_mode.type,
                            OPT_PMTUD_MODE_MIN,OPT_PMTUD_MODE_MAX);
    return result;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery Expire Timer値設定オプションの解析関数
//!
//! 引数で指定されたコマンドオプションを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  opt1       コマンドオプション
//! @param [out] command    コマンドデータ構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool opt_parse_pmtutm(char* opt[], struct m46e_command_t* command)
{
    // ローカル変数宣言
    bool result = true;

    // 引数チェック
    if((opt == NULL) || (command == NULL)){
        _D_(printf("opt_parse_pmtutm Parameter Check NG.\n");)
        return false;
    }

    result = parse_int(opt[0], &command->req.pmtu_exptime.exptime,
                           OPT_PMTUD_EXPIRE_TIME_MIN, OPT_PMTUD_EXPIRE_TIME_MAX);
    if(!result){
        return false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 強制フラグメントモード設定オプションの解析関数
//!
//! 引数で指定されたコマンドオプションを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  opt1       コマンドオプション
//! @param [out] command    コマンドデータ構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool opt_parse_ffrag(char* opt[], struct m46e_command_t* command)
{
    // ローカル変数宣言
    bool result = true;

    // 引数チェック
    if((opt == NULL) || (command == NULL)){
        _D_(printf("opt_parse_ffrag Parameter Check NG.\n");)
        return false;
    }

    result = parse_bool(opt[0], &command->req.ffrag.mode);

    if(!result){
        return false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デフォルトゲートウェイ設定オプションの解析関数
//!
//! 引数で指定されたコマンドオプションを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  opt1       コマンドオプション
//! @param [out] command    コマンドデータ構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool opt_parse_defgw(char* opt[], struct m46e_command_t* command)
{
    // ローカル変数宣言
    bool result = true;

    // 引数チェック
    if((opt == NULL) || (command == NULL)){
        _D_(printf("opt_parse_defgw Parameter Check NG.\n");)
        return false;
    }

    result = parse_bool(opt[0], &command->req.defgw.mode);

    if(!result){
        return false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイスMTU設定オプションの解析関数
//!
//! 引数で指定されたコマンドオプションを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  opt1       コマンドオプション
//! @param [out] command    コマンドデータ構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool opt_parse_tunmtu(char* opt[], struct m46e_command_t* command)
{
    // ローカル変数宣言
    bool result = true;

    // 引数チェック
    if((opt == NULL) || (command == NULL)){
        _D_(printf("opt_parse_tunmtu Parameter Check NG.\n");)
        return false;
    }

    result = parse_int(opt[0], &command->req.tunmtu.mtu,
                           OPT_TUNNEL_MTU_MIN, OPT_TUNNEL_MTU_MAX);

    if(!result){
        return false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 収容デバイスMTU設定オプションの解析関数
//!
//! 引数で指定されたコマンドオプションを解析し、
//! 設定値をデバイス設定構造体に格納する。
//!
//! @param [in]  opt1       コマンドオプション
//! @param [in]  opt2       コマンドオプション
//! @param [out] command    コマンドデータ構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool opt_parse_devmtu(char* opt[], struct m46e_command_t* command)
{
    // ローカル変数宣言
    bool result = true;

    // 引数チェック
    if((opt == NULL) || (command == NULL)){
        _D_(printf("opt_parse_devmtu Parameter Check NG.\n");)
        return false;
    }

    memcpy(&command->req.devmtu.name, opt[0], IFNAMSIZ);

    result = parse_int(opt[1], &command->req.devmtu.mtu,
                           OPT_DEVICE_MTU_MIN, OPT_DEVICE_MTU_MAX);

    if(!result){
        return false;
    }

    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス操作コマンドオプション設定処理関数
//!
//! インタフェース増減設のオプションを設定する。
//!
//! @param [in]  num        オプションの数
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_device_set_option(int num, char* opt[], struct m46e_command_t* command)
{
    int             i;
    opt_keyvalue_t  kv;
    bool            result = false;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_set_option Parameter Check NG.\n");)
        return false;
    }

    // 正規表現をオプションを解析し、値を設定
    for (i = 0; i < num; i++) {
        memset(&kv, 0, sizeof(opt_keyvalue_t));
        if(opt_is_keyvalue(opt[i], &kv)){
            result = opt_parse_device(&kv, command);
            if (!result) {
                return result;
            }
        }
    }

    struct m46e_device_data* device = &(command->req.dev_data);

    //  増減設は「物理デバイス名」が必須
    if ((command->code == M46E_DEVICE_ADD) ||
            (command->code == M46E_DEVICE_DEL)) {
        if (!(device->s_physical.is_set)) {
            printf("%s is not found\n", OPT_PHYSICAL_DEV);
            result = false;
        }
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stub側 汎用コマンド判定関数
//!
//! Stub側で実行可能なコマンドを判定する。
//!
//! @param [in]  line       Stub側で実行するコマンドライン文字列
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_judge_name( char* line)
{
    int             i;
    bool            result = false;
    regex_t         preg;
    size_t          nmatch = 4;
    regmatch_t      pmatch[nmatch];
    char            str[256];

    // 引数チェック
    if (line == NULL) {
        _D_(printf("m46e_command_judge_name Parameter Check NG.\n");)
        return false;
    }

    // 正規表現をオプションを解析し、値を設定
    for (i = 0; command_name[i] != NULL; i++) {
        if (regcomp(&preg, command_name[i], REG_EXTENDED|REG_NEWLINE) != 0) {
            _D_(printf("regex compile failed.\n");)
            return false;
        }

        if (regexec(&preg, line, nmatch, pmatch, 0) == REG_NOMATCH) {
            //_D_(printf("No match.\n");)
        }
        else {
            //_D_(printf("Match.\n");)
            sprintf(str, "%.*s", (pmatch[1].rm_eo-pmatch[1].rm_so), &line[pmatch[1].rm_so]);
            //_D_(printf("$1 = %s\n", str);)
            sprintf(str, "%.*s", (pmatch[2].rm_eo-pmatch[2].rm_so), &line[pmatch[2].rm_so]);
            //_D_(printf("$2 = %s\n", str);)
            sprintf(str, "%.*s", (pmatch[3].rm_eo-pmatch[3].rm_so), &line[pmatch[3].rm_so]);
            //_D_(printf("$3 = %s\n", str);)
            result = true;
        }
        regfree(&preg);

        if (result) {
            break;
        }
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバッグログ出力モード設定コマンドオプション設定処理関数
//!
//! デバッグログモード変更のオプションを設定する。
//!
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_dbglog_set_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool   result = false;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_dbglog_set_option Parameter Check NG.\n");)
        return false;
    }

    // オプション解析
    result = opt_parse_dbglog(&opt[0], command);
    if (!result) {
        return result;
    }

    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery モード値設定コマンドオプション設定処理関数
//!
//! Path MTU Discovery モード値設定のオプションを設定する。
//! 
//! @param [in]  num        オプションの数
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_pmtumd_set_option(int num, char* opt[], struct m46e_command_t* command) 
{
    // 内部変数
    bool    result = false;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_pmtumd_set_option Type Check NG.\n");)
        return false;
    }

    // オプション解析
    result = opt_parse_pmtumd(&opt[0], command);

    return result;

}



///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery expire time値設定コマンドオプション設定処理関数
//!
//! Path MTU Discovery Expire Timer値設定のオプションを設定する。
//!
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_pmtutm_set_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = false;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_pmtutm_set_option Parameter Check NG.\n");)
        return false;
    }

    // オプション解析
    result = opt_parse_pmtutm(&opt[0], command);
    if (!result) {
        return result;
    }

    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief 強制フラグメントモード設定コマンドオプション設定処理関数
//!
//! 強制フラグメントモード設定のオプションを設定する。
//!
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_ffrag_set_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = false;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_ffrag_set_option Parameter Check NG.\n");)
        return false;
    }

    // オプション解析
    result = opt_parse_ffrag(&opt[0], command);
    if (!result) {
        return result;
    }

    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief デフォルトゲートウェイ設定コマンドオプション設定処理関数
//!
//! デフォルトゲートウェイ設定のオプションを設定する。
//!
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_defgw_set_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = false;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_defgw_set_option Parameter Check NG.\n");)
        return false;
    }

    // オプション解析
    result = opt_parse_defgw(&opt[0], command);
    if (!result) {
        return result;
    }

    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイスMTU設定コマンドオプション設定処理関数
//!
//! トンネルデバイスMTU設定のオプションを設定する。
//!
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_tunmtu_set_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = false;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_tunmtu_set_option Parameter Check NG.\n");)
        return false;
    }

    // オプション解析
    result = opt_parse_tunmtu(&opt[0], command);
    if (!result) {
        return result;
    }

    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief 収容デバイスMTU設定コマンドオプション設定処理関数
//!
//! 収容デバイスMTU設定のオプションを設定する。
//!
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_devmtu_set_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = false;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_devmtu_set_option Parameter Check NG.\n");)
        return false;
    }

    // 正規表現をオプションを解析し、値を設定
    result = opt_parse_devmtu(&opt[0], command);
    if (!result) {
        return result;
    }
    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス操作コマンドオプション設定処理関数
//!
//! インタフェース増減設のオプションを設定する。
//!
//! @param [in]  num        オプションの数
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_exec_cmd_inet_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = true;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_exec_inet_cmd_option Parameter Check NG.\n");)
        return false;
    }

    // 実行許容コマンドチェック
    if(!m46e_command_judge_name(opt[0])) {
        printf("Requested command is not allowed.\n");
        return false;
    }
    // コマンド列にエラー出力を標準出力に向けるコマンドを付加
    strcat(opt[0]," 2>&1");
    // コマンドオプションをコマンド構造にセット
    memcpy(&command->req.inetcmd.opt, opt[0], strlen(opt[0]));

    // コマンドオプション数をコマンド構造にセット
    command->req.inetcmd.num = num;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry追加コマンドオプション設定処理関数
//!
//! M46E-PR Entry追加コマンドのオプションを設定する。
//!
//! @param [in]  num        オプションの数
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_add_pr_entry_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = true;
    struct m46e_pr_entry_command_data* pr_data = NULL;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_add_pr_entry_option Parameter Check NG.\n");)
        return false;
    }
    // ローカル変数初期化
    pr_data = &(command->req.pr_data);

    // opt[0]:ipv4_network_address/prefix_len
    result = parse_ipv4address_pr(opt[0], &pr_data->v4addr, &pr_data->v4cidr);
    if (!result) {
        printf("fail to parse parameters 'ipv4_network_address/prefix_len'\n");
        return false;
    }
    // opt[1]:m46e-pr_prefix/prefix_len
    result = parse_ipv6address(opt[1], &pr_data->pr_prefix, &pr_data->v6cidr);
    if (!result) {
        printf("fail to parse parameters 'm46e-pr prefix/prefix_len'\n");
        return false;
    }
    // opt[2]:mode
    result = parse_bool(opt[2], &pr_data->enable);  
    if (!result) {
        printf("fail to parse parameters 'mode'\n");
        return false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry削除コマンドオプション設定処理関数
//!
//! M46E-PR Entry削除コマンドのオプションを設定する。
//!
//! @param [in]  num        オプションの数
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_del_pr_entry_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = true;
    struct m46e_pr_entry_command_data* pr_data = NULL;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_del_pr_entry_option Parameter Check NG.\n");)
        return false;
    }
    // ローカル変数初期化
    pr_data = &(command->req.pr_data);

    // opt[0]:ipv4_network_address/prefix_len
    result = parse_ipv4address_pr(opt[0], &pr_data->v4addr, &pr_data->v4cidr);
    if (!result) {
        printf("fail to parse parameters 'ipv4_network_address/prefix_len'\n");
        return false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry活性化コマンドオプション設定処理関数
//!
//! M46E-PR Entry活性化コマンドのオプションを設定する。
//!
//! @param [in]  num        オプションの数
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_enable_pr_entry_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = true;
    struct m46e_pr_entry_command_data* pr_data = NULL;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_enable_pr_entry_option Parameter Check NG.\n");)
        return false;
    }
    // ローカル変数初期化
    pr_data = &(command->req.pr_data);

    // opt[0]:ipv4_network_address/prefix_len
    result = parse_ipv4address_pr(opt[0], &pr_data->v4addr, &pr_data->v4cidr);
    if (!result) {
        printf("fail to parse parameters 'ipv4_network_address/prefix_len'\n");
        return false;
    }
    // コマンドデータにenableをセット
    pr_data->enable = true;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Entry非活性化コマンドオプション設定処理関数
//!
//! M46E-PR Entry非活性化コマンドのオプションを設定する。
//!
//! @param [in]  num        オプションの数
//! @param [in]  opt[]      オプションの配列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_disable_pr_entry_option(int num, char* opt[], struct m46e_command_t* command)
{

    // 内部変数
    bool    result = true;
    struct m46e_pr_entry_command_data* pr_data = NULL;

    // 引数チェック
    if ((opt == NULL) || (command == NULL)){
        _D_(printf("m46e_command_disable_pr_entry_option Parameter Check NG.\n");)
        return false;
    }
    // ローカル変数初期化
    pr_data = &(command->req.pr_data);

    // opt[0]:ipv4_network_address/prefix_len
    result = parse_ipv4address_pr(opt[0], &pr_data->v4addr, &pr_data->v4cidr);
    if (!result) {
        printf("fail to parse parameters 'ipv4_network_address/prefix_len'\n");
        return false;
    }
    // コマンドデータにdisableをセット
    pr_data->enable = false;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Commandの読み込み処理関数
//!
//! M46E-PR ファイル内のCommand行の読み込み処理を行う。
//!
//! @param [in]  filename   Commandファイル名
//! @param [in]  command    コマンド構造体
//! @param [in]  name       Plane Name
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_load_pr(char* filename, struct m46e_command_t* command, char* name)
{
    FILE*       fp = NULL;
    char        line[OPT_LINE_MAX] = {0};
    uint32_t    line_cnt = 0;
    bool        result = true;
    char*       cmd_opt[DYNAMIC_OPE_ARGS_NUM_MAX] = { "" };
    int         cmd_num = 0;


    // 引数チェック
    if( (filename == NULL) || (strlen(filename) == 0) ||
            (command == NULL) || (name == NULL)){
        printf("internal error\n");
        return false;
    }

    // 設定ファイルオープン
    fp = fopen(filename, "r");
    if(fp == NULL) {
        printf("No such file : %s\n", filename);
        return false;
    }

    // 一行ずつ読み込み
    while(fgets(line, sizeof(line), fp) != NULL) {

        // ラインカウンタ
        line_cnt++;

        // 改行文字を終端文字に置き換える
        line[strlen(line)-1] = '\0';
        if (line[strlen(line)-1] == '\r') {
            line[strlen(line)-1] = '\0';
        }

        // コメント行と空行はスキップ
        if((line[0] == '#') || (strlen(line) == 0)){
            continue;
        }

        // コマンドオプションの初期化
        cmd_num = 0;
        cmd_opt[0] = "";
        cmd_opt[1] = "";
        cmd_opt[2] = "";
        cmd_opt[3] = "";
        cmd_opt[4] = "";
        cmd_opt[5] = "";

        /* コマンド行の解析 */
        result = m46e_command_parse_pr_file(line, &cmd_num, cmd_opt);
        if (!result) {
            printf("internal error\n");
            result = false;
            break;
        }
        else if (cmd_num == 0) {
            // スペースとタブからなる行のためスキップ
            _D_(printf("スペースとタブからなる行のためスキップ\n");)
            continue;
        }

        /* コマンドのパラメータチェック */
        command->code = M46E_COMMAND_MAX;
        for(int i=0; opt_args[i].main != NULL; i++)
        {
            if(!strcmp(cmd_opt[0], opt_args[i].main) && !strcmp(cmd_opt[1], opt_args[i].sub)){
                command->code = opt_args[i].code;
                break;
            }
        }

        // 未対応コマンドチェック
        if(command->code == M46E_COMMAND_MAX){
            printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
            result = false;
            break;
        }

        // PR-Entry追加コマンド処理
        if (command->code == M46E_ADD_PR_ENTRY) {
            _D_(printf("M46E_ADD_PR_ENTRY\n");)
                if ( (cmd_num != ADD_PR_OPE_MIN_ARGS-3) && (cmd_num != ADD_PR_OPE_MAX_ARGS-3) ) {
                    printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                    result = false;
                    break;
                }

            // PR-Entry追加コマンドオプションの設定
            result = m46e_command_add_pr_entry_option(cmd_num-2, &cmd_opt[2], command);
            if (!result) {
                printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                result = false;
                break;
            }
        }

        // PR-Entry削除コマンド処理
        if (command->code == M46E_DEL_PR_ENTRY) {
            _D_(printf("M46E_DEL_PR_ENTRY\n");)
                if (cmd_num != DEL_PR_OPE_ARGS-3) {
                    printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                    result = false;
                    break;
                }

            // PR-Entry削除コマンドオプションの設定
            result = m46e_command_del_pr_entry_option(cmd_num-2, &cmd_opt[2], command);
            if (!result) {
                printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                result = false;
                break;
            }
        }

        // PR-Entry活性化コマンド処理
        if (command->code == M46E_ENABLE_PR_ENTRY) {
            _D_(printf("M46E_ENABLE_PR_ENTRY\n");)
                if (cmd_num != ENABLE_PR_OPE_ARGS-3) {
                    printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                    result = false;
                    break;
                }

            // PR-Entry活性化コマンドオプションの設定
            result = m46e_command_enable_pr_entry_option(cmd_num-2, &cmd_opt[2], command);
            if (!result) {
                printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                result = false;
                break;
            }
        }

        // PR-Entry非活性化コマンド処理
        if (command->code == M46E_DISABLE_PR_ENTRY) {
            _D_(printf("M46E_DISABLE_PR_ENTRY\n");)
                if (cmd_num != DISABLE_PR_OPE_ARGS-3) {
                    printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                    result = false;
                    break;
                }

            // PR-Entry非活性化コマンドオプションの設定
            result = m46e_command_disable_pr_entry_option(cmd_num-2, &cmd_opt[2], command);
            if (!result) {
                printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                result = false;
                break;
            }
        }

        /* コマンド送信 */
        result = m46e_command_pr_send(command, name);
        if (!result) {
            _D_(printf("m46e_command_pr_send NG\n");)
            result = false;
            break;
        }
    }

    fclose(fp);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Command行の解析関数
//!
//! 引数で指定された行を、区切り文字「スペース または タブ」で
//! トークンに分解し、コマンドオプションとオプション数を格納する。
//!
//! @param [in]  line       コマンド行
//! @param [out] num        コマンドオプション数
//! @param [out] cmd_opt    コマンドオプション
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_parse_pr_file(char* line, int* num, char* cmd_opt[])
{
    char *tok = NULL;
    int cnt = 0;

    // 引数チェック
    if( (line == NULL) || (num == NULL) || (cmd_opt == NULL)){
        return false;
    }

    tok = strtok(line, DELIMITER);
    while( tok != NULL ) {
        cmd_opt[cnt] = tok;
        cnt++;
        tok = strtok(NULL, DELIMITER);  /* 2回目以降 */
    }

    *num = cnt;

#if 0
    printf("cnt = %d\n", cnt);
    for (int i = 0; i < cnt; i++) {
        printf( "%s\n", cmd_opt[i]);
    }
#endif

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief M46E-PR Commandファイル読み込み用コマンド送信処理関数
//!
//! M46E-PR Commandファイルから読込んだコマンド行（1行単位）を送信する。
//!
//! @param [in]  command    コマンド構造体
//! @param [in]  name       Plane Name
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool m46e_command_pr_send(struct m46e_command_t* command, char* name)
{
    int     fd = -1;
    char    path[sizeof(((struct sockaddr_un*)0)->sun_path)] = {0};
    char*   offset = &path[1];

    // 引数チェック
    if( (command == NULL) || (name == NULL)) {
        return false;
    }

    sprintf(offset, M46E_COMMAND_SOCK_NAME, name);

    fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
    if(fd < 0){
        printf("fail to open socket : %s\n", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr))){
        printf("fail to connect M46E application(%s) : %s\n", name, strerror(errno));
        close(fd);
        return false;
    }

    int ret;
    int pty;
    char buf[256];

    ret = m46e_socket_send_cred(fd, command->code, &command->req, sizeof(command->req));
    if(ret <= 0){
        printf("fail to send command : %s\n", strerror(-ret));
        close(fd);
        return false;
    }

    ret = m46e_socket_recv(fd, &command->code, &command->res, sizeof(command->res), &pty);
    if(ret <= 0){
        printf("fail to receive response : %s\n", strerror(-ret));
        close(fd);
        return false;
    }
    if(command->res.result != 0){
        printf("receive error response : %s\n", strerror(command->res.result));
        close(fd);
        return false;
    }

    switch(command->code){
    case M46E_ADD_PR_ENTRY:
    case M46E_ENABLE_PR_ENTRY:
    case M46E_DISABLE_PR_ENTRY:
    case M46E_DEL_PR_ENTRY:
        // 出力結果がソケット経由で送信されてくるので、そのまま標準出力に書き込む
        while(1){
            ret = read(fd, buf, sizeof(buf));
            if(ret > 0){
                ret = write(STDOUT_FILENO, buf, ret);
            }
            else{
                break;
            }
        }
        close(fd);
        break;

    default:
        // ありえない
        close(fd);
        break;
    }

    return true;
}

