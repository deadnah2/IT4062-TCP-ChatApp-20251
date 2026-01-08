#include "group_messages.h"
#include "accounts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>

/*
 * server/group_messages.c
 * - Lưu tin nhắn group vào data/gm/{group_id}.txt
 * - Format mỗi dòng: msg_id|from_user_id|content_base64|timestamp
 * - Thread-safe với mutex
 */

#define GM_DIR "data/gm"
#define GROUP_MEMBERS_DB "data/group_members.db"
#define GROUPS_DB "data/groups.db"
#define LINE_MAX 8192

static pthread_mutex_t gm_mutex = PTHREAD_MUTEX_INITIALIZER;
static int gm_next_msg_id = 1;

// ============ Helpers ============

static void get_gm_filepath(int group_id, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%d.txt", GM_DIR, group_id);
}

static int group_exists(int group_id)
{
    FILE *f = fopen(GROUPS_DB, "r");
    if (!f) return 0;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int gid;
        if (sscanf(line, "%d|", &gid) == 1) {
            if (gid == group_id) {
                fclose(f);
                return 1;
            }
        }
    }
    
    fclose(f);
    return 0;
}

// ============ Public APIs ============

int gm_init(void)
{
    // Tạo thư mục data/gm nếu chưa có
    mkdir("data", 0755);
    mkdir(GM_DIR, 0755);
    
    // Tìm max msg_id hiện có
    DIR *dir = opendir(GM_DIR);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strstr(ent->d_name, ".txt")) {
                char path[256];
                snprintf(path, sizeof(path), "%s/%s", GM_DIR, ent->d_name);
                
                FILE *f = fopen(path, "r");
                if (f) {
                    char line[LINE_MAX];
                    while (fgets(line, sizeof(line), f)) {
                        int mid;
                        if (sscanf(line, "%d|", &mid) == 1) {
                            if (mid >= gm_next_msg_id) {
                                gm_next_msg_id = mid + 1;
                            }
                        }
                    }
                    fclose(f);
                }
            }
        }
        closedir(dir);
    }
    
    return GM_OK;
}

int gm_is_member(int user_id, int group_id)
{
    char username[64];
    if (!accounts_get_username(user_id, username, sizeof(username))) {
        return 0;
    }
    
    FILE *f = fopen(GROUP_MEMBERS_DB, "r");
    if (!f) return 0;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int gid;
        char member[64];
        if (sscanf(line, "%d|%63s", &gid, member) == 2) {
            if (gid == group_id && strcmp(member, username) == 0) {
                fclose(f);
                return 1;
            }
        }
    }
    
    fclose(f);
    return 0;
}

int gm_get_member_ids(int group_id, int *out_ids, int max_ids)
{
    FILE *f = fopen(GROUP_MEMBERS_DB, "r");
    if (!f) return 0;
    
    int count = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), f) && count < max_ids) {
        int gid;
        char member[64];
        if (sscanf(line, "%d|%63s", &gid, member) == 2) {
            if (gid == group_id) {
                int uid = accounts_get_user_id(member);
                if (uid > 0) {
                    out_ids[count++] = uid;
                }
            }
        }
    }
    
    fclose(f);
    return count;
}

int gm_get_group_name(int group_id, char *out, size_t cap)
{
    FILE *f = fopen(GROUPS_DB, "r");
    if (!f) return 0;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int gid;
        char name[64], owner[64];
        long ts;
        
        if (sscanf(line, "%d|%63[^|]|%63[^|]|%ld", &gid, name, owner, &ts) >= 2) {
            if (gid == group_id) {
                snprintf(out, cap, "%s", name);
                fclose(f);
                return 1;
            }
        }
    }
    
    fclose(f);
    return 0;
}

