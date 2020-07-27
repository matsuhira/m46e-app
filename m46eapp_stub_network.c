/******************************************************************************/
/* ファイル名 : m46eapp_stub_network.c                                        */
/* 機能概要   : Stubネットワーククラス ソースファイル                         */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2013.08.29 Y.Shibata 動的定義変更機能追加                     */
/*              2013.09.13 K.Nakamura M46E-PR拡張機能 追加                    */
/*              2013.12.02 Y.Shibata 経路同期機能追加                         */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <sched.h>
#include <alloca.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <paths.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include <sys/mount.h>
#include <sys/signalfd.h>
#include <sys/utsname.h>	// CentOS7対応 2016/09/06

#include "m46eapp.h"
#include "m46eapp_stub_network.h"
#include "m46eapp_log.h"
#include "m46eapp_network.h"
#include "m46eapp_command.h"
#include "m46eapp_setup.h"
#include "m46eapp_tunnel.h"
#include "m46eapp_util.h"
#include "m46eapp_stub_mainloop.h"
#include "m46eapp_pr.h"
#include "m46eapp_sync_v4_route.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

//! procfsのマウントポイント
#define PROC_MOUNT_POINT  "/proc"

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static int stub_nw_start(void* data);


///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク生成関数
//!
//! ネットワーク空間、PID空間などを分離したStubネットワーク空間を
//! 子プロセスとして新規に生成する。
//!
//! @param [in] data  ユーザデータ(実態はM46Eハンドラ)
//!
//! @return 生成したStubネットワークのプロセスID
///////////////////////////////////////////////////////////////////////////////
pid_t m46e_stub_nw_clone(void* data)
{
    // ローカル変数宣言
    long  stack_size;
    void* stack;
    int   clone_flags;

    // ローカル変数初期化
    stack_size   = sysconf(_SC_PAGESIZE);
    stack        = alloca(stack_size);
    clone_flags  = CLONE_NEWNET;  // ネットワーク空間
    clone_flags |= CLONE_NEWUTS;  // UTS空間(ホスト名など)
    clone_flags |= CLONE_NEWPID;  // PID空間
    clone_flags |= CLONE_NEWNS;   // マウント空間

    // 上記のフラグで子プロセス生成
    return clone(stub_nw_start, stack+stack_size, (clone_flags|SIGCHLD), data);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク起動関数
//!
//! Stubネットワークの起動処理をおこない、
//! その後はmainloopで親プロセスからの要求を受信待機する。
//!
//! @param [in] data  ユーザデータ(実態はM46Eハンドラ)
//!
//! @return 処理結果(実際はexitで終了するのでこの関数がreturnすることは無い)
///////////////////////////////////////////////////////////////////////////////
static int stub_nw_start(void* data)
{
    DEBUG_LOG("child start\n");

    struct m46e_handler_t* handler = (struct m46e_handler_t*)data;

    // プロセスグループを分離
    setpgid(0, 0);

    // シグナルマスクを元に戻す
    if (sigprocmask(SIG_SETMASK, &handler->oldsigmask, NULL)) {
        m46e_logging(LOG_ERR, "failed to set sigprocmask");
        _exit(-1);
    }
    close(handler->signalfd);

    // シグナルの登録(INT,TERM,QUIT,HUP,CHILDだけ受信する)
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGTERM);
    sigaddset(&sigmask, SIGQUIT);
    sigaddset(&sigmask, SIGHUP);
    sigaddset(&sigmask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigmask, NULL);

    handler->signalfd = signalfd(-1, &sigmask, 0);
    fcntl(handler->signalfd, F_SETFD, FD_CLOEXEC);

    // 親プロセス死亡時にSIGKILLを受け取るように設定
    if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) {
        m46e_logging(LOG_ERR, "failed to set pdeath signal\n");
        _exit(-1);
    }

    m46e_command_init_child(handler);

    // 子プロセス用の経路同期ソケット初期化
    m46e_sync_route_command_init_child(handler);

    // PIDを分離しているので、管理が正常におこなわれるように
    // (psやtopコマンドの結果が正常に表示されるように)
    // procfsをマウントしなおす。
    // ※NSも分離しているので、マウントポイントはデフォルトの
    //   /procで問題ない(ホスト側に影響が出ることは無い)

// CentOS7対応 2016/09/06 chg start
//  mount("procfs", PROC_MOUNT_POINT, "proc", 0, NULL);
    struct utsname uname_buff;
    int proc_mount_flg = 1;
    if (uname(&uname_buff) == 0) {
        if( uname_buff.release[0] >= '3' ){
	    proc_mount_flg = 0;
        }
    }
    if( proc_mount_flg == 1 ){
        mount("procfs", PROC_MOUNT_POINT, "proc", 0, NULL);
    }
