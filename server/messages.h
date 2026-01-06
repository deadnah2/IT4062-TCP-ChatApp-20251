#ifndef MESSAGES_H
#define MESSAGES_H

#include <stddef.h>

/*
 * server/messages.*
 * - Module Private Message (PM) - nhắn tin 1-1 giữa 2 user.
 * - Lưu file theo format: data/pm/{min_id}_{max_id}.txt
 * - Content được encode Base64 để hỗ trợ space và ký tự đặc biệt.
 * - Hỗ trợ real-time delivery qua server push khi recipient đang trong chat mode.
 */

#define PM_OK              0
#define PM_ERR_SELF        1   // Không thể gửi cho chính mình
#define PM_ERR_NOT_FOUND   2   // User không tồn tại
#define PM_ERR_NOT_FRIEND  3   // Chưa là friend (optional)
#define PM_ERR_INTERNAL    4   // Lỗi IO/memory

// Khởi tạo PM module (tạo thư mục data/pm nếu chưa có)
int pm_init(void);

// Gửi private message (content phải được encode Base64)
// Return PM_OK nếu thành công, lưu msg_id vào out_msg_id
int pm_send(int from_user_id, const char* to_username, 
            const char* content_base64, int* out_msg_id);

// Lấy history chat với user khác
// Return format: "msg_id:from_id:content_base64:timestamp,..."
// Message mới nhất trước, giới hạn bởi 'limit'
int pm_get_history(int user_id, const char* other_username,
                   char* out, size_t out_cap, int limit);

// Lấy danh sách conversations (các user đã chat)
// Return: "username:unread_count,..."
int pm_get_conversations(int user_id, char* out, size_t out_cap);

// Đánh dấu messages là đã đọc (khi vào chat với ai đó)
int pm_mark_read(int user_id, const char* other_username);

// ============ Base64 Utilities ============

// Encode binary data thành Base64 string
// Return độ dài chuỗi encoded, hoặc -1 nếu lỗi
int base64_encode(const unsigned char* src, size_t src_len, 
                  char* out, size_t out_cap);

// Decode Base64 string thành binary data
// Return độ dài data decoded, hoặc -1 nếu lỗi
int base64_decode(const char* src, unsigned char* out, size_t out_cap);

// Helper: encode plain text to Base64 (convenience wrapper)
int base64_encode_str(const char* text, char* out, size_t out_cap);

// Helper: decode Base64 to plain text (convenience wrapper)
int base64_decode_str(const char* b64, char* out, size_t out_cap);

#endif
