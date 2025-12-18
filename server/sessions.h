#ifndef SESSIONS_H
#define SESSIONS_H

#include <time.h>

#define SESS_TOKEN_LEN 32

#define SESS_OK 0
#define SESS_ERR_FULL -1
#define SESS_ERR_NOT_FOUND -2
#define SESS_ERR_EXPIRED -3
#define SESS_ERR_ALREADY -4

void sessions_init(int timeout_seconds);

// Create a new session for user_id on socket; returns token in out_token.
int sessions_create(int user_id, int client_socket, char out_token[SESS_TOKEN_LEN + 1]);

// Validate token, update last_activity.
int sessions_validate(const char* token, int* out_user_id);

int sessions_destroy(const char* token);

// Remove session bound to this socket (called when client disconnects)
void sessions_remove_by_socket(int client_socket);

// Check if user already logged in elsewhere
int sessions_is_user_logged_in(int user_id, int exclude_socket);

#endif
