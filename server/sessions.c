#include "sessions.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_SESSIONS 1000

typedef struct {
    int active;
    char token[SESS_TOKEN_LEN + 1];
    int user_id;
    int client_socket;
    time_t created_at;
    time_t last_activity;
} Session;

static pthread_mutex_t g_sess_mutex = PTHREAD_MUTEX_INITIALIZER;
static Session g_sessions[MAX_SESSIONS];
static int g_timeout = 3600;

static void seed_once(void)
{
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned int)(time(NULL) ^ getpid()));
    }
}

static void generate_token(char out[SESS_TOKEN_LEN + 1])
{
    seed_once();
    static const char* cs = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int n = (int)strlen(cs);
    for (int i = 0; i < SESS_TOKEN_LEN; i++) {
        out[i] = cs[rand() % n];
    }
    out[SESS_TOKEN_LEN] = 0;
}

static void cleanup_expired_unlocked(void)
{
    time_t now = time(NULL);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active) {
            if ((int)(now - g_sessions[i].last_activity) >= g_timeout) {
                g_sessions[i].active = 0;
            }
        }
    }
}

void sessions_init(int timeout_seconds)
{
    pthread_mutex_lock(&g_sess_mutex);
    memset(g_sessions, 0, sizeof(g_sessions));
    g_timeout = timeout_seconds > 0 ? timeout_seconds : 3600;
    pthread_mutex_unlock(&g_sess_mutex);
}

int sessions_is_user_logged_in(int user_id, int exclude_socket)
{
    pthread_mutex_lock(&g_sess_mutex);
    cleanup_expired_unlocked();

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active && g_sessions[i].user_id == user_id && g_sessions[i].client_socket != exclude_socket) {
            pthread_mutex_unlock(&g_sess_mutex);
            return 1;
        }
    }

    pthread_mutex_unlock(&g_sess_mutex);
    return 0;
}

int sessions_create(int user_id, int client_socket, char out_token[SESS_TOKEN_LEN + 1])
{
    pthread_mutex_lock(&g_sess_mutex);
    cleanup_expired_unlocked();

    // Avoid multiple active tokens per connection
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active && g_sessions[i].client_socket == client_socket) {
            g_sessions[i].active = 0;
        }
    }

    // Prevent multi-login from different sockets
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active && g_sessions[i].user_id == user_id && g_sessions[i].client_socket != client_socket) {
            pthread_mutex_unlock(&g_sess_mutex);
            return SESS_ERR_ALREADY;
        }
    }

    int slot = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!g_sessions[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&g_sess_mutex);
        return SESS_ERR_FULL;
    }

    Session* s = &g_sessions[slot];
    memset(s, 0, sizeof(*s));
    s->active = 1;
    s->user_id = user_id;
    s->client_socket = client_socket;
    s->created_at = time(NULL);
    s->last_activity = s->created_at;

    // Ensure uniqueness best-effort
    for (int attempt = 0; attempt < 10; attempt++) {
        generate_token(s->token);
        int dup = 0;
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (i != slot && g_sessions[i].active && strcmp(g_sessions[i].token, s->token) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup) break;
    }

    strcpy(out_token, s->token);

    pthread_mutex_unlock(&g_sess_mutex);
    return SESS_OK;
}

int sessions_validate(const char* token, int* out_user_id)
{
    if (out_user_id) *out_user_id = 0;
    if (!token || !token[0]) return SESS_ERR_NOT_FOUND;

    pthread_mutex_lock(&g_sess_mutex);
    cleanup_expired_unlocked();

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active && strcmp(g_sessions[i].token, token) == 0) {
            time_t now = time(NULL);
            if ((int)(now - g_sessions[i].last_activity) >= g_timeout) {
                g_sessions[i].active = 0;
                pthread_mutex_unlock(&g_sess_mutex);
                return SESS_ERR_EXPIRED;
            }
            g_sessions[i].last_activity = now;
            if (out_user_id) *out_user_id = g_sessions[i].user_id;
            pthread_mutex_unlock(&g_sess_mutex);
            return SESS_OK;
        }
    }

    pthread_mutex_unlock(&g_sess_mutex);
    return SESS_ERR_NOT_FOUND;
}

int sessions_destroy(const char* token)
{
    if (!token || !token[0]) return SESS_ERR_NOT_FOUND;

    pthread_mutex_lock(&g_sess_mutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active && strcmp(g_sessions[i].token, token) == 0) {
            g_sessions[i].active = 0;
            pthread_mutex_unlock(&g_sess_mutex);
            return SESS_OK;
        }
    }
    pthread_mutex_unlock(&g_sess_mutex);
    return SESS_ERR_NOT_FOUND;
}

void sessions_remove_by_socket(int client_socket)
{
    pthread_mutex_lock(&g_sess_mutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active && g_sessions[i].client_socket == client_socket) {
            g_sessions[i].active = 0;
        }
    }
    pthread_mutex_unlock(&g_sess_mutex);
}

