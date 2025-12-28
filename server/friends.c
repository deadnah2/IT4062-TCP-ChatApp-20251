#include "friends.h"
#include "sessions.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define USERS_DB_PATH "data/users.db"
#define FRIENDS_DB_PATH "data/friends.db"

#define LINE_MAX 512

static pthread_mutex_t friends_mutex = PTHREAD_MUTEX_INITIALIZER;

static int get_username_by_id(int user_id, char *out, size_t cap)
{
    FILE *f = fopen(USERS_DB_PATH, "r");
    if (!f)
        return 0;

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f))
    {
        int id, active;
        char username[64], salt[64], hash[64], email[128];

        if (sscanf(line, "%d|%63[^|]|%63[^|]|%63[^|]|%127[^|]|%d",
                   &id, username, salt, hash, email, &active) == 6)
        {
            if (id == user_id)
            {
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
    if (!f)
        return 0;

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f))
    {
        int id, active;
        char u[64], salt[64], hash[64], email[128];

        if (sscanf(line, "%d|%63[^|]|%63[^|]|%63[^|]|%127[^|]|%d",
                   &id, u, salt, hash, email, &active) == 6)
        {
            if (strcmp(u, username) == 0 && active)
            {
                fclose(f);
                return 1;
            }
        }
    }

    fclose(f);
    return 0;
}

static int get_user_id_by_username(const char *username)
{
    FILE *f = fopen(USERS_DB_PATH, "r");
    if (!f)
        return -1;

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f))
    {
        int id, active;
        char u[64], salt[64], hash[64], email[128];

        if (sscanf(line, "%d|%63[^|]|%63[^|]|%63[^|]|%127[^|]|%d",
                   &id, u, salt, hash, email, &active) == 6)
        {
            if (strcmp(u, username) == 0)
            {
                fclose(f);
                return id;
            }
        }
    }

    fclose(f);
    return -1;
}

