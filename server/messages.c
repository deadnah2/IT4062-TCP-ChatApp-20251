#include "messages.h"
#include "sessions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

/*
 * server/messages.c
 * - Private messaging with file-based storage
 * - Each conversation stored in data/pm/{min_id}_{max_id}.txt
 * - Content is Base64 encoded for safe transmission
 * - Thread-safe with mutex protection
 */

#define PM_DIR "data/pm"
#define USERS_DB_PATH "data/users.db"
#define LINE_MAX 4096
#define MSG_ID_FILE "data/pm/.msg_id"

static pthread_mutex_t pm_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_next_msg_id = 1;

// ============ Base64 Implementation ============

static const char b64_table[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const int b64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

int base64_encode(const unsigned char* src, size_t src_len, 
                  char* out, size_t out_cap)
{
    size_t out_len = ((src_len + 2) / 3) * 4;
    if (out_len + 1 > out_cap) return -1;
    
    size_t i = 0, j = 0;
    while (i < src_len) {
        unsigned int a = i < src_len ? src[i++] : 0;
        unsigned int b = i < src_len ? src[i++] : 0;
        unsigned int c = i < src_len ? src[i++] : 0;
        
        unsigned int triple = (a << 16) | (b << 8) | c;
        
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }
    
    // Padding
    size_t mod = src_len % 3;
    if (mod == 1) {
        out[j - 1] = '=';
        out[j - 2] = '=';
    } else if (mod == 2) {
        out[j - 1] = '=';
    }
    
    out[j] = '\0';
    return (int)j;
}

int base64_decode(const char* src, unsigned char* out, size_t out_cap)
{
    size_t src_len = strlen(src);
    if (src_len % 4 != 0) return -1;
    
    size_t out_len = (src_len / 4) * 3;
    if (src_len > 0 && src[src_len - 1] == '=') out_len--;
    if (src_len > 1 && src[src_len - 2] == '=') out_len--;
    
    if (out_len + 1 > out_cap) return -1;
    
    size_t i = 0, j = 0;
    while (i < src_len) {
        int a = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]]; i++;
        int b = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]]; i++;
        int c = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]]; i++;
        int d = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]]; i++;
        
        if (a < 0 || b < 0 || c < 0 || d < 0) return -1;
        
        unsigned int triple = (a << 18) | (b << 12) | (c << 6) | d;
        
        if (j < out_len) out[j++] = (triple >> 16) & 0xFF;
        if (j < out_len) out[j++] = (triple >> 8) & 0xFF;
        if (j < out_len) out[j++] = triple & 0xFF;
    }
    
    out[j] = '\0';
    return (int)out_len;
}

int base64_encode_str(const char* text, char* out, size_t out_cap)
{
    return base64_encode((const unsigned char*)text, strlen(text), out, out_cap);
}

int base64_decode_str(const char* b64, char* out, size_t out_cap)
{
    return base64_decode(b64, (unsigned char*)out, out_cap);
}

// ============ Helper Functions ============

static int get_user_id_by_username(const char* username)
{
    FILE* f = fopen(USERS_DB_PATH, "r");
    if (!f) return -1;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int id, active;
        char u[64], salt[64], hash[64], email[128];
        
        if (sscanf(line, "%d|%63[^|]|%63[^|]|%63[^|]|%127[^|]|%d",
                   &id, u, salt, hash, email, &active) == 6) {
            if (strcmp(u, username) == 0) {
                fclose(f);
                return id;
            }
        }
    }
    
    fclose(f);
    return -1;
}

static int get_username_by_id(int user_id, char* out, size_t cap)
{
    FILE* f = fopen(USERS_DB_PATH, "r");
    if (!f) return 0;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int id, active;
        char username[64], salt[64], hash[64], email[128];
        
        if (sscanf(line, "%d|%63[^|]|%63[^|]|%63[^|]|%127[^|]|%d",
                   &id, username, salt, hash, email, &active) == 6) {
            if (id == user_id) {
                snprintf(out, cap, "%s", username);
                fclose(f);
                return 1;
            }
        }
    }
    
    fclose(f);
    return 0;
}

static void get_pm_filepath(int user1_id, int user2_id, char* out, size_t cap)
{
    // Always use min_max order for consistent file naming
    int min_id = user1_id < user2_id ? user1_id : user2_id;
    int max_id = user1_id < user2_id ? user2_id : user1_id;
    snprintf(out, cap, "%s/%d_%d.txt", PM_DIR, min_id, max_id);
}

