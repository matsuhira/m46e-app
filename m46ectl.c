/******************************************************************************/
/* ファイル名 : m46ectl.c                                                     */
/* 機能概要   : M46Eアクセス用外部コマンド ソースファイル                     */
/* 修正履歴   : 2012.08.09 T.Maeda 新規作成                                   */
/*              2013.07.08 Y.Shibata 動的定義変更機能追加                     */
/*              2013.09.12 H.Koganemaru 動的定義変更機能追加                  */
/*              2013.10.03 Y.Shibata  M46E-PR拡張機能                         */
/*              2013.11.18 H.Koganemaru Usage表示修正                         */
/*              2014.01.21 M.Iwatsubo M46E-PR外部連携機能追加                 */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <sched.h>
#include <alloca.h>
#include <signal.h>
#include <getopt.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <netinet/in.h>

#include "m46eapp_command_data.h"
#include "m46eapp_socket.h"
#include "m46ectl_command.h"


//! コマンドオプション構造体
static const struct option options[] = {
    {"name",  required_argument, 0, 'n'},
    {"help",  no_argument,       0, 'h'},
    {"usage", no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

//! コマンド引数構造体定義
struct command_arg {
    char* main;   ///< メインコマンド文字列
    char* sub;    ///< サブコマンド文字列
    int   code;   ///< コマンドコード
};

//! コマンド引数
static const struct command_arg command_args[] = {
    {"exec",     "shell", M46E_EXEC_SHELL},
    {"show",     "stat",  M46E_SHOW_STATISTIC},
    {"show",     "conf",  M46E_SHOW_CONF},
    {"show",     "pmtu",  M46E_SHOW_PMTU},
    {"shutdown", "",      M46E_SHUTDOWN},
    {"restart", "",       M46E_RESTART},
    {"add",     "device", M46E_DEVICE_ADD},
    {"del",     "device", M46E_DEVICE_DEL},
    {"set",     "debug",  M46E_SET_DEBUG_LOG},
    {"set",     "ffrag",  M46E_SET_FORCE_FRAG},
    {"set",     "pmtumd", M46E_SET_PMTUD_MODE},
    {"set",     "pmtutm", M46E_SET_PMTUD_EXPTIME},
    {"set",     "defgw",  M46E_SET_DEFAULT_GW},
    {"set",     "tunmtu", M46E_SET_TUNNEL_MTU},
    {"set",     "devmtu", M46E_SET_DEVICE_MTU},
    {"exec",     "inet",  M46E_EXEC_INET_CMD},
    {"add",     "pr",     M46E_ADD_PR_ENTRY},
    {"del",     "pr",     M46E_DEL_PR_ENTRY},
    {"delall",  "pr",     M46E_DELALL_PR_ENTRY},
    {"enable",  "pr",     M46E_ENABLE_PR_ENTRY},
    {"disable", "pr",     M46E_DISABLE_PR_ENTRY},
    {"show",    "pr",     M46E_SHOW_PR_ENTRY},
    {"load",    "pr",     M46E_LOAD_PR_COMMAND},
    {"show",     "route", M46E_SHOW_ROUTE},
    {NULL,       NULL,    M46E_COMMAND_MAX}
};

/* start */
// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif
/* end */

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
"Usage: m46ectl -n PLANE_NAME COMMAND OPTIONS\n"
"       m46ectl { -h | --help | --usage }\n"
"\n"
"where  COMMAND := { exec shell | exec inet  | show stat  | show conf  |\n"
"                    show pmtu  | set debug  | set ffrag  | set defgw  |\n"
"                    set pmtumd | set pmtutm | set tunmtu | set devmtu |\n"
"                    add device | del device | add pr     | del pr     |\n"
"                    delall pr  | enable pr  | disable pr | show pr    |\n"
"                    load pr    | shutdown   | restart }\n"

"where  OPTIONS :=\n"
"       exec inet  : 'command opt1 opt2...'\n"
"       add device :  physical_dev name ipv4_address ipv4_gateway mtu hwaddr\n"
"       del device :  physical_dev\n"
"       set debug  :  on/off\n"
"       set ffrag  :  on/off\n"
"       set defgw  :  on/off\n"
"       set pmtumd :  mode\n"
"       set pmtutm :  value\n"
"       set tunmtu :  value\n"
"       set devmtu :  device_name value\n"
"       add pr     :  ipv4_network_address/prefix_len m46e-pr_prefix/plefix_len mode\n"
"       del pr     :  ipv4_network_address/prefix_len\n"
"       enable pr  :  ipv4_network_address/prefix_len\n"
"       disable pr :  ipv4_network_address/prefix_len\n"
"       load pr    :  file_name"
"\n"
"// m46ectl command explanations // \n"
"  exec shell : Execute shell into the specified PLANE_NAME\n"
"  exec inet  : Execute command at stub network side specified PLANE_NAME\n"
"  show stat  : Show the statistics information in specified PLANE_NAME\n"
"  show conf  : Show the configuration in specified PLANE_NAME\n"
"  show pmtu  : Show the Path MTU Discovery table in specified PLANE_NAME\n"
"  set debug  : Set the debug log printing mode specified PLANE_NAME\n"
"  set ffrag  : Set the force fragment mode specified PLANE_NAME\n"
"  set defgw  : Set the default gateway to tunnel device specified PLANE_NAME\n"
"  set pmtumd : Set the Path MTU Discovery mode specified PLANE_NAME\n"
"  set pmtutm : Set the available timer value used Path MTU Discovery specified PLANE_NAME\n"
"  set tunmtu : Set the mtu size used tunnel device specified PLANE_NAME\n"
"  set devmtu : Set the mtu size used stub network managing device specified PLANE_NAME\n"
"  add device : Add the device specified PLANE_NAME\n"
"  del device : Delete the device  specified PLANE_NAME\n"
"  add pr     : Add the M46E-PR Entry to M46E-PR Table specified PLANE_NAME\n"
"  del pr     : Delete the M46E-PR Entry from SA46t-PR Table specified PLANE_NAME\n"
"  delall pr  : Delete the all M46E-PR Entry from SA46t-PR Table specified PLANE_NAME\n"
"  enable pr  : Enable the M46E-PR Entry at M46E-PR Table specified PLANE_NAME\n"
"  disable pr : Disable the M46E-PR Entry at M46E-PR Table specified PLANE_NAME\n"
"  show pr    : Show the M46E-PR Table specified PLANE_NAME\n"
"  load pr    : Load M46E-PR Command file specified PLANE_NAME\n"
"  shutdown   : Shutting down the application specified PLANE_NAME\n"
"  restart    : Restart the application specified PLANE_NAME\n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス操作コマンド凡例表示関数
//!
//! デバイス操作コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_device(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME add device physical_dev=(physical device name) [name=(device name at stub)] [ipv4_address=(IPv4 address/prefix)] [ipv4_gateway=(IPv4 address)] [mtu=(mtu value)] [hwaddr=(MAC address)]\n"
"       m46ectl -n PLANE_NAME del device physical_dev=(physical device name)\n"
"        - mandatory : 'physical_dev'\n"
"        - optional  : 'name' 'ipv4_address' 'ipv4_gateway' 'mtu' 'hwaddr' \n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバッグログ出力モード設定コマンド凡例表示関数
//!
//! デバッグログ出力モード設定コマンド実行時の引数が不正だった場合などに凡例を
//! 表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_debuglog(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME set debug [on|off]\n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery Type値設定コマンド凡例表示関数
//!
//! Path MTU Discovery Type値設定コマンド実行時の引数が不正だった場合
//! などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_pmtutype(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME set pmtutype [value]\n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Path MTU Discovery Expire Timer値設定コマンド凡例表示関数
//!
//! Path MTU Discovery Expire Timer値設定コマンド実行時の引数が不正だった場合
//! などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_pmtutm(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME set pmtutm [value]\n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 強制フラグメントモード設定コマンド凡例表示関数
//!
//! 強制フラグメントモード設定コマンド実行時の引数が不正だった場合などに凡例を
//! 表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_ffrag(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME set ffrag [on|off]\n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief デフォルトゲートウェイ設定コマンド凡例表示関数
//!
//! デフォルトゲートウェイ設定コマンド実行時の引数が不正だった場合などに凡例を
//! 表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_defgw(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME set defgw [on|off]\n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @トンネルデバイスMTU設定コマンド凡例表示関数
//!
//! トンネルデバイスMTU設定コマンド実行時の引数が不正だった場合などに凡例を
//! 表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_tunmtu(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME set tunmtu [value]\n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @収容デバイスMTU設定コマンド凡例表示関数
//!
//! 収容デバイスMTU設定コマンド実行時の引数が不正だった場合などに凡例を
//! 表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_devmtu(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME set devmtu [device_name] [value]\n"
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @StubNetwork側コマンド実行コマンド凡例表示関数
//!
//! StubNetwork側コマンド実行コマンド実行時の引数が不正だった場合などに凡例を
//! 表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_exec_cmd_inet(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME exec inet ['command opt1,opt2,...']\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @M46E-PR Entry追加コマンド凡例表示関数
//!
//! M46E-PR Entry追加コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_add_pr(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME add pr [ipv4_network_address/prefix_len] [m46e-pr_prefix/prefix_len] [enable|disable]\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @M46E-PR Entry削除コマンド凡例表示関数
//!
//! M46E-PR Entry削除コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_del_pr(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME del pr [ipv4_network_address/prefix_len]\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @M46E-PR Entry全削除コマンド凡例表示関数
//!
//! M46E-PR Entry全削除コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_delall_pr(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME delall pr\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @M46E-PR Entry活性化コマンド凡例表示関数
//!
//! M46E-PR Entry活性化コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_enable_pr(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME enable pr [ipv4_network_address/prefix_len]\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @M46E-PR Entry非活性化コマンド凡例表示関数
//!
//! M46E-PR Entry非活性化コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_disable_pr(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME disable pr [ipv4_network_address/prefix_len]\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @M46E-PR Entry表示コマンド凡例表示関数
//!
//! M46E-PR Entry表示コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_show_pr(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME show pr\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @M46E-PR PR-Commandファイル読み込みコマンド凡例表示関数
//!
//! M46E-PR PR-Commandファイル読み込みコマンド実行時の引数が
//! 不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_load_pr(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME load pr file_name\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @Config情報表示コマンド凡例表示関数
//!
//! Config情報表示コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_show_conf(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME show conf\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @統計情報表示コマンド凡例表示関数
//!
//! 統計情報表示コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_show_stat(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME show stat\n "
"\n"
    );

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @Path MTU Discovery Table表示コマンド凡例表示関数
//!
//! Path MTU Discovery Table表示コマンド実行時の引数が不正だった場合などに
//! 凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void usage_show_pmtu(void)
{
    fprintf(stderr,
"Usage: m46ectl -n PLANE_NAME show pmtu\n "
"\n"
    );

    return;
}

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static int exec_shell(int pty);
static void winsize_change(int pty);
static bool is_exit(const char read_char);

////////////////////////////////////////////////////////////////////////////////
// メイン関数
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    char* name         = NULL;
    int   option_index = 0;

    // 引数チェック
    while (1) {
        int c = getopt_long(argc, argv, "n:h", options, &option_index);
        if (c == -1){
            break;
        }

        switch (c) {
        case 'n':
            name = optarg;
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

    if(name == NULL){
        usage();
        exit(EINVAL);
    }

    if(argc <= optind){
        usage();
        exit(EINVAL);
    }

    char* cmd_main = argv[optind];
    char* cmd_sub  = (argc > (optind+1)) ? argv[optind+1] : "";

    struct m46e_command_t command = {0};
    command.code = M46E_COMMAND_MAX;
    for(int i=0; command_args[i].main != NULL; i++){
        if(!strcmp(cmd_main, command_args[i].main) && !strcmp(cmd_sub, command_args[i].sub)){
            command.code = command_args[i].code;
            break;
        }
    }

    if(command.code == M46E_COMMAND_MAX){
        usage();
        exit(EINVAL);
    }

    bool result;
    char* cmd_opt[DYNAMIC_OPE_ARGS_NUM_MAX] = { NULL };
    cmd_opt[0] = (argc > (optind+2)) ? argv[optind+2] : "";
    cmd_opt[1] = (argc > (optind+3)) ? argv[optind+3] : "";
    cmd_opt[2] = (argc > (optind+4)) ? argv[optind+4] : "";
    cmd_opt[3] = (argc > (optind+5)) ? argv[optind+5] : "";
    cmd_opt[4] = (argc > (optind+6)) ? argv[optind+6] : "";
    cmd_opt[5] = (argc > (optind+7)) ? argv[optind+7] : "";

    // デバイス増設
    if (command.code == M46E_DEVICE_ADD) {
        if ((argc < DYNAMIC_OPE_DEVICE_MIN_ARGS) || (argc > DYNAMIC_OPE_DEVICE_MAX_ARGS)) {
            usage_device();
            exit(EINVAL);
        }

        result = m46e_command_device_set_option(argc-5, cmd_opt, &command);
        if (!result) {
            exit(EINVAL);
        }
    }

    // デバイス減設
    if (command.code == M46E_DEVICE_DEL) {
        if (argc != DYNAMIC_OPE_DEVICE_MIN_ARGS) {
            usage_device();
            exit(EINVAL);
        }

        result = m46e_command_device_set_option(argc-5, cmd_opt, &command);
        if (!result) {
            exit(EINVAL);
        }
    }

    // デバッグログ出力モード設定
    if (command.code == M46E_SET_DEBUG_LOG) {
        if (argc != DYNAMIC_OPE_DBGLOG_ARGS) {
            usage_debuglog();
            exit(EINVAL);
        }

        result = m46e_command_dbglog_set_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_debuglog();
            exit(EINVAL);
        }
    }

    // PMTU動作モード設定 
    if (command.code == M46E_SET_PMTUD_MODE) {
        if (argc != DYNAMIC_OPE_PMTUMD_ARGS) {
            usage_pmtutype();
            exit(EINVAL);
        }

        result = m46e_command_pmtumd_set_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_pmtutype();
            exit(EINVAL);
        }
    }


    // PMTU保持時間設定
    if (command.code == M46E_SET_PMTUD_EXPTIME) {
        if (argc != DYNAMIC_OPE_PMTUTM_ARGS) {
            usage_pmtutm();
            exit(EINVAL);
        }

        result = m46e_command_pmtutm_set_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_pmtutm();
            exit(EINVAL);
        }
    }

    // 強制フラグメントモード設定
    if (command.code == M46E_SET_FORCE_FRAG) {
        if (argc != DYNAMIC_OPE_FFRAG_ARGS) {
            usage_ffrag();
            exit(EINVAL);
        }

        result = m46e_command_ffrag_set_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_ffrag();
            exit(EINVAL);
        }
    }

    // デフォルトゲートウェイ設定
    if (command.code == M46E_SET_DEFAULT_GW) {
        if (argc != DYNAMIC_OPE_DEFGW_ARGS) {
            usage_defgw();
            exit(EINVAL);
        }

        result = m46e_command_defgw_set_option(argc-5, cmd_opt, &command);

        if (!result) {
            usage_defgw();
            exit(EINVAL);
        }
    }

    // トンネルデバイスMTU設定
    if (command.code == M46E_SET_TUNNEL_MTU) {
        if (argc != DYNAMIC_OPE_TUNMTU_ARGS) {
            usage_tunmtu();
            exit(EINVAL);
        }

        result = m46e_command_tunmtu_set_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_tunmtu();
            exit(EINVAL);
        }
    }

    // 収容デバイスMTU設定
    if (command.code == M46E_SET_DEVICE_MTU) {
        if (argc != DYNAMIC_OPE_DEVMTU_ARGS) {
            usage_devmtu();
            exit(EINVAL);
        }

        result = m46e_command_devmtu_set_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_devmtu();
            exit(EINVAL);
        }
    }

    // EXEC INETコマンド設定
    if (command.code == M46E_EXEC_INET_CMD) {
        if (argc != DYNAMIC_OPE_EXEC_CMD_ARGS) {
            usage_exec_cmd_inet();
            exit(EINVAL);
        }

        result = m46e_command_exec_cmd_inet_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_exec_cmd_inet();
            exit(EINVAL);
        }
    }

    // PR-Entry追加コマンド設定
    if (command.code == M46E_ADD_PR_ENTRY) {
        if ((argc < ADD_PR_OPE_MIN_ARGS) || (argc > ADD_PR_OPE_MAX_ARGS)) {
            usage_add_pr();
            exit(EINVAL);
        }

        result = m46e_command_add_pr_entry_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_add_pr();
            exit(EINVAL);
        }
    }

    // PR-Entry削除コマンド設定
    if (command.code == M46E_DEL_PR_ENTRY) {
        if (argc != DEL_PR_OPE_ARGS)  {
            usage_del_pr();
            exit(EINVAL);
        }

        result = m46e_command_del_pr_entry_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_del_pr();
            exit(EINVAL);
        }
    }

    // PR-Entry全削除コマンド設定
    if (command.code == M46E_DELALL_PR_ENTRY) {
        if (argc != DELALL_PR_OPE_ARGS)  {
            usage_delall_pr();
            exit(EINVAL);
        }
    }

    // PR-Entry活性化コマンド設定
    if (command.code == M46E_ENABLE_PR_ENTRY) {
        if (argc != ENABLE_PR_OPE_ARGS)  {
            usage_enable_pr();
            exit(EINVAL);
        }

        result = m46e_command_enable_pr_entry_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_enable_pr();
            exit(EINVAL);
        }
    }

    // PR-Entry非活性化コマンド設定
    if (command.code == M46E_DISABLE_PR_ENTRY) {
        if (argc != DISABLE_PR_OPE_ARGS)  {
            usage_disable_pr();
            exit(EINVAL);
        }

        result = m46e_command_disable_pr_entry_option(argc-5, cmd_opt, &command);
        if (!result) {
            usage_disable_pr();
            exit(EINVAL);
        }
    }

    // Config情報表示
    if (command.code == M46E_SHOW_CONF) {
        if (argc != SHOW_CONF_OPE_ARGS)  {
            usage_show_conf();
            exit(EINVAL);
        }
    }

    // 統計情報表示
    if (command.code == M46E_SHOW_STATISTIC) {
        if (argc != SHOW_STAT_OPE_ARGS)  {
            usage_show_stat();
            exit(EINVAL);
        }
    }

    // Path MTU Discovery Table表示
    if (command.code == M46E_SHOW_PMTU) {
        if (argc != SHOW_PMTU_OPE_ARGS)  {
            usage_show_pmtu();
            exit(EINVAL);
        }
    }

    // M46E-PR Table表示
    if (command.code == M46E_SHOW_PR_ENTRY) {
        if (argc != SHOW_PR_OPE_ARGS)  {
            usage_show_pr();
            exit(EINVAL);
        }
    }

    // M46E-PR Commandファイル読み込み
    if (command.code == M46E_LOAD_PR_COMMAND) {
        if (argc != OPE_NUM_LOAD_PR) {
            usage_load_pr();
            exit(EINVAL);
        }

        result = m46e_command_load_pr(cmd_opt[0], &command, name);
        if (!result) {
            usage_load_pr();
            exit(EINVAL);
        }

        return 0;
    }

    int fd;
    char path[sizeof(((struct sockaddr_un*)0)->sun_path)] = {0};
    char* offset = &path[1];

    sprintf(offset, M46E_COMMAND_SOCK_NAME, name);

    fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
    if(fd < 0){
        printf("fail to open socket : %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr))){
        printf("fail to connect M46E application(%s) : %s\n", name, strerror(errno));
        close(fd);
        return -1;
    }

    int ret;
    int pty;
    char buf[256];

    ret = m46e_socket_send_cred(fd, command.code, &command.req, sizeof(command.req));
    if(ret <= 0){
        printf("fail to send command : %s\n", strerror(-ret));
        close(fd);
        return -1;
    }

    ret = m46e_socket_recv(fd, &command.code, &command.res, sizeof(command.res), &pty);
    if(ret <= 0){
        printf("fail to receive response : %s\n", strerror(-ret));
        close(fd);
        return -1;
    }
    if(command.res.result != 0){
        printf("receive error response : %s\n", strerror(command.res.result));
        close(fd);
        return -1;
    }

    switch(command.code){
    case M46E_EXEC_SHELL:
        close(fd);
        exec_shell(pty);
        break;

    case M46E_SHOW_STATISTIC:
    case M46E_SHOW_CONF:
    case M46E_SHOW_PMTU:
    case M46E_DEVICE_ADD:
    case M46E_DEVICE_DEL:
    case M46E_SET_DEBUG_LOG:
    case M46E_SET_PMTUD_MODE:
    case M46E_SET_PMTUD_EXPTIME:
    case M46E_SET_FORCE_FRAG:
    case M46E_SET_DEFAULT_GW:
    case M46E_SET_TUNNEL_MTU:
    case M46E_SET_DEVICE_MTU:
    case M46E_EXEC_INET_CMD:
    case M46E_ADD_PR_ENTRY:
    case M46E_DEL_PR_ENTRY:
    case M46E_DELALL_PR_ENTRY:
    case M46E_ENABLE_PR_ENTRY:
    case M46E_DISABLE_PR_ENTRY:
    case M46E_SHOW_PR_ENTRY:
    case M46E_LOAD_PR_COMMAND:
    case M46E_SHOW_ROUTE:

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

    case M46E_SHUTDOWN:
    case M46E_RESTART:
        close(fd);
        break;

    default:
        // ありえない
        close(fd);
        break;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief シェル実行関数
//!
//! 引数で指定された擬似端末のmasterデバイスをStubネットワーク内のシェルの
//! 入出力先としてシェルとの対話を実現する。
//!
//! @param [in] pty シェル入出力先の擬似端末masterデバイス
//!
//! @return 0固定
///////////////////////////////////////////////////////////////////////////////
static int exec_shell(int pty)
{
    int ret;
    struct termios newtios;
    struct termios oldtios;

    fd_set fds;
    char   buff[1];

    printf("\nType \"exit\" or [ Ctrl+a q ] to exit the shell\n\n");

    tcgetattr(STDIN_FILENO, &oldtios);
    newtios = oldtios;
    cfmakeraw(&newtios);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &newtios);

    // シグナル登録(Windowサイズ変更時に追従する為)
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGWINCH);
    sigprocmask(SIG_BLOCK, &sigmask, NULL);

    int sigfd = signalfd(-1, &sigmask, 0);
   
    winsize_change(pty);

    int max_fd = (pty > sigfd) ? pty : sigfd;
    max_fd++;

    while(1){
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(pty, &fds);
        FD_SET(sigfd, &fds);

        ret = select(max_fd, &fds, NULL, NULL, NULL);

        if(ret < 0){
            if(errno == EINTR){
                continue;
            }
            else{
                break;
            }
        }

        if(FD_ISSET(STDIN_FILENO, &fds)){
            ret = read(STDIN_FILENO, buff, sizeof(buff));
            if(ret > 0){
                // 終了コードのチェック
                if(is_exit(buff[0])){
                    printf("\n");
                    break;
                }
                else{
                    ret = write(pty, buff, ret);
                }
            }
            else if(ret < 0){
                break;
            }
            else{
                break;
            }
        }

        if(FD_ISSET(pty, &fds)){
            ret = read(pty, buff, sizeof(buff));
            if(ret > 0){
                ret = write(STDOUT_FILENO, buff, ret);
            }
            else if(ret < 0){
                break;
            }
            else{
                break;
            }
        }

        if(FD_ISSET(sigfd, &fds)){
            struct signalfd_siginfo siginfo;

            ret = read(sigfd, &siginfo, sizeof(siginfo));
            if(ret > 0){
                if(siginfo.ssi_signo == SIGWINCH){
                    winsize_change(pty);
                }
            }
            else if (ret < 0) {
                break;
            }
            else{
                break;
            }
        }
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldtios);
    close(pty);
    close(sigfd);
    printf("\n");

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ウィンドウサイズ変更関数
//!
//! 端末のウィンドウサイズ変更時に呼ばれるハンドラ。
//! 引数のmasterデバイスのウィンドウサイズを変更後のウィンドウサイズに設定する。
//!
//! @param [in] pty シェル入出力先の擬似端末masterデバイス
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void winsize_change(int pty)
{
    struct winsize wsz;
    if (ioctl(0, TIOCGWINSZ, &wsz) == 0){
        ioctl(pty, TIOCSWINSZ, &wsz);
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 終了コードチェック関数
//!
//! 標準入力(stdin)から入力された文字をチェックし
//! シェル終了コードが入力されているかをチェックする。
//!
//! @param [in] read_char 標準入力から読み込んだ一文字
//!
//! @retval true  シェル終了する
//! @retval false シェル終了しない
///////////////////////////////////////////////////////////////////////////////
static bool is_exit(const char read_char)
{
    static bool ctrl_push = false;

    if(read_char == ('a' & 0x1f)){
        // Ctrl+a 押下時(フラグをトグルする)
        ctrl_push = !ctrl_push;
        return false;
    }

    if((read_char == 'q') && ctrl_push){
        return true;
    }

    ctrl_push = false;

    return false;
}