int friends_send_invite(int from_user_id, const char *to_username)
{
    char from_username[64];

    if (!to_username || !to_username[0])
    {
        return FRIEND_ERR_INTERNAL;
    }

    // Lấy username người gửi
    if (!get_username_by_id(from_user_id, from_username, sizeof(from_username)))
    {
        return FRIEND_ERR_INTERNAL;
    }

    // Không cho mời chính mình
    if (strcmp(from_username, to_username) == 0)
    {
        return FRIEND_ERR_SELF;
    }

    // Kiểm tra user nhận tồn tại
    if (!username_exists(to_username))
    {
        return FRIEND_ERR_NOT_FOUND;
    }

    pthread_mutex_lock(&friends_mutex);

    FILE *f = fopen(FRIENDS_DB_PATH, "r");
    if (f)
    {
        char line[LINE_MAX];
        while (fgets(line, sizeof(line), f))
        {
            char from[64], to[64], status[32];
            long ts;

            if (sscanf(line, "%63[^|]|%63[^|]|%31[^|]|%ld",
                       from, to, status, &ts) == 4)
            {

                // from -> to
                if (strcmp(from, from_username) == 0 &&
                    strcmp(to, to_username) == 0 &&
                    (strcmp(status, "PENDING") == 0 ||
                     strcmp(status, "ACCEPTED") == 0))
                {

                    fclose(f);
                    pthread_mutex_unlock(&friends_mutex);
                    return FRIEND_ERR_EXISTS;
                }

                // to -> from (đã là bạn)
                if (strcmp(from, to_username) == 0 &&
                    strcmp(to, from_username) == 0 &&
                    strcmp(status, "ACCEPTED") == 0)
                {

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
    if (!f)
    {
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_INTERNAL;
    }

    fprintf(f, "%s|%s|PENDING|%ld\n",
            from_username, to_username, time(NULL));

    fclose(f);
    pthread_mutex_unlock(&friends_mutex);

    return FRIEND_OK;
}

int friends_accept_invite(int to_user_id, const char *from_username)
{
    char to_username[64];
    if (!from_username || !from_username[0])
        return FRIEND_ERR_INTERNAL;

    if (!get_username_by_id(to_user_id, to_username, sizeof(to_username)))
        return FRIEND_ERR_INTERNAL;

    if (strcmp(to_username, from_username) == 0)
        return FRIEND_ERR_SELF;

    pthread_mutex_lock(&friends_mutex);

    FILE *in = fopen(FRIENDS_DB_PATH, "r");
    if (!in)
    {
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_NOT_FOUND;
    }

    FILE *out = fopen(FRIENDS_DB_PATH ".tmp", "w");
    if (!out)
    {
        fclose(in);
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_INTERNAL;
    }

    char line[LINE_MAX];
    int found = 0;

    while (fgets(line, sizeof(line), in))
    {
        char from[64], to[64], status[32];
        long ts;

        if (sscanf(line, "%63[^|]|%63[^|]|%31[^|]|%ld",
                   from, to, status, &ts) == 4)
        {

            if (strcmp(from, from_username) == 0 &&
                strcmp(to, to_username) == 0)
            {

                if (strcmp(status, "ACCEPTED") == 0)
                {
                    fclose(in);
                    fclose(out);
                    remove(FRIENDS_DB_PATH ".tmp");
                    pthread_mutex_unlock(&friends_mutex);
                    return FRIEND_ERR_EXISTS;
                }

                if (strcmp(status, "PENDING") == 0)
                {
                    fprintf(out, "%s|%s|ACCEPTED|%ld\n",
                            from, to, time(NULL));
                    found = 1;
                    continue;
                }
            }
        }

        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    if (!found)
    {
        remove(FRIENDS_DB_PATH ".tmp");
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_NOT_FOUND;
    }

    rename(FRIENDS_DB_PATH ".tmp", FRIENDS_DB_PATH);
    pthread_mutex_unlock(&friends_mutex);
    return FRIEND_OK;
}

int friends_reject_invite(int to_user_id, const char *from_username)
{
    char to_username[64];
    if (!from_username || !from_username[0])
        return FRIEND_ERR_INTERNAL;

    if (!get_username_by_id(to_user_id, to_username, sizeof(to_username)))
        return FRIEND_ERR_INTERNAL;

    if (strcmp(to_username, from_username) == 0)
        return FRIEND_ERR_SELF;

    pthread_mutex_lock(&friends_mutex);

    FILE *in = fopen(FRIENDS_DB_PATH, "r");
    if (!in)
    {
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_NOT_FOUND;
    }

    FILE *out = fopen(FRIENDS_DB_PATH ".tmp", "w");
    if (!out)
    {
        fclose(in);
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_INTERNAL;
    }

    char line[LINE_MAX];
    int removed = 0;

    while (fgets(line, sizeof(line), in))
    {
        char from[64], to[64], status[32];
        long ts;

        if (sscanf(line, "%63[^|]|%63[^|]|%31[^|]|%ld",
                   from, to, status, &ts) == 4)
        {

            if (strcmp(from, from_username) == 0 &&
                strcmp(to, to_username) == 0 &&
                strcmp(status, "PENDING") == 0)
            {
                removed = 1;
                continue;
            }
        }

        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    if (!removed)
    {
        remove(FRIENDS_DB_PATH ".tmp");
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_NOT_FOUND;
    }

    rename(FRIENDS_DB_PATH ".tmp", FRIENDS_DB_PATH);
    pthread_mutex_unlock(&friends_mutex);
    return FRIEND_OK;
}

int friends_pending(int user_id, char *out, size_t cap)
{
    char my_username[64];
    out[0] = 0;

    if (!get_username_by_id(user_id, my_username, sizeof(my_username)))
    {
        return FRIEND_ERR_INTERNAL;
    }

    pthread_mutex_lock(&friends_mutex);

    FILE *f = fopen(FRIENDS_DB_PATH, "r");
    if (!f)
    {
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_OK; // chưa có file => không có pending
    }

    char line[LINE_MAX];
    size_t used = 0;

    while (fgets(line, sizeof(line), f))
    {
        char from[64], to[64], status[32];
        long ts;

        if (sscanf(line, "%63[^|]|%63[^|]|%31[^|]|%ld",
                   from, to, status, &ts) != 4)
            continue;

        if (strcmp(to, my_username) == 0 &&
            strcmp(status, "PENDING") == 0)
        {

            size_t len = strlen(from);
            if (used + len + 2 >= cap)
                break;

            if (used > 0)
            {
                out[used++] = ',';
            }

            memcpy(out + used, from, len);
            used += len;
            out[used] = 0;
        }
    }

    fclose(f);
    pthread_mutex_unlock(&friends_mutex);
    return FRIEND_OK;
}

int friends_list(int user_id, char *out, size_t cap)
{
    char my_username[64];
    out[0] = 0;

    if (!get_username_by_id(user_id, my_username, sizeof(my_username)))
    {
        return FRIEND_ERR_INTERNAL;
    }

    pthread_mutex_lock(&friends_mutex);

    FILE *f = fopen(FRIENDS_DB_PATH, "r");
    if (!f)
    {
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_OK; // chưa có file => không có pending
    }

    char line[LINE_MAX];
    size_t used = 0;

    while (fgets(line, sizeof(line), f))
    {
        char from[64], to[64], status[32], friend_name[64];
        long ts;

        if (sscanf(line, "%63[^|]|%63[^|]|%31[^|]|%ld",
                   from, to, status, &ts) != 4)
            continue;

        if ((strcmp(to, my_username) == 0 || strcmp(from, my_username) == 0) &&
            strcmp(status, "ACCEPTED") == 0)
        {

            if (strcmp(to, my_username) == 0)
            {
                strcpy(friend_name, from);
            }
            else
            {
                strcpy(friend_name, to);
            }

            // Lấy user_id của friend và check online status
            int friend_id = get_user_id_by_username(friend_name);
            const char *online_status = "offline";
            if (friend_id > 0 && sessions_is_online(friend_id))
            {
                online_status = "online";
            }

            // Format: username:online hoặc username:offline
            char entry[128];
            snprintf(entry, sizeof(entry), "%s:%s", friend_name, online_status);

            size_t len = strlen(entry);
            if (used + len + 2 >= cap)
                break;

            if (used > 0)
            {
                out[used++] = ',';
            }

            memcpy(out + used, entry, len);
            used += len;
            out[used] = 0;
        }
    }

    fclose(f);
    pthread_mutex_unlock(&friends_mutex);
    return FRIEND_OK;
}

int friends_delete(int user_id, const char *other_username)
{
    char my_username[64];

    if (!other_username || !other_username[0])
        return FRIEND_ERR_INTERNAL;

    if (!get_username_by_id(user_id, my_username, sizeof(my_username)))
        return FRIEND_ERR_INTERNAL;

    if (strcmp(my_username, other_username) == 0)
        return FRIEND_ERR_SELF;

    pthread_mutex_lock(&friends_mutex);

    FILE *in = fopen(FRIENDS_DB_PATH, "r");
    if (!in) {
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_NOT_FOUND;
    }

    FILE *out = fopen(FRIENDS_DB_PATH ".tmp", "w");
    if (!out) {
        fclose(in);
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_INTERNAL;
    }

    char line[LINE_MAX];
    int removed = 0;

    while (fgets(line, sizeof(line), in)) {
        char from[64], to[64], status[32];
        long ts;

        if (sscanf(line, "%63[^|]|%63[^|]|%31[^|]|%ld",
                   from, to, status, &ts) == 4) {

            if (strcmp(status, "ACCEPTED") == 0) {

                // match either direction
                if ((strcmp(from, my_username) == 0 &&
                     strcmp(to, other_username) == 0) ||
                    (strcmp(from, other_username) == 0 &&
                     strcmp(to, my_username) == 0)) {

                    removed = 1;
                    continue; // skip writing this line
                }
            }
        }

        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    if (!removed) {
        remove(FRIENDS_DB_PATH ".tmp");
        pthread_mutex_unlock(&friends_mutex);
        return FRIEND_ERR_NOT_FOUND;
    }

    rename(FRIENDS_DB_PATH ".tmp", FRIENDS_DB_PATH);
    pthread_mutex_unlock(&friends_mutex);
    return FRIEND_OK;
}
