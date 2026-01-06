#include "accounts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * server/accounts.c
 * - DB dạng text file (data/users.db) để đơn giản hoá việc deploy/chạy thử.
 * - Không lưu plaintext password: lưu (salt, hash) và verify khi LOGIN.
 * - Dùng mutex để tránh race khi nhiều thread đọc/ghi file cùng lúc.
 *
 * Lưu ý: thuật toán hash hiện tại chỉ để phục vụ đồ án môn học, không dùng production.
 */

static pthread_mutex_t g_accounts_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_db_path[512] = {0};

static int ensure_data_dir(const char* path)
{
    // Tối giản: nếu path bắt đầu bằng "data/" thì đảm bảo thư mục "data" tồn tại.
    if (strncmp(path, "data/", 5) == 0) {
        mkdir("data", 0755);
    }
    return 0;
}

static int is_valid_username(const char* s)
{
    size_t n = strlen(s);
    if (n < 3 || n > ACC_USERNAME_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_')) return 0;
    }
    return 1;
}

static int is_valid_password(const char* s)
{
    size_t n = strlen(s);
    if (n < 6 || n > ACC_PASSWORD_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == ' ') return 0;
    }
    return 1;
}

static int is_valid_email(const char* s)
{
    size_t n = strlen(s);
    if (n < 5 || n > ACC_EMAIL_MAX) return 0;
    const char* at = strchr(s, '@');
    if (!at || at == s) return 0;
    const char* dot = strchr(at + 1, '.');
    if (!dot || dot == at + 1) return 0;
    if (dot[1] == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == ' ') return 0;
    }
    return 1;
}

static unsigned long long fnv1a64(const char* s)
{
    unsigned long long h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static void hex64(unsigned long long v, char out[17])
{
    static const char* hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        out[i] = hex[v & 0xF];
        v >>= 4;
    }
    out[16] = 0;
}

static void random_hex(char* out, size_t out_len)
{
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned int)(time(NULL) ^ getpid()));
    }

    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i + 1 < out_len; i++) {
        out[i] = hex[rand() % 16];
    }
    out[out_len - 1] = 0;
}

static void compute_password_hash(const char* salt, const char* password, char out_hash[17])
{
    // Không đủ mạnh về mặt mật mã; chỉ đủ để tránh lưu plaintext.
    char buf[256];
    snprintf(buf, sizeof(buf), "%s:%s", salt, password);
    hex64(fnv1a64(buf), out_hash);
}

static int read_next_id_unlocked(FILE* f, int* out_next_id)
{
    int max_id = 0;
    char line[512];
    rewind(f);

    while (fgets(line, sizeof(line), f)) {
        int id = 0;
        char username[ACC_USERNAME_MAX + 8];
        char salt[64];
        char hash[32];
        char email[ACC_EMAIL_MAX + 8];
        int active = 0;

        if (sscanf(line, "%d|%31[^|]|%63[^|]|%31[^|]|%95[^|]|%d", &id, username, salt, hash, email, &active) == 6) {
            if (id > max_id) max_id = id;
        }
    }

    *out_next_id = max_id + 1;
    return 0;
}

int accounts_init(const char* db_path)
{
    // Khởi tạo đường dẫn DB và tạo file nếu chưa tồn tại (thread-safe).
    if (!db_path || !db_path[0]) return ACC_ERR_INVALID;

    pthread_mutex_lock(&g_accounts_mutex);
    strncpy(g_db_path, db_path, sizeof(g_db_path) - 1);
    g_db_path[sizeof(g_db_path) - 1] = 0;

    ensure_data_dir(g_db_path);

    FILE* f = fopen(g_db_path, "a+");
    if (!f) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return ACC_ERR_IO;
    }
    fclose(f);

    pthread_mutex_unlock(&g_accounts_mutex);
    return ACC_OK;
}

int accounts_username_exists(const char* username)
{
    // Helper: scan file DB để kiểm tra trùng username.
    if (!username || !username[0]) return 0;

    pthread_mutex_lock(&g_accounts_mutex);

    FILE* f = fopen(g_db_path, "r");
    if (!f) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return 0;
    }

    char line[512];
    int exists = 0;
    while (fgets(line, sizeof(line), f)) {
        int id = 0;
        char file_user[ACC_USERNAME_MAX + 8];
        char salt[64];
        char hash[32];
        char email[ACC_EMAIL_MAX + 8];
        int active = 0;

        if (sscanf(line, "%d|%31[^|]|%63[^|]|%31[^|]|%95[^|]|%d", &id, file_user, salt, hash, email, &active) == 6) {
            if (strcmp(file_user, username) == 0) {
                exists = 1;
                break;
            }
        }
    }

    fclose(f);
    pthread_mutex_unlock(&g_accounts_mutex);
    return exists;
}

