#ifndef FRIENDS_H
#define FRIENDS_H

#include <stddef.h>

#define FRIEND_OK             0
#define FRIEND_ERR_SELF       1   // mời / accept chính mình
#define FRIEND_ERR_NOT_FOUND  2   // không có lời mời
#define FRIEND_ERR_EXISTS     3   // đã là bạn / mời trùng
#define FRIEND_ERR_INTERNAL   4   // lỗi DB

#pragma once

int friends_send_invite(int from_user_id, const char *to_username);
int friends_accept_invite(int to_user_id, const char *from_username);
int friends_reject_invite(int to_user_id, const char *from_username);
int friends_pending(int user_id, char *out, size_t cap);
int friends_list(int user_id, char *out, size_t cap);
int friends_delete(int user_id, const char *other_username);

#endif
