/**
 * client/client.h
 * Header dùng chung cho các module client
 * 
 * Định nghĩa:
 * - ANSI colors: Màu sắc cho terminal output
 * - Icons: Biểu tượng emoji cho UI
 * - ClientState: Struct chứa trạng thái client (socket, token, framer)
 * - Function prototypes: Khai báo các hàm từ các module khác
 */

#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>
#include <time.h>

#include "../common/framing.h"

// ===== ANSI colors =====
#define C_RESET   "\033[0m"      // Reset về mặc định
#define C_TITLE   "\033[1;36m"   // Cyan bold - Tiêu đề
#define C_MENU    "\033[1;33m"   // Yellow - Menu items
#define C_OK      "\033[1;32m"   // Green - Thành công
#define C_WARN    "\033[1;31m"   // Red - Cảnh báo/Lỗi
#define C_INFO    "\033[1;34m"   // Blue - Thông tin
#define C_DIM     "\033[2m"      // Dim - Mờ
#define C_MSG_ME    "\033[1;32m"  // Green - Tin nhắn của mình
#define C_MSG_OTHER "\033[1;36m" // Cyan - Tin nhắn người khác

// ===== Icons =====
#define ICON_USER    "👤"
#define ICON_LOGIN   "🔐"
#define ICON_LOGOUT  "🚪"
#define ICON_FRIEND  "🤝"
#define ICON_GROUP   "👥"
#define ICON_LIST    "📜"
#define ICON_INVITE  "📨"
#define ICON_EXIT    "❌"
#define ICON_RAW     "🧪"
#define ICON_ID      "🆔"
#define ICON_ONLINE  "🟢"
#define ICON_OFFLINE "⚫"
#define ICON_CHAT    "💬"
#define ICON_SEND    "➤"

// ===== Client state =====
typedef struct {
    int sock;           // Socket kết nối đến server
    LineFramer framer;  // Bộ đệm để tách message theo \r\n
    char token[128];    // Token xác thực (rỗng nếu chưa login)
    int next_id;        // ID tự tăng cho mỗi request
} ClientState;

// ===== Utilities (client_utils.c) =====
int  client_connect(const char *ip, unsigned short port);  // Kết nối TCP đến server
void trim_line(char *s);                                    // Xóa \n\r cuối chuỗi
int  send_line(int sock, const char *line);                // Gửi 1 dòng + \r\n
int  kv_get(const char *payload, const char *key, char *out, size_t out_cap);  // Parse key=value
int  parse_response(const char *line, char *kind, size_t kind_cap,             // Parse response OK/ERR
                    char *rid, size_t rid_cap, char *rest, size_t rest_cap);

// Base64 encoding/decoding
int base64_encode(const unsigned char *src, size_t src_len, char *out, size_t out_cap);
int base64_decode(const char *src, unsigned char *out, size_t out_cap);

// ===== UI (client_ui.c) =====
void menu_show(int logged_in);  // Hiển thị menu chính

// ===== Auth (client_auth.c) =====
void cmd_register(ClientState *cs);    // Đăng ký tài khoản mới
void cmd_login(ClientState *cs);       // Đăng nhập
void cmd_logout(ClientState *cs);      // Đăng xuất
void cmd_whoami(ClientState *cs);      // Xem thông tin user hiện tại
void cmd_raw_send(ClientState *cs);    // Gửi raw request (debug)
void cmd_disconnect(ClientState *cs);  // Ngắt kết nối và thoát

// ===== Friends (client_friends.c) =====
void cmd_friend_invite(ClientState *cs);   // Gửi lời mời kết bạn
void cmd_friend_pending(ClientState *cs);  // Xem/xử lý lời mời đang chờ
void cmd_friend_list(ClientState *cs);     // Xem danh sách bạn bè

// ===== Groups (client_groups.c) =====
void cmd_groups_menu(ClientState *cs);  // Menu quản lý nhóm

// ===== Private Message (client_pm.c) =====
void cmd_chat_mode(ClientState *cs);  // Vào chế độ chat 1-1

// ===== Group Message (client_gm.c) =====
void cmd_group_chat_mode(ClientState *cs, int group_id);  // Vào chế độ chat nhóm

#endif // CLIENT_H
