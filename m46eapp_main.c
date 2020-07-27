/******************************************************************************/
/* ファイル名 : m46eapp_main.c                                                */
/* 機能概要   : メイン関数 ソースファイル                                     */
/* 修正履歴   : 2011.12.20 T.Maeda 新規作成                                   */
/*              2012.07.11 T.MAeda Phase4向けに全面改版                       */
/*              2013.07.11 Y.Shibata 動的定義変更機能追加                     */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2011-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <sys/signalfd.h>

#include "m46eapp.h"
#include "m46eapp_config.h"
#include "m46eapp_log.h"
#include "m46eapp_network.h"
#include "m46eapp_tunnel.h"
#include "m46eapp_stub_network.h"
#include "m46eapp_backbone_mainloop.h"
#include "m46eapp_command.h"
#include "m46eapp_setup.h"
#include "m46eapp_statistics.h"
#include "m46eapp_dynamic_setting.h"
#include "m46eapp_mng_v6_route.h"
#include "m46eapp_mng_v4_route.h"
#include "m46eapp_sync_v6_route.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

//! コマンドオプション構造体
static const struct option options[] = {
    {"file",  required_argument, 0, 'f'},
    {"help",  no_argument,       0, 'h'},
    {"usage", no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

///////////////////////////////////////////////////////////////////////////////
//! @brief コマンド凡例表示関数
//!
//! コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage(void)
{
    fprintf(stderr,
"Usage: m46eapp { -f | --file } CONFIG_FILE \n"
"       m46eapp { -h | --help | --usage }\n"
"\n"
    );

    return;
}


////////////////////////////////////////////////////////////////////////////////
// メイン関数
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
    // ローカル変数宣言
    struct m46e_handler_t handler;
    int                    status;
    int                    ret;
    char*                  conf_file;
    int                    option_index;
    pthread_t              tunnel_tid;
    pthread_t              v6_sync_route_tid;


    ret          = 0;
    conf_file    = NULL;
    option_index = 0;
    tunnel_tid   = -1;
    v6_sync_route_tid = -1;


    // 引数チェック
    while (1) {
        int c = getopt_long(argc, argv, "f:h", options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'f':
            conf_file = optarg;
            break;

        case 'h':
            usage();
            exit(EXIT_SUCCESS);
            break;

        default:
            usage();
            exit(EINVAL);
            break;
        }
    }

    if(conf_file == NULL){
        usage();
        exit(EINVAL);
    }

    // デバッグ時は設定ファイル解析前に標準エラー出力有り、
    // デバッグログ出力有りで初期化する
#ifdef DEBUG
    m46e_initial_log(NULL, true);
#else
    m46e_initial_log(NULL, false);
#endif

    m46e_logging(LOG_INFO, "M46E application start!!\n");

    // シグナルの登録
    sigset_t sigmask;
    sigfillset(&sigmask);
    sigdelset(&sigmask, SIGILL);
    sigdelset(&sigmask, SIGSEGV);
    sigdelset(&sigmask, SIGBUS);
    sigprocmask(SIG_BLOCK, &sigmask, &handler.oldsigmask);

    handler.signalfd = signalfd(-1, &sigmask, 0);
    fcntl(handler.signalfd, F_SETFD, FD_CLOEXEC);

    // リスタートフラグの初期化
    m46eapp_set_flag_restart(false);

    // 設定ファイルの読み込み
    handler.conf = m46e_config_load(conf_file);
    if(handler.conf == NULL){
        m46e_logging(LOG_ERR, "fail to load config file.\n");
        return -1;
    }
    _D_(m46e_config_dump(handler.conf, STDOUT_FILENO);)

    // 設定ファイルの内容でログ情報を再初期化
    m46e_initial_log(
        handler.conf->general->plane_name,
        handler.conf->general->debug_log
    );

    // デーモン化
    if(handler.conf->general->daemon && (daemon(1, 0) != 0)){
        m46e_logging(LOG_ERR, "fail to daemonize : %s\n", strerror(errno));
        m46e_config_destruct(handler.conf);
        return -1;
    }

    // 統計情報用の共有メモリ取得＆初期化
    handler.stat_info = m46e_initial_statistics(handler.conf->filename);
    if(handler.stat_info == NULL){
        m46e_logging(LOG_ERR, "fail to initialize statistic info.\n");
        m46e_config_destruct(handler.conf);
        return -1;
    }

    // M46E prefix アドレスの格納
    if(m46e_setup_plane_prefix(&handler) != 0){
        m46e_logging(LOG_ERR, "fail to setup prefix address\n");
        m46e_finish_statistics(handler.stat_info);
        m46e_config_destruct(handler.conf);
        return -1;
    }

    // macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
    ret = m46e_set_mac_of_physicalDevice_to_localAddr( &handler );
    if(ret != 0){
        m46e_logging(LOG_ERR, "fail to move network device to stub network\n");
        m46e_command_sync_child(&handler, M46E_SETUP_FAILURE);
        // 異常終了
        ret = -1;
        goto proc_end;
    }
    // macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end

    // ネットワークデバイス生成
    if(m46e_create_network_device(&handler) != 0){
        m46e_logging(LOG_ERR, "fail to netowrk device\n");
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        // 生成したデバイスの後始末
        m46e_delete_network_device(&handler);
        m46e_finish_statistics(handler.stat_info);
        m46e_config_destruct(handler.conf);
        return -1;
    }

    // ネットワーク空間との通信用のソケット生成
    if(!m46e_command_init(&handler)){
        m46e_logging(LOG_ERR, "fail to create internal communication socket\n");
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        // 生成したデバイスの後始末
        m46e_delete_network_device(&handler);
        m46e_finish_statistics(handler.stat_info);
        m46e_config_destruct(handler.conf);
        return -1;
    }

    // ネットワーク空間との経路同期通信用のソケット生成
    if(!m46e_sync_route_command_init(&handler)){
        m46e_logging(LOG_ERR, "fail to create sync route communication socket\n");
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        // 生成したデバイスの後始末
        m46e_delete_network_device(&handler);
        m46e_finish_statistics(handler.stat_info);
        m46e_config_destruct(handler.conf);
        return -1;
    }

    // v6経路同期情報用の共有メモリ取得＆初期化
   if(!m46e_sync_route_initial_v6_table(&handler)) {
        m46e_logging(LOG_ERR, "fail to initialize v6 sync route info.\n");
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        // 生成したデバイスの後始末
        m46e_delete_network_device(&handler);
        m46e_finish_statistics(handler.stat_info);
        m46e_config_destruct(handler.conf);
        return -1;
    }

    // v4経路同期情報用の共有メモリ取得＆初期化
   if(!m46e_sync_route_initial_v4_table(&handler)) {
        m46e_logging(LOG_ERR, "fail to initialize v4 sync route info.\n");
        m46e_finish_v6_table(handler.v6_route_info);
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        // 生成したデバイスの後始末
        m46e_delete_network_device(&handler);
        m46e_finish_statistics(handler.stat_info);
        m46e_config_destruct(handler.conf);
        return -1;
    }

    // ネットワーク空間生成
    handler.stub_nw_pid = m46e_stub_nw_clone(&handler);
    if(handler.stub_nw_pid < 0){
        m46e_logging(LOG_ERR, "fail to unshare network namespace\n");
        m46e_finish_v6_table(handler.v6_route_info);
        m46e_finish_v4_table(handler.v4_route_info);
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        // 生成したデバイスの後始末
        m46e_delete_network_device(&handler);
        m46e_finish_statistics(handler.stat_info);
        m46e_config_destruct(handler.conf);
        return -1;
    }
    DEBUG_LOG("unshared network namespace pid = %u\n", handler.stub_nw_pid);

    // 親プロセス用のソケット初期化
    m46e_command_init_parent(&handler);

    // 親プロセス用の経路同期ソケット初期化
    m46e_sync_route_command_init_parent(&handler);

    // 子プロセスからの初期化完了通知待ち
    if(!m46e_command_wait_child(&handler, M46E_CHILD_INIT_END)){
        m46e_logging(LOG_ERR, "status error (waiting for CHILD_INIT_END)\n");
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        // 異常終了
        ret = -1;
        goto proc_end;
    }
    DEBUG_LOG("[parent] recv child init end\n");

    // ネットワークデバイス移動
    ret = m46e_move_network_device(&handler);
    if(ret != 0){
        m46e_logging(LOG_ERR, "fail to move network device to stub network\n");
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        m46e_command_sync_child(&handler, M46E_SETUP_FAILURE);
        // 異常終了
        ret = -1;
        goto proc_end;
    }

    // 子プロセスにネットワークデバイス移動完了通知
    DEBUG_LOG("[parent] send network device moved\n");
    m46e_command_sync_child(&handler, M46E_NETDEV_MOVED);

    // 子プロセスからのネットワークデバイス設定完了通知待ち
    if(!m46e_command_wait_child(&handler, M46E_NETWORK_CONFIGURE)){
        m46e_logging(LOG_ERR, "status error (waiting for NETWORK_CONFIGURE)\n");
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
		if( m46e_set_mac_of_physicalDevice( &handler ) != 0 ){
   			m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		}
    	// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end
        // 異常終了
        ret = -1;
        goto proc_end;
    }
    DEBUG_LOG("[parent] recv network configure\n");

    // macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
	ret = m46e_set_mac_of_physicalDevice( &handler );
	if( ret != 0 ){
   		m46e_logging(LOG_ERR, "failed to return the MAC address of the physical device \n");
		m46e_command_sync_child(&handler, M46E_SETUP_FAILURE);
		// 異常終了
		ret = -1;
		goto proc_end;
	}
    // macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end

    // Backbone側のスタートアップスクリプト実行
    m46e_backbone_startup_script(&handler);

    // Backbone側のネットワークデバイス設定
    if(m46e_setup_backbone_network(&handler) != 0){
        m46e_logging(LOG_ERR, "IPv6 tunnel device up failed\n");
        m46e_command_sync_child(&handler, M46E_SETUP_FAILURE);
        // 異常終了
        ret = -1;
        goto proc_end;
    }

    // Backbone側のネットワークデバイス起動
    if(m46e_start_backbone_network(&handler) != 0){
        m46e_command_sync_child(&handler, M46E_SETUP_FAILURE);
        // 異常終了
        ret = -1;
        goto proc_end;
    }

    // 子プロセスへ運用開始を通知
    DEBUG_LOG("[parent] send start operation\n");
    m46e_command_sync_child(&handler, M46E_START_OPERATION);

    // v6経路同期スレッド起動
    if(pthread_create(&v6_sync_route_tid, NULL, m46e_sync_route_backbone_thread, &handler) != 0){
        m46e_logging(LOG_ERR, "fail to create v6 sync route thread : %s\n", strerror(errno));
        // ここまで来ると通常運用に入っているので、
        // 子プロセスにSIGTERMを送信して正常終了させる。
        kill(handler.stub_nw_pid, SIGTERM);
    }

    // カプセリングパケット送受信スレッド起動
    if(pthread_create(&tunnel_tid, NULL, m46e_tunnel_backbone_thread, &handler) != 0){
        m46e_logging(LOG_ERR, "fail to create IPv6 tunnel thread : %s\n", strerror(errno));
        // ここまで来ると通常運用に入っているので、
        // 子プロセスにSIGTERMを送信して正常終了させる。
        kill(handler.stub_nw_pid, SIGTERM);
    }

    // mainloop
    ret = m46e_backbone_mainloop(&handler);

proc_end:
    // 子プロセス終了待ち
    DEBUG_LOG("waiting for child process end.");
    while(waitpid(handler.stub_nw_pid, &status, 0) < 0 && (errno == EINTR)){
        // シグナル割り込みの場合は処理継続
        continue;
    }
    DEBUG_LOG("child process done.");

    if(v6_sync_route_tid != -1){
        // スレッドの取り消し
        pthread_cancel(v6_sync_route_tid);
        // スレッドのjoin
        DEBUG_SYNC_LOG("waiting for v6 sync route thread end.");
        pthread_join(v6_sync_route_tid, NULL);
        DEBUG_SYNC_LOG("v6 sync route thread done.");
    }

    if(tunnel_tid != -1){
        // スレッドの取り消し
        pthread_cancel(tunnel_tid);
        // スレッドのjoin
        DEBUG_LOG("waiting for IPv6 tunnel thread end.");
        pthread_join(tunnel_tid, NULL);
        DEBUG_LOG("IPv6 tunnel thread done.");
    }

    // 後処理
    m46e_delete_network_device(&handler);
    m46e_finish_statistics(handler.stat_info);
    m46e_finish_v6_table(handler.v6_route_info);
    m46e_finish_v4_table(handler.v4_route_info);

    m46e_logging(LOG_INFO, "M46E application finish!!\n");

    m46e_config_destruct(handler.conf);

    // リスタートコマンドによる終了の場合、プロセスを再起動
    if (m46eapp_get_flag_restart()) {
        ret = execlp(argv[0], argv[0], "-f", conf_file, NULL);
        if (ret == -1) {
            m46e_logging(LOG_ERR, "fail to restart : %s\n", strerror(errno));
        }
    }

    return ret;
}
