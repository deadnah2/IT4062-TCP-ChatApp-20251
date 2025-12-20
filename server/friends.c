#include "friends.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define USERS_DB_PATH   "data/users.db"
#define FRIENDS_DB_PATH "data/friends.db"

#define LINE_MAX 512

static pthread_mutex_t friends_mutex = PTHREAD_MUTEX_INITIALIZER;


static int get_username_by_id(int user_id, char *out, size_t cap)
{
    FILE *f = fopen(USERS_DB_PATH, "r");
    if (!f) return 0;

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        int id, active;
        char username[64], salt[64], hash[64], email[128];

        if (sscanf(line, "%d|%63[^|]|%63[^|]|%63[^|]|%127[^|]|%d",
                   &id, username, salt, hash, email, &active) == 6) {
            if (id == user_id && active) {
                snprintf(out, cap, "%s", username);
                fclose(f);
                return 1;
            }
        }
    }

    fclose(f);
    return 0;
}


static int username_exists(const char *username)
{
    FILE *f = fopen(USERS_DB_PATH, "r");
    if (!f) return 0;

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        int id, active;
        char u[64], salt[64], hash[64], email[128];

        if (sscanf(line, "%d|%63[^|]|%63[^|]|%63[^|]|%127[^|]|%d",
                   &id, u, salt, hash, email, &active) == 6) {
            if (strcmp(u, username) == 0 && active) {
                fclose(f);
                return 1;
            }
        }
    }

    fclose(f);
    return 0;
}

int friends_send_invite(int from_user_id, const char *to_username)
{
    char from_username[64];

    if (!to_username || !to_username[0]) {
        return FRIEND_ERR_INTERNAL;
    }

    // Lấy username người gửi
    if (!get_username_by_id(from_user_id, from_username, sizeof(from_username))) {
        return FRIEND_ERR_INTERNAL;
    }

    // Không cho mời chính mình
    if (strcmp(from_username, to_username) == 0) {
        return FRIEND_ERR_SELF;
    }

    // Kiểm tra user nhận tồn tại
    if (!username_exists(to_username)) {
        return FRIEND_ERR_NOT_FOUND;
    }

    pthread_mutex_lock(&friends_mutex);

    FILE *f = fopen(FRIENDS_DB_PATH, "r");
    if (f) {
        char line[LINE_MAX];
        while (fgets(line, sizeof(line), f)) {
            char from[64], to[64], status[32];
            long ts;

            if (sscanf(line, "%63[^|]|%63[^|]|%31[^|]|%ld",
                       from, to, status, &ts) == 4) {

                // from -> to
                if (strcmp(from, from_username) == 0 &&
                    strcmp(to, to_username) == 0 &&
                    (strcmp(status, "PENDING") == 0 ||
                     strcmp(status, "ACCEPTED") == 0)) {

                    fclose(f);
                    pthread_mutex_unlock(&friends_mutex);
                    return FRIEND_ERR_EXISTS;
                }

                // to -> from (đã là bạn)
                if (strcmp(from, to_username) == 0 &&
                    strcmp(to, from_username) == 0 &&
                    strcmp(status, "ACCEPTED") == 0) {

                    fclose(f);
                    pthread_mutex_unlock(&friends_mutex);
                    return FRIEND_ERR_EXISTS;
                }
            }
        }
        fclose(f);
    }

    // Append lời mời mới
    f = fopen(FRIENDS_DB_PATH, "a");
    if (!f) {
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_INTERNAL;
    }

    fprintf(f, "%s|%s|PENDING|%ld\n",
            from_username, to_username, time(NULL));

    fclose(f);
    pthread_mutex_unlock(&friends_mutex);

    return FRIEND_OK;
}