// CentOS7対応 2016/09/06 chg end

    // ホスト名を設定ファイルのplane_nameに書き換え
    // (必要なければ削除する)
    char* hostname = handler->conf->general->plane_name;
    if(sethostname(hostname, strlen(hostname)) != 0){
        m46e_logging(LOG_WARNING, "set host name failed");
    }

    // Stubネットワーク側のプロセス名を判りやすいように書き換え
    // (必要なければ削除する)
    prctl(PR_SET_NAME, "m46e_init");

    // 親プロセスに初期化完了を通知
    DEBUG_LOG("[child] send child init end\n");
    m46e_command_sync_parent(handler, M46E_CHILD_INIT_END);

    // 親プロセスからのネットワークデバイス移動完了通知待ち
    if(!m46e_command_wait_parent(handler, M46E_NETDEV_MOVED)){
        m46e_logging(LOG_ERR, "status error (waiting for NETDEV_MOVED)");
        _exit(-1);
    }
    DEBUG_LOG("[child] recv network device moved\n");

    // M46E-PR tableの生成
    if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR){
        handler->pr_handler = m46e_pr_init_pr_table(handler);
        if(handler->pr_handler == NULL){
            m46e_logging(LOG_ERR, "fail to create M46E-PR Table\n");
            m46e_command_sync_parent(handler, M46E_PR_TABLE_GENERATE_FAILURE);
            _exit(-1);
        }
    }

    // ネットワークデバイスの設定
    if(m46e_setup_stub_network(handler) != 0){
        m46e_command_sync_parent(handler, M46E_SETUP_FAILURE);
        _exit(-1);
    }

// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 del start
//  // 親プロセスにネットワークデバイス設定完了を通知
//  DEBUG_LOG("[child] send network configure\n");
//  m46e_command_sync_parent(handler, M46E_NETWORK_CONFIGURE);
// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 del end

    // ネットワークデバイス起動
    if(m46e_start_stub_network(handler) != 0){
        _exit(-1);
    }

// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add start
    // 親プロセスにネットワークデバイス設定完了を通知
    DEBUG_LOG("[child] send network configure\n");
    m46e_command_sync_parent(handler, M46E_NETWORK_CONFIGURE);
// macvlanのMACアドレスに物理デバイスのMacアドレスを設定する対処。 add end

    // 親プロセスからの運用開始通知待ち
    if(!m46e_command_wait_parent(handler, M46E_START_OPERATION)){
        m46e_logging(LOG_ERR, "status error (waiting for START_OPERATION)");
        _exit(-1);
    }
    DEBUG_LOG("[child] recv start operation\n");

    // Path MTU管理の起動
    handler->pmtud_handler = m46e_init_pmtud(
        handler->conf->pmtud,
        handler->conf->tunnel->ipv6.mtu
    );
    if(handler->pmtud_handler == NULL){
        m46e_logging(LOG_ERR, "fail to create Path MTU Discovery table\n");
        _exit(-1);
    }

    // Stub側のスタートアップスクリプト実行
    m46e_stub_startup_script(handler);

    // v4経路同期スレッド起動
    pthread_t v4_sync_route_tid = -1;
    if(pthread_create(&v4_sync_route_tid, NULL, m46e_sync_route_stub_thread, handler) != 0){
        m46e_logging(LOG_ERR, "fail to start v4 sync route thread : %s", strerror(errno));
        m46e_end_pmtud(handler->pmtud_handler);
        _exit(-1);
    }

    // カプセリングパケット送受信スレッド起動
    pthread_t tunnel_tid;
    if(pthread_create(&tunnel_tid, NULL, m46e_tunnel_stub_thread, handler) != 0){
        m46e_logging(LOG_ERR, "fail to start IPv4 tunnel thread : %s", strerror(errno));
        m46e_end_pmtud(handler->pmtud_handler);
        if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR){
            m46e_pr_destruct_pr_table(handler->pr_handler);
        }

        _exit(-1);
    }

    // mainloop開始
    m46e_stub_mainloop(handler);

    // v4経路同期スレッドの取り消し
    pthread_cancel(v4_sync_route_tid);

    // v4経路同期スレッドのjoin
    DEBUG_SYNC_LOG("waiting for v4 sync route thread ended.");
    pthread_join(v4_sync_route_tid, NULL);
    DEBUG_SYNC_LOG("v4 sync route thread done.");

    // スレッドの取り消し
    pthread_cancel(tunnel_tid);

    // スレッドのjoin
    DEBUG_LOG("waiting for IPv6 tunnel thread ended.");
    pthread_join(tunnel_tid, NULL);
    DEBUG_LOG("IPv6 tunnel thread done.");

    // 後処理
    m46e_end_pmtud(handler->pmtud_handler);
    if(handler->conf->general->tunnel_mode == M46E_TUNNEL_MODE_PR){
        m46e_pr_destruct_pr_table(handler->pr_handler);
    }

    // v4経路同期テーブルのデバイス情報
    delAllInterfaceInfo(handler);

    DEBUG_LOG("Stub network process end");

    // 設定情報のメモリ解放
    m46e_config_destruct(handler->conf);

    _exit(0);
}
