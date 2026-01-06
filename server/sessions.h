#ifndef SESSIONS_H
#define SESSIONS_H

#include <time.h>

#define SESS_TOKEN_LEN 32

#define SESS_OK 0
#define SESS_ERR_FULL -1
#define SESS_ERR_NOT_FOUND -2
#define SESS_ERR_EXPIRED -3
#define SESS_ERR_ALREADY -4

/*
 * server/sessions.*
 * - Session lưu in-memory: token -> user_id/socket/last_activity.
 * - Có timeout để hết hạn session khi không hoạt động.
 * - Chính sách: 1 user chỉ login 1 nơi (chặn multi-login).
 */

// Khởi tạo session store; timeout_seconds <=0 sẽ dùng mặc định.
void sessions_init(int timeout_seconds);

// Tạo session mới cho user_id trên socket; trả token qua out_token.
int sessions_create(int user_id, int client_socket, char out_token[SESS_TOKEN_LEN + 1]);

// Validate token và cập nhật last_activity (để gia hạn timeout).
int sessions_validate(const char* token, int* out_user_id);

// Logout: xoá session theo token.
int sessions_destroy(const char* token);

// Xoá session gắn với socket này (gọi khi client disconnect).
void sessions_remove_by_socket(int client_socket);

// Kiểm tra user đã login ở socket khác chưa (chặn multi-login).
int sessions_is_user_logged_in(int user_id, int exclude_socket);

// Kiểm tra user có đang online không (có session active).
int sessions_is_online(int user_id);

// ============ Chat Mode (Real-time PM) ============

// Set chat partner for a user (0 to clear)
// When user A is in chat mode with user B, messages from B are pushed immediately
void sessions_set_chat_partner(int user_id, int partner_user_id);

// Get chat partner of a user (0 if not in chat mode)
int sessions_get_chat_partner(int user_id);

// Get socket of a user (for pushing messages). Returns -1 if not online.
int sessions_get_socket(int user_id);

// Check if user is in chat mode with specific partner
int sessions_is_chatting_with(int user_id, int partner_user_id);

#endif
