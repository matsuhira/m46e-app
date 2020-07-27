/******************************************************************************/
/* ファイル名 : m46eapp_socket.h                                              */
/* 機能概要   : ソケット送受信クラス ヘッダファイル                           */
/* 修正履歴   : 2012.08.08 T.Maeda 新規作成                                   */
/*              2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2012-2016                */
/******************************************************************************/
#ifndef __M46EAPP_SOCKET_H__
#define __M46EAPP_SOCKET_H__

#include "m46eapp_command_data.h"

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int m46e_socket_send(int sockfd, enum m46e_command_code command, void* data, size_t size, int fd);
int m46e_socket_recv(int sockfd, enum m46e_command_code* command, void* data, size_t size, int* fd);
int m46e_socket_send_cred(int sockfd, enum m46e_command_code command, void* data, size_t size);
int m46e_socket_recv_cred(int sockfd, enum m46e_command_code* command, void* data, size_t size);

#endif // __M46EAPP_SOCKET_H__
