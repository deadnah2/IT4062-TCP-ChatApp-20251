#include "groups.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define USERS_DB_PATH "data/users.db"
#define GROUPS_DB_PATH "data/groups.db"
#define GROUP_MEMBERS_DB_PATH "data/group_members.db"

#define LINE_MAX 512

static pthread_mutex_t groups_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ===== Helpers ===== */

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
            if (id == user_id && active)
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

static int is_group_owner(int group_id, const char *username)
{
    FILE *f = fopen(GROUPS_DB_PATH, "r");
    if (!f)
        return 0;

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f))
    {
        int gid;
        char name[64], owner[64];
        long ts;

        if (sscanf(line, "%d|%63[^|]|%63[^|]|%ld",
                   &gid, name, owner, &ts) == 4)
        {
            if (gid == group_id && strcmp(owner, username) == 0)
            {
                fclose(f);
                return 1;
            }
        }
    }

    fclose(f);
    return 0;
}

static int is_group_member(int group_id, const char *username)
{
    FILE *f = fopen(GROUP_MEMBERS_DB_PATH, "r");
    if (!f)
        return 0;

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f))
    {
        int gid;
        char u[64];

        if (sscanf(line, "%d|%63s", &gid, u) == 2)
        {
            if (gid == group_id && strcmp(u, username) == 0)
            {
                fclose(f);
                return 1;
            }
        }
    }

    fclose(f);
    return 0;
}

/* ===== Public APIs ===== */

int groups_create(int owner_user_id,
                  const char *group_name,
                  int *out_group_id)
{
    char owner[64];
    if (!group_name || !group_name[0])
        return GROUP_ERR_INTERNAL;

    if (!get_username_by_id(owner_user_id, owner, sizeof(owner)))
        return GROUP_ERR_INTERNAL;

    pthread_mutex_lock(&groups_mutex);

    int gid = (int)time(NULL); // simple unique id

    FILE *g = fopen(GROUPS_DB_PATH, "a");
    if (!g)
    {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_INTERNAL;
    }

    fprintf(g, "%d|%s|%s|%ld\n",
            gid, group_name, owner, time(NULL));
    fclose(g);

    FILE *m = fopen(GROUP_MEMBERS_DB_PATH, "a");
    if (m)
    {
        fprintf(m, "%d|%s\n", gid, owner);
        fclose(m);
    }

    pthread_mutex_unlock(&groups_mutex);

    if (out_group_id)
        *out_group_id = gid;

    return GROUP_OK;
}

int groups_list(int user_id, char *out, size_t cap)
{
    char username[64];
    if (!get_username_by_id(user_id, username, sizeof(username)))
        return GROUP_ERR_INTERNAL;

    FILE *m = fopen(GROUP_MEMBERS_DB_PATH, "r");
    if (!m)
    {
        out[0] = 0;
        return GROUP_OK;
    }

    out[0] = 0;
    char line[LINE_MAX];

    while (fgets(line, sizeof(line), m))
    {
        int gid;
        char u[64];

        if (sscanf(line, "%d|%63s", &gid, u) == 2)
        {
            if (strcmp(u, username) == 0)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d,", gid);
                if (strlen(out) + strlen(buf) < cap)
                    strcat(out, buf);
            }
        }
    }

    fclose(m);
    return GROUP_OK;
}

int groups_list_members(int user_id, int group_id, char *out, size_t cap)
{
    char username[64];
    if (!get_username_by_id(user_id, username, sizeof(username)))
        return GROUP_ERR_INTERNAL;

    pthread_mutex_lock(&groups_mutex);

    /* Permission: must be a member */
    if (!is_group_member(group_id, username))
    {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_PERMISSION;
    }

    FILE *m = fopen(GROUP_MEMBERS_DB_PATH, "r");
    if (!m)
    {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_INTERNAL;
    }

    out[0] = 0;
    char line[LINE_MAX];

    while (fgets(line, sizeof(line), m))
    {
        int gid;
        char u[64];

        if (sscanf(line, "%d|%63s", &gid, u) == 2)
        {
            if (gid == group_id)
            {
                if (strlen(out) + strlen(u) + 2 < cap)
                {
                    strcat(out, u);
                    strcat(out, ",");
                }
            }
        }
    }

    fclose(m);
    pthread_mutex_unlock(&groups_mutex);

    /* Remove trailing comma */
    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == ',')
        out[len - 1] = '\0';

    return GROUP_OK;
}

