#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include <stddef.h>

#define ACC_USERNAME_MAX 32
#define ACC_PASSWORD_MAX 64
#define ACC_EMAIL_MAX 96

// Return codes
#define ACC_OK 0
#define ACC_ERR_IO -1
#define ACC_ERR_EXISTS -2
#define ACC_ERR_INVALID -3
#define ACC_ERR_NOT_FOUND -4
#define ACC_ERR_BAD_PASSWORD -5
#define ACC_ERR_INACTIVE -6

/*
 * server/accounts.*
 * - Quản lý tài khoản bằng file DB text: data/users.db
 * - Mỗi dòng: id|username|salt|hash|email|active
 * - Có mutex để thread-safe khi nhiều client REGISTER/LOGIN đồng thời.
 */

// Khởi tạo DB (tạo thư mục data/ và file nếu chưa có).
int accounts_init(const char* db_path);

// Đăng ký tài khoản mới.
int accounts_register(const char* username,
                      const char* password,
                      const char* email,
                      int* out_user_id);

// Xác thực username/password -> user_id.
int accounts_authenticate(const char* username,
                          const char* password,
                          int* out_user_id);

// Optional helpers
int accounts_username_exists(const char* username);

#endif