static void load_next_msg_id(void)
{
    FILE* f = fopen(MSG_ID_FILE, "r");
    if (f) {
        if (fscanf(f, "%d", &g_next_msg_id) != 1) {
            g_next_msg_id = 1;
        }
        fclose(f);
    }
}

static void save_next_msg_id(void)
{
    FILE* f = fopen(MSG_ID_FILE, "w");
    if (f) {
        fprintf(f, "%d", g_next_msg_id);
        fclose(f);
    }
}

// ============ Public API ============

int pm_init(void)
{
    pthread_mutex_lock(&pm_mutex);
    
    // Create data/pm directory
    mkdir("data", 0755);
    mkdir(PM_DIR, 0755);
    
    // Load message ID counter
    load_next_msg_id();
    
    pthread_mutex_unlock(&pm_mutex);
    return PM_OK;
}

int pm_send(int from_user_id, const char* to_username, 
            const char* content_base64, int* out_msg_id)
{
    if (!to_username || !to_username[0] || !content_base64) {
        return PM_ERR_INTERNAL;
    }
    
    // Get sender username
    char from_username[64];
    if (!get_username_by_id(from_user_id, from_username, sizeof(from_username))) {
        return PM_ERR_INTERNAL;
    }
    
    // Check not sending to self
    if (strcmp(from_username, to_username) == 0) {
        return PM_ERR_SELF;
    }
    
    // Get recipient user_id
    int to_user_id = get_user_id_by_username(to_username);
    if (to_user_id < 0) {
        return PM_ERR_NOT_FOUND;
    }
    
    pthread_mutex_lock(&pm_mutex);
    
    // Get file path
    char filepath[256];
    get_pm_filepath(from_user_id, to_user_id, filepath, sizeof(filepath));
    
    // Append message
    FILE* f = fopen(filepath, "a");
    if (!f) {
        pthread_mutex_unlock(&pm_mutex);
        return PM_ERR_INTERNAL;
    }
    
    int msg_id = g_next_msg_id++;
    long ts = (long)time(NULL);
    
    // Format: msg_id|from_id|content_base64|timestamp|read(0/1)
    fprintf(f, "%d|%d|%s|%ld|0\n", msg_id, from_user_id, content_base64, ts);
    fclose(f);
    
    save_next_msg_id();
    
    if (out_msg_id) *out_msg_id = msg_id;
    
    pthread_mutex_unlock(&pm_mutex);
    
    // Try to push to recipient if online and in chat mode
    // This is handled by the caller (handlers.c) for better separation
    
    return PM_OK;
}

int pm_get_history(int user_id, const char* other_username,
                   char* out, size_t out_cap, int limit)
{
    if (!other_username || !out) return PM_ERR_INTERNAL;
    out[0] = '\0';
    
    int other_id = get_user_id_by_username(other_username);
    if (other_id < 0) return PM_ERR_NOT_FOUND;
    
    pthread_mutex_lock(&pm_mutex);
    
    char filepath[256];
    get_pm_filepath(user_id, other_id, filepath, sizeof(filepath));
    
    FILE* f = fopen(filepath, "r");
    if (!f) {
        // No chat history yet
        pthread_mutex_unlock(&pm_mutex);
        return PM_OK;
    }
    
    // Read all messages into array (we need to reverse order - latest first)
    typedef struct {
        int msg_id;
        int from_id;
        char content[2048];
        long ts;
    } Msg;
    
    Msg* msgs = NULL;
    int msg_count = 0;
    int msg_cap = 0;
    
    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        int msg_id, from_id, read_flag;
        char content[2048];
        long ts;
        
        if (sscanf(line, "%d|%d|%2047[^|]|%ld|%d", 
                   &msg_id, &from_id, content, &ts, &read_flag) >= 4) {
            
            if (msg_count >= msg_cap) {
                msg_cap = msg_cap ? msg_cap * 2 : 64;
                msgs = realloc(msgs, msg_cap * sizeof(Msg));
            }
            
            msgs[msg_count].msg_id = msg_id;
            msgs[msg_count].from_id = from_id;
            snprintf(msgs[msg_count].content, sizeof(msgs[msg_count].content), "%s", content);
            msgs[msg_count].ts = ts;
            msg_count++;
        }
    }
    
    fclose(f);
    
    // Build output (latest first, limited)
    size_t used = 0;
    int start = msg_count - 1;
    int count = 0;
    
    // Get usernames
    char my_username[64], their_username[64];
    get_username_by_id(user_id, my_username, sizeof(my_username));
    strcpy(their_username, other_username);
    
    for (int i = start; i >= 0 && count < limit; i--) {
        char* from_name = (msgs[i].from_id == user_id) ? my_username : their_username;
        
        // Format: msg_id:from_username:content_base64:timestamp
        char entry[3000];
        int entry_len = snprintf(entry, sizeof(entry), "%d:%s:%s:%ld",
                                  msgs[i].msg_id, from_name, 
                                  msgs[i].content, msgs[i].ts);
        
        if (used + entry_len + 2 >= out_cap) break;
        
        if (used > 0) {
            out[used++] = ',';
        }
        memcpy(out + used, entry, entry_len);
        used += entry_len;
        out[used] = '\0';
        count++;
    }
    
    free(msgs);
    pthread_mutex_unlock(&pm_mutex);
    
    return PM_OK;
}