int groups_add_member(int owner_user_id, int group_id, const char *username)
{
    char owner[64];

    if (!username || !username[0])
        return GROUP_ERR_INTERNAL;

    if (!get_username_by_id(owner_user_id, owner, sizeof(owner)))
        return GROUP_ERR_INTERNAL;

    if (!username_exists(username))
        return GROUP_ERR_NOT_FOUND;

    pthread_mutex_lock(&groups_mutex);

    /* Only owner can add members */
    if (!is_group_owner(group_id, owner))
    {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_PERMISSION;
    }

    if (is_group_member(group_id, username))
    {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_EXISTS;
    }

    FILE *m = fopen(GROUP_MEMBERS_DB_PATH, "a");
    if (!m)
    {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_INTERNAL;
    }

    fprintf(m, "%d|%s\n", group_id, username);
    fclose(m);

    pthread_mutex_unlock(&groups_mutex);
    return GROUP_OK;
}

int groups_remove_member(int owner_user_id,
                         int group_id,
                         const char *username)
{
    char owner[64];

    if (!get_username_by_id(owner_user_id, owner, sizeof(owner)))
        return GROUP_ERR_INTERNAL;

    pthread_mutex_lock(&groups_mutex);

    if (!is_group_owner(group_id, owner)) {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_PERMISSION;
    }

    if (!is_group_member(group_id, username)) {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_NOT_FOUND;
    }

    FILE *in = fopen(GROUP_MEMBERS_DB_PATH, "r");
    FILE *out = fopen(GROUP_MEMBERS_DB_PATH ".tmp", "w");

    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_INTERNAL;
    }

    char line[LINE_MAX];
    int removed = 0;

    while (fgets(line, sizeof(line), in)) {
        int gid;
        char u[64];

        if (sscanf(line, "%d|%63s", &gid, u) == 2) {
            if (gid == group_id && strcmp(u, username) == 0) {
                removed = 1;
                continue;
            }
        }
        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    if (!removed) {
        remove(GROUP_MEMBERS_DB_PATH ".tmp");
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_NOT_FOUND;
    }

    rename(GROUP_MEMBERS_DB_PATH ".tmp", GROUP_MEMBERS_DB_PATH);
    pthread_mutex_unlock(&groups_mutex);
    return GROUP_OK;
}

int groups_leave(int user_id, int group_id)
{
    char username[64];

    if (!get_username_by_id(user_id, username, sizeof(username)))
        return GROUP_ERR_INTERNAL;

    pthread_mutex_lock(&groups_mutex);

    if (is_group_owner(group_id, username)) {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_SELF; // owner cannot leave
    }

    if (!is_group_member(group_id, username)) {
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_NOT_FOUND;
    }

    FILE *in = fopen(GROUP_MEMBERS_DB_PATH, "r");
    FILE *out = fopen(GROUP_MEMBERS_DB_PATH ".tmp", "w");

    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_INTERNAL;
    }

    char line[LINE_MAX];
    int removed = 0;

    while (fgets(line, sizeof(line), in)) {
        int gid;
        char u[64];

        if (sscanf(line, "%d|%63s", &gid, u) == 2) {
            if (gid == group_id && strcmp(u, username) == 0) {
                removed = 1;
                continue;
            }
        }
        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    if (!removed) {
        remove(GROUP_MEMBERS_DB_PATH ".tmp");
        pthread_mutex_unlock(&groups_mutex);
        return GROUP_ERR_NOT_FOUND;
    }

    rename(GROUP_MEMBERS_DB_PATH ".tmp", GROUP_MEMBERS_DB_PATH);
    pthread_mutex_unlock(&groups_mutex);
    return GROUP_OK;
}
