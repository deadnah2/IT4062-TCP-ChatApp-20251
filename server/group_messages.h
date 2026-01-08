#ifndef GROUP_MESSAGES_H
#define GROUP_MESSAGES_H

#include <stddef.h>

/*
 * server/group_messages.*
 * - Module Group Message (GM) - nhắn tin trong nhóm chat.
 * - Lưu file theo format: data/gm/{group_id}.txt
 * - Content được encode Base64 để hỗ trợ space và ký tự đặc biệt.
 * - Hỗ trợ real-time delivery qua server push cho tất cả members đang trong group chat.
 */

#define GM_OK              0
#define GM_ERR_NOT_MEMBER  1   // User không phải member của group
#define GM_ERR_NOT_FOUND   2   // Group không tồn tại
#define GM_ERR_INTERNAL    3   // Lỗi IO/memory

// Khởi tạo GM module (tạo thư mục data/gm nếu chưa có)
int gm_init(void);

// Gửi group message (content phải được encode Base64)
// Return GM_OK nếu thành công, lưu msg_id vào out_msg_id
int gm_send(int from_user_id, int group_id,
            const char* content_base64, int* out_msg_id);

// Lấy history chat của group
// Return format: "msg_id:from_username:content_base64:timestamp,..."
// Message mới nhất trước, giới hạn bởi 'limit'
int gm_get_history(int user_id, int group_id,
                   char* out, size_t out_cap, int limit);

// Kiểm tra user có phải member của group không
int gm_is_member(int user_id, int group_id);

// Lấy danh sách member IDs của group (để broadcast)
// Return: số lượng members
int gm_get_member_ids(int group_id, int *out_ids, int max_ids);

// Lấy tên group
int gm_get_group_name(int group_id, char *out, size_t cap);

#endif