int pm_get_conversations(int user_id, char* out, size_t out_cap)
{
    if (!out) return PM_ERR_INTERNAL;
    out[0] = '\0';
    
    char my_username[64];
    if (!get_username_by_id(user_id, my_username, sizeof(my_username))) {
        return PM_ERR_INTERNAL;
    }
    
    pthread_mutex_lock(&pm_mutex);
    
    DIR* dir = opendir(PM_DIR);
    if (!dir) {
        pthread_mutex_unlock(&pm_mutex);
        return PM_OK;
    }
    
    size_t used = 0;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL) {
        int id1, id2;
        if (sscanf(entry->d_name, "%d_%d.txt", &id1, &id2) == 2) {
            // Check if this conversation involves user_id
            int other_id = -1;
            if (id1 == user_id) other_id = id2;
            else if (id2 == user_id) other_id = id1;
            
            if (other_id > 0) {
                char other_username[64];
                if (get_username_by_id(other_id, other_username, sizeof(other_username))) {
                    // Count unread messages
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "%s/%s", PM_DIR, entry->d_name);
                    
                    FILE* f = fopen(filepath, "r");
                    int unread = 0;
                    if (f) {
                        char line[LINE_MAX];
                        while (fgets(line, sizeof(line), f)) {
                            int msg_id, from_id, read_flag;
                            char content[2048];
                            long ts;
                            
                            if (sscanf(line, "%d|%d|%2047[^|]|%ld|%d",
                                       &msg_id, &from_id, content, &ts, &read_flag) >= 4) {
                                // Unread = sent by other AND not read
                                if (from_id == other_id && read_flag == 0) {
                                    unread++;
                                }
                            }
                        }
                        fclose(f);
                    }
                    
                    // Format: username:unread_count
                    char conv_entry[128];
                    int len = snprintf(conv_entry, sizeof(conv_entry), "%s:%d", 
                                       other_username, unread);
                    
                    if (used + len + 2 >= out_cap) break;
                    
                    if (used > 0) {
                        out[used++] = ',';
                    }
                    memcpy(out + used, conv_entry, len);
                    used += len;
                    out[used] = '\0';
                }
            }
        }
    }
    
    closedir(dir);
    pthread_mutex_unlock(&pm_mutex);
    
    return PM_OK;
}

int pm_mark_read(int user_id, const char* other_username)
{
    if (!other_username) return PM_ERR_INTERNAL;
    
    int other_id = get_user_id_by_username(other_username);
    if (other_id < 0) return PM_ERR_NOT_FOUND;
    
    pthread_mutex_lock(&pm_mutex);
    
    char filepath[256];
    get_pm_filepath(user_id, other_id, filepath, sizeof(filepath));
    
    FILE* in = fopen(filepath, "r");
    if (!in) {
        pthread_mutex_unlock(&pm_mutex);
        return PM_OK;
    }
    
    char tmppath[260];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", filepath);
    
    FILE* out = fopen(tmppath, "w");
    if (!out) {
        fclose(in);
        pthread_mutex_unlock(&pm_mutex);
        return PM_ERR_INTERNAL;
    }
    
    char line[LINE_MAX];
    while (fgets(line, sizeof(line), in)) {
        int msg_id, from_id, read_flag;
        char content[2048];
        long ts;
        
        if (sscanf(line, "%d|%d|%2047[^|]|%ld|%d",
                   &msg_id, &from_id, content, &ts, &read_flag) >= 4) {
            // Mark as read if from the other user
            if (from_id == other_id) {
                read_flag = 1;
            }
            fprintf(out, "%d|%d|%s|%ld|%d\n", msg_id, from_id, content, ts, read_flag);
        } else {
            fputs(line, out);
        }
    }
    
    fclose(in);
    fclose(out);
    rename(tmppath, filepath);
    
    pthread_mutex_unlock(&pm_mutex);
    return PM_OK;
}