int gm_send(int from_user_id, int group_id,
            const char* content_base64, int* out_msg_id)
{
    if (!content_base64 || !content_base64[0]) {
        return GM_ERR_INTERNAL;
    }
    
    // Kiểm tra group tồn tại
    if (!group_exists(group_id)) {
        return GM_ERR_NOT_FOUND;
    }
    
    // Kiểm tra user là member
    if (!gm_is_member(from_user_id, group_id)) {
        return GM_ERR_NOT_MEMBER;
    }
    
    pthread_mutex_lock(&gm_mutex);
    
    char filepath[256];
    get_gm_filepath(group_id, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "a");
    if (!f) {
        pthread_mutex_unlock(&gm_mutex);
        return GM_ERR_INTERNAL;
    }
    
    int msg_id = gm_next_msg_id++;
    long ts = (long)time(NULL);
    
    // Format: msg_id|from_user_id|content_base64|timestamp
    fprintf(f, "%d|%d|%s|%ld\n", msg_id, from_user_id, content_base64, ts);
    fclose(f);
    
    pthread_mutex_unlock(&gm_mutex);
    
    if (out_msg_id) *out_msg_id = msg_id;
    return GM_OK;
}

int gm_get_history(int user_id, int group_id,
                   char* out, size_t out_cap, int limit)
{
    out[0] = '\0';
    
    // Kiểm tra group tồn tại
    if (!group_exists(group_id)) {
        return GM_ERR_NOT_FOUND;
    }
    
    // Kiểm tra user là member
    if (!gm_is_member(user_id, group_id)) {
        return GM_ERR_NOT_MEMBER;
    }
    
    pthread_mutex_lock(&gm_mutex);
    
    char filepath[256];
    get_gm_filepath(group_id, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "r");
    if (!f) {
        // File chưa có = chưa có tin nhắn
        pthread_mutex_unlock(&gm_mutex);
        return GM_OK;
    }
    
    // Đọc tất cả messages vào mảng tạm
    typedef struct {
        int msg_id;
        int from_id;
        char content[4096];
        long ts;
    } TempMsg;
    
    TempMsg *msgs = malloc(sizeof(TempMsg) * 1000);
    int count = 0;
    
    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f) && count < 1000) {
        int mid, fid;
        char content[4096];
        long ts;
        
        // Parse: msg_id|from_user_id|content_base64|timestamp
        char *p = line;
        char *tok;
        
        tok = strtok(p, "|");
        if (!tok) continue;
        mid = atoi(tok);
        
        tok = strtok(NULL, "|");
        if (!tok) continue;
        fid = atoi(tok);
        
        tok = strtok(NULL, "|");
        if (!tok) continue;
        strncpy(content, tok, sizeof(content) - 1);
        content[sizeof(content) - 1] = '\0';
        
        tok = strtok(NULL, "|\n");
        if (!tok) continue;
        ts = atol(tok);
        
        msgs[count].msg_id = mid;
        msgs[count].from_id = fid;
        strcpy(msgs[count].content, content);
        msgs[count].ts = ts;
        count++;
    }
    
    fclose(f);
    pthread_mutex_unlock(&gm_mutex);
    
    // Lấy limit messages mới nhất (từ cuối)
    int start = count - limit;
    if (start < 0) start = 0;
    
    // Build output: msg_id:from_username:content_base64:timestamp,...
    size_t pos = 0;
    for (int i = start; i < count && pos < out_cap - 1; i++) {
        char from_username[64];
        if (!accounts_get_username(msgs[i].from_id, from_username, sizeof(from_username))) {
            strcpy(from_username, "unknown");
        }
        
        char entry[8192];
        snprintf(entry, sizeof(entry), "%d:%s:%s:%ld",
                 msgs[i].msg_id, from_username, msgs[i].content, msgs[i].ts);
        
        if (pos > 0 && pos < out_cap - 1) {
            out[pos++] = ',';
        }
        
        size_t elen = strlen(entry);
        if (pos + elen >= out_cap) break;
        
        strcpy(out + pos, entry);
        pos += elen;
    }
    
    free(msgs);
    return GM_OK;
}