int accounts_register(const char* username,
                      const char* password,
                      const char* email,
                      int* out_user_id)
{
    /*
     * Flow:
     * - Validate input
     * - Lock mutex
     * - Check duplicate username
     * - Compute (salt, hash)
     * - Append 1 dòng vào DB và trả về user_id mới
     */
    if (!out_user_id) return ACC_ERR_INVALID;
    *out_user_id = 0;

    if (!username || !password || !email) return ACC_ERR_INVALID;
    if (!is_valid_username(username) || !is_valid_password(password) || !is_valid_email(email)) {
        return ACC_ERR_INVALID;
    }

    pthread_mutex_lock(&g_accounts_mutex);

    FILE* f = fopen(g_db_path, "r+");
    if (!f) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return ACC_ERR_IO;
    }

    // Check duplicate
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int id = 0;
        char file_user[ACC_USERNAME_MAX + 8];
        char salt[64];
        char hash[32];
        char file_email[ACC_EMAIL_MAX + 8];
        int active = 0;

        if (sscanf(line, "%d|%31[^|]|%63[^|]|%31[^|]|%95[^|]|%d", &id, file_user, salt, hash, file_email, &active) == 6) {
            if (strcmp(file_user, username) == 0) {
                fclose(f);
                pthread_mutex_unlock(&g_accounts_mutex);
                return ACC_ERR_EXISTS;
            }
        }
    }

    int next_id = 1;
    read_next_id_unlocked(f, &next_id);

    char salt[33];
    char hash[17];
    random_hex(salt, sizeof(salt));
    compute_password_hash(salt, password, hash);

    fseek(f, 0, SEEK_END);
    fprintf(f, "%d|%s|%s|%s|%s|%d\n", next_id, username, salt, hash, email, 1);
    fflush(f);

    fclose(f);
    pthread_mutex_unlock(&g_accounts_mutex);

    *out_user_id = next_id;
    return ACC_OK;
}

int accounts_authenticate(const char* username,
                          const char* password,
                          int* out_user_id)
{
    /*
     * Flow:
     * - Validate
     * - Lock mutex
     * - Tìm record theo username
     * - Verify hash(password, salt) == stored_hash
     * - Trả user_id nếu hợp lệ
     */
    if (!out_user_id) return ACC_ERR_INVALID;
    *out_user_id = 0;

    if (!username || !password) return ACC_ERR_INVALID;
    if (!is_valid_username(username) || !is_valid_password(password)) return ACC_ERR_INVALID;

    pthread_mutex_lock(&g_accounts_mutex);

    FILE* f = fopen(g_db_path, "r");
    if (!f) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return ACC_ERR_IO;
    }

    char line[512];
    int found = 0;
    int active = 0;
    int user_id = 0;
    char salt[64] = {0};
    char stored_hash[32] = {0};

    while (fgets(line, sizeof(line), f)) {
        int id = 0;
        char file_user[ACC_USERNAME_MAX + 8];
        char file_salt[64];
        char file_hash[32];
        char file_email[ACC_EMAIL_MAX + 8];
        int file_active = 0;

        if (sscanf(line, "%d|%31[^|]|%63[^|]|%31[^|]|%95[^|]|%d", &id, file_user, file_salt, file_hash, file_email, &file_active) == 6) {
            if (strcmp(file_user, username) == 0) {
                found = 1;
                active = file_active;
                user_id = id;
                snprintf(salt, sizeof(salt), "%s", file_salt);
                snprintf(stored_hash, sizeof(stored_hash), "%s", file_hash);
                break;
            }
        }
    }

    fclose(f);

    if (!found) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return ACC_ERR_NOT_FOUND;
    }

    if (!active) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return ACC_ERR_INACTIVE;
    }

    char computed[17];
    compute_password_hash(salt, password, computed);
    if (strcmp(computed, stored_hash) != 0) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return ACC_ERR_BAD_PASSWORD;
    }

    *out_user_id = user_id;
    pthread_mutex_unlock(&g_accounts_mutex);
    return ACC_OK;
}

int accounts_get_user_id(const char* username)
{
    if (!username || !username[0]) return -1;

    pthread_mutex_lock(&g_accounts_mutex);

    FILE* f = fopen(g_db_path, "r");
    if (!f) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return -1;
    }

    char line[512];
    int result = -1;

    while (fgets(line, sizeof(line), f)) {
        int id = 0;
        char file_user[ACC_USERNAME_MAX + 8];
        char salt[64];
        char hash[32];
        char email[ACC_EMAIL_MAX + 8];
        int active = 0;

        if (sscanf(line, "%d|%31[^|]|%63[^|]|%31[^|]|%95[^|]|%d", 
                   &id, file_user, salt, hash, email, &active) == 6) {
            if (strcmp(file_user, username) == 0) {
                result = id;
                break;
            }
        }
    }

    fclose(f);
    pthread_mutex_unlock(&g_accounts_mutex);
    return result;
}

int accounts_get_username(int user_id, char* out, size_t out_cap)
{
    if (!out || out_cap == 0 || user_id <= 0) return 0;
    out[0] = '\0';

    pthread_mutex_lock(&g_accounts_mutex);

    FILE* f = fopen(g_db_path, "r");
    if (!f) {
        pthread_mutex_unlock(&g_accounts_mutex);
        return 0;
    }

    char line[512];
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        int id = 0;
        char file_user[ACC_USERNAME_MAX + 8];
        char salt[64];
        char hash[32];
        char email[ACC_EMAIL_MAX + 8];
        int active = 0;

        if (sscanf(line, "%d|%31[^|]|%63[^|]|%31[^|]|%95[^|]|%d", 
                   &id, file_user, salt, hash, email, &active) == 6) {
            if (id == user_id) {
                snprintf(out, out_cap, "%s", file_user);
                found = 1;
                break;
            }
        }
    }

    fclose(f);
    pthread_mutex_unlock(&g_accounts_mutex);
    return found;
}