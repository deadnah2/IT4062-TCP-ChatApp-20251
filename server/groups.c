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

int groups_create(int owner_user_id, const char *group_name)
{
    char owner[64];
    if (!group_name || !group_name[0])
        return GROUP_ERR_INTERNAL;

    if (!get_username_by_id(owner_user_id, owner, sizeof(owner)))
        return GROUP_ERR_INTERNAL;

    pthread_mutex_lock(&groups_mutex);

    int gid = (int)time(NULL);

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
