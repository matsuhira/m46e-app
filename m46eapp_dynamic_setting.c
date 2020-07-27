/******************************************************************************/
/* ファイル名 : m46eapp_dynamic_setting.c                                     */
/* 機能概要   : 動的定義変更 ソースファイル                                   */
/* 修正履歴   : 2013.07.08 Y.Shibata 新規作成                                 */
/*              2013.09.04 H.Koganemaru 動的定義変更機能追加                  */
/*              2013.11.15 H.Koganemaru mkstempワーニング対処                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include <linux/if_link.h>

#include "m46eapp.h"
#include "m46eapp_command.h"
#include "m46eapp_network.h"
#include "m46eapp_config.h"
#include "m46eapp_log.h"
#include "m46eapp_dynamic_setting.h"
#include "m46eapp_pr.h"
#include "m46eapp_sync_v4_route.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

//! リスタートフラグ
static bool flag_restart = false;

///////////////////////////////////////////////////////////////////////////////
//! @brief restartフラグ設定関数
//!
//! リスタートフラグのセッター
//!
//! @param [in]     flg     true/false
//!
//! @return  なし
///////////////////////////////////////////////////////////////////////////////
void m46eapp_set_flag_restart(bool flg) {
    flag_restart = flg;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief restartフラグ参照関数
//!
//! リスタートフラグのゲッター
//!
//! @param なし
//!
//! @return  true       設定
//! @return  false      未設定
///////////////////////////////////////////////////////////////////////////////
bool m46eapp_get_flag_restart(void) {
    return flag_restart;
}


#ifdef DEBUG
///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス増減設要求データの表示関数(デバッグ用)
//!
//! デバイス増減設要求データを表示する。
//!
//! @param [out]     device     デバイス情報構造体
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void print_device_data(struct m46e_device_data *data)
{
    char address[INET_ADDRSTRLEN] = { 0 };

    if (data->s_physical.is_set) {
        printf("physical_name = %s\n", data->s_physical.name);
    }

    if (data->s_virtual.is_set) {
        printf("virtual_name  = %s\n", data->s_virtual.name);
    }

    if (data->s_v4address.is_set) {
        printf("ipv4_address  = %s\n", inet_ntop(AF_INET, &data->s_v4address.address, address, sizeof(address)));
    }

    if (data->s_v4netmask.is_set) {
        printf("ipv4_netmask  = %d\n", data->s_v4netmask.netmask);
    }

    if (data->s_v4gateway.is_set) {
        printf("ipv4_gateway  = %s\n", inet_ntop(AF_INET, &data->s_v4gateway.gateway, address, sizeof(address)));
    }

    if (data->s_hwaddr.is_set) {
        printf("hwaddr        = %s\n", ether_ntoa(&data->s_hwaddr.hwaddr));
    }

    if (data->s_mtu.is_set) {
        printf("mtu           = %d\n", data->s_mtu.mtu);
    }

    return;
}
#endif

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク終了設定関数
//!
//! 仮想デバイスの減設に必要なBackboneネットワークの設定をおこなう。具体的には
//!
//!   - Backboneネットワークの経路の削除
//!
//!   をおこなう。
//!
//! @param [in]     handler     M46Eハンドラ
//! @param [in]     device      デバイス情報構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int end_backbone_network_for_del_device(struct m46e_handler_t* handler, m46e_device_t* device)
{

    struct in6_addr v6addr;
    int             prefixlen;

    // 引数チェック
    if ((handler == NULL) || (device == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(end_backbone_network_for_del_device).");
        return -1;
    }

    v6addr = handler->unicast_prefix;

    // StubネットワークのIPv4アドレスに対応した
    // Backboneネットワークの経路設定(通常モードのみ)
    if((handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_NORMAL) ||
            (handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR) ){
        DEBUG_LOG("delete backbone routing device %s\n", device->name);

        if(device->ipv4_address != NULL){
            v6addr.s6_addr32[3] = device->ipv4_address->s_addr;
            prefixlen = IPV6_PREFIX_MAX - (IPV4_PREFIX_MAX - device->ipv4_netmask);

            m46e_network_del_route(
                    AF_INET6,
                    handler->conf->tunnel->ipv6.ifindex,
                    &v6addr,
                    prefixlen,
                    NULL
                    );
        }
    }

    return 0;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief 増設時Backboneネットワーク設定関数
//!
//! 仮想デバイスの増設に必要なBackboneネットワークの設定をおこなう。具体的には
//!
//!   - 対応する物理デバイスの活性化
//!   - Backboneネットワークの経路設定
//!
//!   をおこなう。
//!
//! @param [in]     handler      M46Eハンドラ
//! @param [in]     device     デバイス情報構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int set_backbone_network_for_add_device(struct m46e_handler_t* handler, m46e_device_t* device)
{
    int             ret = -1;
    struct in6_addr v6addr;
    int             prefixlen = -1;

    // 引数チェック
    if ((handler == NULL) || (device == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(set_backbone_network_for_add_device).");
        return -1;
    }

    // 初期化
    v6addr = handler->unicast_prefix;

    // 対応する物理デバイスの活性化
    if(device->type != M46E_DEVICE_TYPE_PHYSICAL){
        ret = m46e_network_set_flags_by_name(device->physical_name, IFF_UP);
        if(ret != 0){
            m46e_logging(LOG_WARNING, "Physical device up failed : %s\n", strerror(ret));
        }
    }

    // StubネットワークのIPv4アドレスに対応した
    // Backboneネットワークの経路設定(通常モードのみ)
    if((handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_NORMAL) ||
            (handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR) ){
        DEBUG_LOG("setup backbone routing device %s\n", device->name);

        if(device->ipv4_address != NULL){
            v6addr.s6_addr32[3] = device->ipv4_address->s_addr;
            prefixlen = IPV6_PREFIX_MAX - (IPV4_PREFIX_MAX - device->ipv4_netmask);

            m46e_network_add_route(
                    AF_INET6,
                    handler->conf->tunnel->ipv6.ifindex,
                    &v6addr,
                    prefixlen,
                    NULL
                    );
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ネットワークデバイス移動関数
//!
//! 仮想デバイスをStubネットワーク空間に移動する。
//!
//! @param [in]     handler      M46Eハンドラ
//! @param [in]     device     デバイス情報構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int move_network_device(struct m46e_handler_t* handler, m46e_device_t* device)
{
    int ret;

    // 引数チェック
    if ((handler == NULL) || (device == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(move_network_device).");
        return -1;
    }

    ret = m46e_network_device_move_by_index(device->ifindex, handler->stub_nw_pid);
    if(ret != 0){
        m46e_logging(LOG_ERR, "fail to move device to stub network : %s\n", strerror(ret));
    }

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MAC-VLANデバイス生成関数
//!
//! 仮想デバイスを生成する。
//!
//! @param [in]     device     デバイス情報構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int create_network_device(m46e_device_t* device)
{
    char  devbuf[IFNAMSIZ] = { 0 };
    char* devname = NULL;
    int   ret = -1;
    int   filedes = -1;

    // 引数チェック
    if (device == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(create_network_device).");
        return -1;
    }

    // ネットワークデバイス生成
    switch(device->type){
        case M46E_DEVICE_TYPE_MACVLAN: // macvlanの場合
            snprintf(devbuf, sizeof(devbuf), "mvlanXXXXXX");
            filedes = mkstemp(devbuf);
            devname = devbuf;
            unlink(devbuf);
            ret = m46e_network_create_macvlan(devname, device);
            if(ret != 0){
                m46e_logging(LOG_ERR, "fail to create macvlan device : %s.", strerror(ret));
            }
            break;

        default:
            // なにもしない
            m46e_logging(LOG_ERR, "illegal device type : %d.", device->type);
            break;
    }

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス情報構造体の初期化関数
//!
//! デバイス情報構造体を初期化する。
//!
//! @param [out]     device     デバイス情報構造体
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void init_device(m46e_device_t* device)
{

    // 引数チェック
    if (device == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(init_device).");
        return;
    }

    // デバイス情報構造体の初期化
    device->type               = M46E_DEVICE_TYPE_NONE;
    device->name               = NULL;
    device->physical_name      = NULL;
    device->ipv4_address       = NULL;
    device->ipv4_netmask       = -1;
    device->ipv4_gateway       = NULL;
    device->ipv6_address       = NULL;
    device->ipv6_prefixlen     = -1;
    device->mtu                = -1;
    device->hwaddr             = NULL;
    device->ifindex            = -1;
    memset(&device->option, 0, sizeof(device->option));

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス情報構造体の設定関数
//!
//! 受信したデバイス増減設要求データをデバイス情報構造体へ設定する。
//!
//! @param [out]    device     デバイス情報構造体
//! @param [in]     data       デバイス増減設要求データ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void set_device(m46e_device_t* device, struct m46e_device_data* data)
{

    // 引数チェック
    if ((device == NULL) || (data == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(set_device).");
        return;
    }

    // デバイスタイプの設定
    device->type                = M46E_DEVICE_TYPE_MACVLAN;

    // MAC-VLANモードの設定
    device->option.macvlan.mode = MACVLAN_MODE_PRIVATE;

    // 対応する物理デバイス名
    if (data->s_physical.is_set) {
        device->physical_name = strdup(data->s_physical.name);
    }

    // 仮想デバイス名
    if (data->s_virtual.is_set) {
        device->name = strdup(data->s_virtual.name);
    } else {
        // 仮想デバイス名が設定されてい場合は、
        // 物理デバイス名を使用
        device->name = strdup(data->s_physical.name);
    }

    // 仮想デバイスに設定するIPv4アドレス
    if (data->s_v4address.is_set) {
        device->ipv4_address = malloc(sizeof(struct in_addr));
        memcpy(device->ipv4_address, &data->s_v4address.address, sizeof(struct in_addr));
    }

    // 仮想デバイスに設定するIPv4サブネットマスク
    if (data->s_v4netmask.is_set) {
        device->ipv4_netmask = data->s_v4netmask.netmask;
    }

    // デフォルトゲートウェイアドレス
    if (data->s_v4gateway.is_set) {
        device->ipv4_gateway = malloc(sizeof(struct in_addr));
        memcpy(device->ipv4_gateway, &data->s_v4gateway.gateway, sizeof(struct in_addr));
    }

    // 仮想デバイスのMACアドレス
    if (data->s_hwaddr.is_set) {
        device->hwaddr = malloc(sizeof(struct ether_addr));
        memcpy(device->hwaddr, &data->s_hwaddr.hwaddr, sizeof(struct ether_addr));
    }

    // 仮想デバイスのMTU長
    if (data->s_mtu.is_set) {
        device->mtu = data->s_mtu.mtu;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス増設時Stubネットワーク起動関数
//!
//! 仮想デバイスの起動をおこなう。具体的には
//!
//!   - 移動した仮想デバイスのUP
//!   - 移動した仮想デバイスのデフォルトゲートウェイの設定
//!
//!   をおこなう。
//!
//! @param [in]     device      デバイス情報
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int start_stub_network_for_add_device(m46e_device_t* device)
{

    // 引数チェック
    if (device == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(start_stub_network_for_add_device).");
        return -1;
    }

    // デバイスの活性化
    m46e_network_set_flags_by_index(device->ifindex, IFF_UP);

    // デフォルトゲートウェイの設定
    if(device->ipv4_gateway != NULL){
        m46e_network_add_gateway(
                AF_INET,
                device->ifindex,
                device->ipv4_gateway
                );
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク終了設定関数
//!
//! 仮想デバイスの減設に必要なStubネットワークの設定をおこなう。具体的には
//!
//!   - 仮想デバイスの非活性化
//!
//!   をおこなう。
//!
//! @param [in]     handler     M46Eハンドラ
//! @param [in]     device      デバイス情報構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int end_stub_network_for_del_device(m46e_device_t* device)
{
    // 引数チェック
    if (device == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(end_stub_network_for_del_device).");
        return -1;
    }

    // デバイスの非活性化
    m46e_network_set_flags_by_index(device->ifindex, -(IFF_UP | IFF_RUNNING));

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス増設時Stubネットワーク設定関数
//!
//! 仮想デバイスの増設に必要はStubネットワークの設定をおこなう。具体的には
//!
//!   - 移動した仮想デバイスのデバイス名変更
//!   - 移動した仮想デバイスへのIPv4アドレスの割り当て
//!   - 移動した仮想デバイスのMACアドレス変更
//!
//!   をおこなう。
//!
//! @param [in]     device      デバイス情報
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int set_stub_network_for_add_device(m46e_device_t* device)
{
    int ret;

    // 引数チェック
    if (device == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(set_stub_network_for_add_device).");
        return -1;
    }

    // デバイスの名前設定
    m46e_network_device_rename_by_index(device->ifindex, device->name);

    // デバイスのIPv4アドレス設定
    if(device->ipv4_address != NULL){
        m46e_network_add_ipaddr(
                AF_INET,
                device->ifindex,
                device->ipv4_address,
                device->ipv4_netmask
                );
    }

    // MACアドレスの設定
    if(device->hwaddr != NULL){
        ret = m46e_network_set_hwaddr_by_name(device->name, device->hwaddr);
    }
    else{
        device->hwaddr = malloc(sizeof(struct ether_addr));
        ret = m46e_network_get_hwaddr_by_name(device->name, device->hwaddr);
    }
    if(ret != 0){
        m46e_logging(LOG_WARNING, "%s configure hwaddr error : %s\n", device->name, strerror(ret));
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ネットワークデバイス削除関数
//!
//! 仮想デバイスを削除する。
//!
//! @param [in]     device      デバイス情報
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static int delete_network_device(m46e_device_t* device)
{
    int ret;

    // 引数チェック
    if (device == NULL) {
        m46e_logging(LOG_ERR, "Parameter Check NG(end_stub_network).");
        return -1;
    }

    DEBUG_LOG("delete virtual dev index=%d %s\n", device->ifindex, device->name);
    ret = m46e_network_device_delete_by_index(device->ifindex);
    if(ret != 0){
        DEBUG_LOG("delete failed : %s\n", strerror(ret));
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス設定格納用構造体解放関数
//!
//! デバイス設定格納用構造体を解放する。
//!
//! @param [in,out] device   デバイス設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void destruct_device(m46e_device_t* device)
{
    // 引数チェック
    if(device == NULL){
        return;
    }

    // デバイス情報
    free(device->physical_name);
    device->physical_name = NULL;
    free(device->name);
    device->name = NULL;
    free(device->ipv4_address);
    device->ipv4_address = NULL;
    free(device->ipv4_gateway);
    device->ipv4_gateway = NULL;
    free(device->hwaddr);
    device->hwaddr = NULL;

    return;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス増設コマンド関数(Backbone側)
//!
//! 新規にデバイスを増設する。
//! 物理デバイスが存在しない場合、または仮想デバイス名が
//! 重複する場合は、増設しない。
//!
//! @param [in]     handler         アプリケーションハンドラー
//! @param [in]     command         コマンド構造体
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return  true       正常
//! @return  false      異常
///////////////////////////////////////////////////////////////////////////////
bool m46eapp_backbone_add_device(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    int                         ret = -1;
    m46e_config_t*             config = NULL;
    struct m46e_device_data*   data = NULL;

    // 引数チェック
    if ((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_backbone_add_device).");
        return false;
    }

    // 初期化
    config = handler->conf;
    data = &(command->req.dev_data);

    // デバッグ用
    _D_(print_device_data(data);)

    // 物理デバイスの存在チェック
    if(if_nametoindex(data->s_physical.name) == 0){
        dprintf(fd, "physical %s device is no exists.\n", data->s_physical.name);
        m46e_logging(LOG_ERR, "physical %s device is no exists.", data->s_physical.name);
        return false;
    }

    // 物理デバイス名の重複チェック
    m46e_device_t* device = NULL;
    struct m46e_list* iter =NULL;
    m46e_list_for_each(iter, &config->device_list){
        device = iter->data;
        if (strcmp(device->physical_name, data->s_physical.name) == 0) {
            dprintf(fd, "physical %s device is exists.\n", data->s_physical.name);
            m46e_logging(LOG_ERR, "physical %s device is exists.", data->s_physical.name);
            return false;
        }
    }

    // 仮想デバイス名の重複チェック
    if (data->s_virtual.is_set) {
        m46e_list_for_each(iter, &config->device_list){
            device = iter->data;
            if (strcmp(device->name, data->s_virtual.name) == 0) {
                dprintf(fd, "virtual %s device is exists.\n", data->s_virtual.name);
                m46e_logging(LOG_ERR, "virtual %s device is exists.", data->s_virtual.name);
                return false;
            }
        }
    } else {
        // 仮想デバイス名が設定されていない場合、
        // 物理デバイス名で重複チェック
        m46e_list_for_each(iter, &config->device_list){
            device = iter->data;
            if (strcmp(device->name, data->s_physical.name) == 0) {
                dprintf(fd, "virtual %s device is exists.\n", data->s_physical.name);
                m46e_logging(LOG_ERR, "virtual %s device is exists.", data->s_physical.name);
                return false;
            }
        }
    }

    // リスト用の領域を確保
    m46e_list* node = malloc(sizeof(m46e_list));
    if(node == NULL){
        m46e_logging(LOG_ERR, "node memory allocation failed\n");
        return false;
    }

    device = malloc(sizeof(m46e_device_t));
    if(device == NULL){
        m46e_logging(LOG_ERR, "device memory allocation failed\n");
        free(node);
        return false;
    }

    // MAC-VLAN構造体の初期化
    init_device(device);

    // MAC-VLAN構造体の設定
    set_device(device, data);

    // MAC-VLANデバイスの生成
    ret = create_network_device(device);
    if(ret < 0){
        dprintf(fd, "fail to create device\n");
        m46e_logging(LOG_ERR, "fail to create device : %s\n", strerror(-ret));
        free(node);
        free(device);
        return false;
    }

    // MAC-VLANデバイスの移動
    ret = move_network_device(handler, device);
    if(ret < 0){
        dprintf(fd, "fail to move device\n");
        m46e_logging(LOG_ERR, "fail to move device : %s\n", strerror(-ret));
        delete_network_device(device);
        free(node);
        free(device);
        return false;
    }

    // Stub側へ渡すため、仮想デバイスのインデックス番号を設定
    data->ifindex = device->ifindex;

    // Stubへデバイス増設開始要求の送信
    ret = m46e_command_send_request(handler, command);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to send request to stub network : %s\n", strerror(-ret));
        free(node);
        free(device);
        return false;
    }

    // 子プロセスからのデバイス増減設完了通知待ち
    if(m46e_command_wait_child_with_result(handler, M46E_DEVICE_OPE_END) != 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "status error (waiting for M46E_DEVICE_OPE_END)\n");
        free(node);
        free(device);
        return false;
    }

    // Backbone側ネットワーク設定
    ret = set_backbone_network_for_add_device(handler, device);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to start backbone network : %s\n", strerror(-ret));
        free(node);
        free(device);
        return false;
    }

    // デバイス情報をノードに設定して、ノードをデバイスリストの最後に追加
    m46e_list_init(node);
    m46e_list_add_data(node, device);
    m46e_list_add_tail(&config->device_list, node);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス増設コマンド関数(Stub側)
//!
//! 新規にデバイスを増設する。
//! 物理デバイスが存在しない場合、または仮想デバイス名が
//! 重複する場合は、増設しない。
//!
//! @param [in]     handler         アプリケーションハンドラー
//! @param [in]     command         コマンド構造体
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return  true       正常
//! @return  false      異常
///////////////////////////////////////////////////////////////////////////////
bool m46eapp_stub_add_device(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    int                         ret = -1;
    m46e_config_t*             config = NULL;
    struct m46e_device_data*   data = NULL;

    // 引数チェック
    if ((handler == NULL) || (command == NULL)){
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_stub_add_device).");
        return false;
    }

    // 初期化
    config = handler->conf;
    data = &(command->req.dev_data);

    // デバッグ用
    _D_(print_device_data(data);)

    // リスト用の領域を確保
    m46e_list* node = malloc(sizeof(m46e_list));
    if(node == NULL){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "node memory allocation failed\n");
        return false;
    }

    m46e_device_t* device = malloc(sizeof(m46e_device_t));
    if(device == NULL){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "device memory allocation failed\n");
        free(node);
        return false;
    }

    // MAC-VLAN構造体の初期化
    init_device(device);

    // MAC-VLAN構造体の設定
    set_device(device, data);

    // 仮想デバイスのインデックス番号
    device->ifindex = data->ifindex;

    // 経路同期用デバイスリストへの追加
    if (handler->conf->general->route_sync) {
        if(!addInterfaceInfo(handler, device->ifindex)) {
            m46e_logging(LOG_ERR, "fail to add device info\n");
        }
    }

    // Stub側ネットワーク設定
    ret = set_stub_network_for_add_device(device);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to start stub network : %s\n", strerror(-ret));
        delete_network_device(device);
        free(node);
        free(device);
        return false;
    }

    // デバイスの起動処理
    ret = start_stub_network_for_add_device(device);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to start stub network : %s\n", strerror(-ret));
        delete_network_device(device);
        free(node);
        free(device);
        return false;
    }

    // デバイス情報をノードに設定して、ノードをデバイスリストの最後に追加
    m46e_list_init(node);
    m46e_list_add_data(node, device);
    m46e_list_add_tail(&config->device_list, node);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス減設コマンド関数(Backbone側)
//!
//! 既存デバイスを減設する。
//! 仮想デバイスが存在しない場合、または仮想デバイス名が
//! 最後のエントリーの場合は、減設しない。
//!
//! @param [in]     handler         アプリケーションハンドラー
//! @param [in]     command         コマンド構造体
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return  true       正常
//! @return  false      異常
///////////////////////////////////////////////////////////////////////////////
bool m46eapp_backbone_del_device(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    int                         ret;
    m46e_config_t*             config;
    struct m46e_device_data*   data;

    // 引数チェック
    if ((handler == NULL) || (command == NULL)){
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_backbone_del_device).");
        return false;
    }

    config = handler->conf;
    data = &(command->req.dev_data);
    DEBUG_LOG("name = %s\n", data->s_physical.name);

    // デバイス情報の検索
    struct m46e_list* iter;
    m46e_device_t* tmp = NULL;
    m46e_device_t* device = NULL;
    m46e_list_for_each(iter, &config->device_list){
        tmp = iter->data;
        if (strcmp(tmp->physical_name, data->s_physical.name) == 0) {
            device = tmp;
            break;
        }
    }

    // 検索できかたチェック
    if (device == NULL) {
        dprintf(fd, "physical %s device is no exists.\n", data->s_physical.name);
        return false;
    }

    // 最後エントリーかチェック。最後エントリーは削除しない。
    if ((iter->next == &(config->device_list)) && ((iter->prev == &(config->device_list)))) {
        dprintf(fd, "physical %s device is last entry.\n", data->s_physical.name);
        return false;
    }

    // 検索結果を表示(デバッグ用)
    char address[INET_ADDRSTRLEN];
    DEBUG_LOG("name = %s\n", device->name);
    DEBUG_LOG("physical_name = %s\n", device->physical_name);
    if (device->ipv4_address != NULL) {
        DEBUG_LOG("ipv4_address = %s\n", inet_ntop(AF_INET, &device->ipv4_address, address, sizeof(address)));
    }
    DEBUG_LOG("ipv4_netmask = %d\n", device->ipv4_netmask);
    if (device->ipv4_gateway != NULL) {
        DEBUG_LOG("ipv4_gateway = %s\n", inet_ntop(AF_INET, &device->ipv4_gateway, address, sizeof(address)));
    }
    if (device->hwaddr != NULL) {
        DEBUG_LOG("hwaddr = %s\n", ether_ntoa(device->hwaddr));
    }
    DEBUG_LOG("mtu = %d\n", device->mtu);
    DEBUG_LOG("ifindex = %d\n", device->ifindex);

    // Stubへ減設開始要求の送信
    ret = m46e_command_send_request(handler, command);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to send request to stub network : %s\n", strerror(-ret));
        return false;
    }

    // 子プロセスからのデバイス増減設完了通知待ち
    if(m46e_command_wait_child_with_result(handler, M46E_DEVICE_OPE_END) != 0){
        m46e_logging(LOG_ERR, "status error (waiting for M46E_DEVICE_OPE_END)\n");
        return false;
    }

    // Backbone側ネットワーク設定
    ret = end_backbone_network_for_del_device(handler, device);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to end backbone network : %s\n", strerror(-ret));
        return false;
    }

    // デバイス情報ノードをデバイスリストから削除
    destruct_device(device);
    free(device);
    m46e_list_del(iter);
    free(iter);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス減設コマンド関数(Stub側)
//!
//! 既存デバイスを減設する。
//! 仮想デバイスが存在しない場合、または仮想デバイス名が
//! 最後のエントリーの場合は、減設しない。
//!
//! @param [in]     handler         アプリケーションハンドラー
//! @param [in]     command         コマンド構造体
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return  true       正常
//! @return  false      異常
///////////////////////////////////////////////////////////////////////////////
bool m46eapp_stub_del_device(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    int                         ret;
    m46e_config_t*             config;
    struct m46e_device_data*   data;
    int                         idx;

    // 引数チェック
    if ((handler == NULL) || (command == NULL)){
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_stub_del_device).");
        return false;
    }

    config = handler->conf;
    data = &(command->req.dev_data);

    DEBUG_LOG("name = %s\n", data->s_physical.name);

    // デバイス情報の検索
    struct m46e_list* iter;
    m46e_device_t* device = NULL;
    m46e_list_for_each(iter, &config->device_list){
        device = iter->data;
        if (strcmp(device->physical_name, data->s_physical.name) == 0) {
            break;
        }
    }

    // 検索できかたチェック
    if (device == NULL) {
        dprintf(fd, "physical %s device is no exists.\n", data->s_physical.name);
        return false;
    }

    // 最後エントリーかチェック。最後エントリーは削除しない。
    if ((iter->next == &(config->device_list)) && ((iter->prev == &(config->device_list)))) {
        dprintf(fd, "physical %s device is last entry.\n", data->s_physical.name);
        return false;
    }

    // Stubネットワークの設定
    ret = end_stub_network_for_del_device(device);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to end stub network : %s\n", strerror(-ret));
        return false;
    }

    // 仮想デバイスの削除
    ret = delete_network_device(device);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to delete device : %s\n", strerror(-ret));
        return false;
    }

    // メモリを解放する前にデバイスインデックス番号の退避
    idx = device->ifindex;

    DEBUG_LOG("macvlanのリストからの削除\n");
    destruct_device(device);
    free(device);
    m46e_list_del(iter);
    free(iter);

    // 経路同期用デバイスリストからの削除
    if (handler->conf->general->route_sync) {
        if(!delInterfaceInfo(handler, idx)) {
            m46e_logging(LOG_ERR, "fail to del device info\n");
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief デバッグログ設定コマンド関数(Backbone側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_backbone_set_debug_log(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_backbone_set_debug_log).");
        return false;
    }

    // 内部変数
    int ret;
    m46e_config_t* config = handler->conf;
    struct m46e_set_debuglog_data* data = &(command->req.dlog);

    // Stubへデフォルトゲートウェイ設定要求の送信
    ret = m46e_command_send_request(handler, command);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to send request to stub network : %s\n", strerror(-ret));
        return false;
    }
    // 子プロセスからのデフォルトGW設定設完了通知待ち
    if(m46e_command_wait_child_with_result(handler, M46E_SET_DEBUG_LOG_END) != 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "return value (waiting for M46E_SET_DEBUG_LOG_END)\n");
        return false;
    }

    // Config情報を更新
    config->general->debug_log = data->mode;

    // 更新されたConfig情報設定値でログ情報を再初期化
    m46e_initial_log(
        handler->conf->general->plane_name,
        handler->conf->general->debug_log
    );

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief デバッグログ設定コマンド関数(Stub側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_stub_set_debug_log(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_stub_set_debug_log).");
        return false;
    }

    // 内部変数
    m46e_config_t* config = handler->conf;
    struct m46e_set_debuglog_data* data = &(command->req.dlog);

    // Config情報を更新
    config->general->debug_log = data->mode;

    // 更新されたConfig情報設定値でログ情報を再初期化
    m46e_initial_log(
        handler->conf->general->plane_name,
        handler->conf->general->debug_log
    );

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief PMTU保持時間設定コマンド関数(Backbone側/Stub側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_set_pmtud_expire(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_set_pmtud_expire).");
        return false;
    }

    // 内部変数
    m46e_config_t* config = handler->conf;
    struct m46e_set_pmtud_exptime_data* data = &(command->req.pmtu_exptime);

    // Config情報を更新
    config->pmtud->expire_time = data->exptime;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief PMTU TYPE設定コマンド関数(Backbone側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_set_pmtud_type_bb(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_set_pmtud_type).");
        return false;
    }

    // 内部変数
    m46e_config_t* config = handler->conf;
    struct m46e_set_pmtud_type_data* data = &(command->req.pmtu_mode);

    // Config情報を更新
    config->pmtud->type = data->type;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief PMTU TYPE設定コマンド関数(stub側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_set_pmtud_type_stub(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    DEBUG_LOG("m46eapp_set_pmtud_type_stub start.\n");
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_set_pmtud_type).");
        return false;
    }

    // 内部変数
    m46e_config_t* config = handler->conf;
    struct m46e_set_pmtud_type_data* data = &(command->req.pmtu_mode);

    //切り替え処理
    switch(config->pmtud->type){

    case 0:
        if(data->type == 1 || data->type == 2){
            handler->pmtud_handler = m46e_restart_pmtud(
                                        handler->pmtud_handler,
                                        config->tunnel->ipv6.mtu,
                                        data->type);
        }
        else{
        //他の値（data->type == 0）の場合は何もしない
        }

    break;

    case 1:
        if(data->type == 0 || data->type == 2){ 
            handler->pmtud_handler = m46e_restart_pmtud(
                                        handler->pmtud_handler,
                                        config->tunnel->ipv6.mtu,
                                        data->type);
        }
        else{
        //他の値（data->type == 1）の場合は何もしない
        }

    break;

    case 2:
        if(data->type == 0 || data->type == 1){
            handler->pmtud_handler = m46e_restart_pmtud(
                                        handler->pmtud_handler,
                                        config->tunnel->ipv6.mtu,
                                        data->type);
        }
        else{
        //他の値（data->type == 2）の場合は何もしない
        }

    break;

    default:
    //処理なし
    break;
    }

    // Config情報を更新
    config->pmtud->type = data->type;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief 強制フラグメント設定コマンド関数(Backbone側/Stub側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_set_force_fragment(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_set_force_fragment).");
        return false;
    }

    // 内部変数
    m46e_config_t* config = handler->conf;
    struct m46e_set_force_fragment_data* data = &(command->req.ffrag);

    // Config情報を更新
    config->general->force_fragment = data->mode;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief デフォルトゲートウェイ設定コマンド関数(Backbone側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_backbone_set_default_gw(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_backbone_set_default_gw).");
        return false;
    }

    // 内部変数
    int ret;
    m46e_config_t* config = handler->conf;
    struct m46e_set_default_gw_data* data = &(command->req.defgw);

    // Stubへデフォルトゲートウェイ設定要求の送信
    ret = m46e_command_send_request(handler, command);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to send request to stub network : %s\n", strerror(-ret));
        return false;
    }
    // 子プロセスからのデフォルトGW設定設完了通知待ち
    if(m46e_command_wait_child_with_result(handler, M46E_SET_DEFAULT_GW_END) != 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "return value (waiting for M46E_SET_DEFAULT_GW_END)\n");
        return false;
    }

    // デフォルトゲートウェイ登録ルート
    if(data->mode == true) {
        if(config->tunnel->ipv4.ipv4_gateway == NULL){
            // 設定領域を確保する
            config->tunnel->ipv4.ipv4_gateway = malloc(sizeof(struct in_addr));
            if(config->tunnel->ipv4.ipv4_gateway != NULL) {
                config->tunnel->ipv4.ipv4_gateway->s_addr = INADDR_ANY;
            }
        }
        else {
            // 処理無し
        }
    }
    else if(data->mode == false) {
    // デフォルトゲートウェイ削除ルート
        if(config->tunnel->ipv4.ipv4_gateway != NULL){
            // 設定領域を開放する
            free(config->tunnel->ipv4.ipv4_gateway);
            config->tunnel->ipv4.ipv4_gateway = NULL;
        }
        else {
            // 処理無し
        }
    }

    return true;

}

///////////////////////////////////////////////////////////////////////////////
////! @brief デフォルトゲートウェイ設定コマンド関数(Stub側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_stub_set_default_gw(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_stub_set_default_gw).");
        return false;
    }

    // 内部変数
    int ret;
    m46e_config_t* config = handler->conf;
    struct m46e_set_default_gw_data* data = &(command->req.defgw);

    // デフォルトゲートウェイ登録ルート
    if(data->mode == true) {
        if(config->tunnel->ipv4.ipv4_gateway == NULL){
            // 設定領域を確保する
            config->tunnel->ipv4.ipv4_gateway = malloc(sizeof(struct in_addr));
            if(config->tunnel->ipv4.ipv4_gateway != NULL) {
                config->tunnel->ipv4.ipv4_gateway->s_addr = INADDR_ANY;
            }
            // デフォルトゲートウェイをトンネルデバイスに設定する
            ret = m46e_network_add_gateway(
                      AF_INET,
                      config->tunnel->ipv4.ifindex,
                      config->tunnel->ipv4.ipv4_gateway
                  );

            if(ret != 0) {
                dprintf(fd, "internal error\n");
                m46e_logging(LOG_ERR, "fail to add default gw : %s\n", strerror(-ret));
                free(config->tunnel->ipv4.ipv4_gateway);
                config->tunnel->ipv4.ipv4_gateway = NULL;
                return false;
            }
        }
        else {
            // 既に設定領域を確保済みなので処理なし
        }
    }
    else if(data->mode == false) {
        // デフォルトゲートウェイをトンネルデバイスから削除設定する
        ret = m46e_network_del_gateway(
                  AF_INET,
                  config->tunnel->ipv4.ifindex,
                  config->tunnel->ipv4.ipv4_gateway
              );
        if(ret != 0) {
            dprintf(fd, "internal error\n");
            m46e_logging(LOG_ERR, "fail to delete default gw : %s\n", strerror(-ret));
            return false;
        }

        if(config->tunnel->ipv4.ipv4_gateway != NULL){
            // 設定領域を開放する
            free(config->tunnel->ipv4.ipv4_gateway);
            config->tunnel->ipv4.ipv4_gateway = NULL;
        }
        else {
            // そもそも設定領域は確保されていないので処理なし
        }
    }

    return true;

}

///////////////////////////////////////////////////////////////////////////////
////! @brief トンネルデバイスMTU設定コマンド関数(Backbone側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_backbone_set_tunnel_mtu(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_backbone_set_tunnel_mtu).");
        return false;
    }

    // 内部変数
    int ret;
    struct m46e_set_tunnel_mtu_data* data = &(command->req.tunmtu);
    m46e_device_t* device = &(handler->conf->tunnel->ipv6);

    // MTUの設定
    ret = m46e_network_set_mtu_by_name(device->name, data->mtu);
    if(ret != 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to set tunnel device(backbone) mtu : %s\n", strerror(-ret));
        return false;
    }

    // Config情報を更新
    device->mtu = data->mtu;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief トンネルデバイスMTU設定コマンド関数(Stub側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_stub_set_tunnel_mtu(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_stub_set_tunnel_mtu).");
        return false;
    }

    // 内部変数
    int ret;
    struct m46e_set_tunnel_mtu_data* data = &(command->req.tunmtu);
    m46e_device_t* device = &(handler->conf->tunnel->ipv4);

    // MTUの設定
    ret = m46e_network_set_mtu_by_name(device->name, data->mtu - sizeof(struct ip6_hdr));
    if(ret != 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to set tunnel device(stub) mtu : %s\n", strerror(-ret));
        return false;
    }

    // Config情報を更新
    // IPv4側のMTUは設定値からIPv6ヘッダ長(40byte)を引いた値を設定する
    device->mtu = data->mtu - sizeof(struct ip6_hdr);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief 仮想デバイスMTU設定コマンド関数(Backbone側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_backbone_set_device_mtu(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_backbone_set_device_mtu).");
        return false;
    }

    // 内部変数
    int ret;
    m46e_config_t* config = handler->conf;
    struct m46e_set_device_mtu_data* data = &(command->req.devmtu);

    // Stubへ収容デバイスMTU設定要求の送信
    ret = m46e_command_send_request(handler, command);
    if(ret < 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "fail to send request to stub network : %s\n", strerror(-ret));
        return false;
    }
    // 子プロセスからの収容デバイスMTU設定完了通知待ち
    if(m46e_command_wait_child_with_result(handler, M46E_SET_DEVICE_MTU_END) != 0){
        dprintf(fd, "internal error\n");
        m46e_logging(LOG_ERR, "return value (waiting for M46E_SET_DEVICE_MTU_END)\n");
        return false;
    }

    // デバイス情報の検索
    struct m46e_list* iter;
    m46e_device_t* device = NULL;
    m46e_list_for_each(iter, &config->device_list){
        device = iter->data;
        if (strcmp(device->name, data->name) == 0) {
            break;
        }
    }
    // 検索できたかチェック
    if (device == NULL) {
        dprintf(fd, "%s device is no exists.\n", data->name);
        m46e_logging(LOG_ERR, "fail to set pseudo device mtu\n");
        return false;
    }

    // 検索できたので値をセット
    device->mtu = data->mtu;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief 仮想デバイスMTU設定コマンド関数(Stub側)
////!
////! Config情報にコマンドで設定された値をセットする。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_stub_set_device_mtu(struct m46e_handler_t* handler, struct m46e_command_t* command, int fd)
{
    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_stub_set_device_mtu).");
        return false;
    }

    // 内部変数
    int ret;
    m46e_config_t* config = handler->conf;
    struct m46e_set_device_mtu_data* data = &(command->req.devmtu);

    // MTUの設定
    ret = m46e_network_set_mtu_by_name(data->name, data->mtu);
    if(ret != 0){
        m46e_logging(LOG_ERR, "%s configure mtu error : %s\n", data->name, strerror(ret));
        // コマンド実行エラー表示
        m46e_pr_print_error(fd, M46E_PR_COMMAND_EXEC_FAILURE);
        return false;
    }

    // デバイス情報の検索
    struct m46e_list* iter;
    m46e_device_t* device = NULL;
    m46e_list_for_each(iter, &config->device_list){
        device = iter->data;
        if (strcmp(device->name, data->name) == 0) {
            break;
        }
    }
    // 検索できたかチェック
    if (device == NULL) {
        dprintf(fd, "%s device is no exists.\n", data->name);
        m46e_logging(LOG_ERR, "fail to set pseudo device mtu\n");
        return false;
    }

    // 検索できたので値をセット
    device->mtu = data->mtu;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief Stub Network側実行コマンド要求受信関数(Stub側)
////!
////! 要求されたコマンドを実行する。
////!
////! @param [in]     handler         アプリケーションハンドラー
////! @param [in]     command         コマンド構造体
////!
////! @return  true       正常
////! @return  false      異常
/////////////////////////////////////////////////////////////////////////////////
bool m46eapp_stub_exec_cmd(struct m46e_handler_t* handler, struct m46e_command_t* command)
{

    // 引数チェック
    if((handler == NULL) || (command == NULL)) {
        m46e_logging(LOG_ERR, "Parameter Check NG(m46eapp_backbone_exec_cmd).");
        return false;
    }

    // 内部変数
    FILE *fp ;
    char buf[256];

    struct m46e_exec_cmd_inet_data* data = &(command->req.inetcmd);

    if( (fp = popen(&data->opt[0], "r")) == NULL) {
        m46e_logging(LOG_ERR, "popen error");
    }

    while(fgets(buf, 256, fp) != NULL) {
        dprintf(data->fd, "%s", buf);
    }
    (void) pclose(fp);

    return true;

}

