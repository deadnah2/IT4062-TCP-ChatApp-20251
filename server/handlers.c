#include "handlers.h"

#include <string.h>
#include <stdio.h>

#include "../common/protocol.h"
#include "accounts.h"
#include "sessions.h"

static int kv_get(const char* payload, const char* key, char* out, size_t out_cap)
{
    if (!payload || !key || !out || out_cap == 0) return 0;
    out[0] = 0;

    size_t key_len = strlen(key);
    const char* p = payload;

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char* token_end = strchr(p, ' ');
        size_t token_len = token_end ? (size_t)(token_end - p) : strlen(p);

        // token looks like k=v
        const char* eq = memchr(p, '=', token_len);
        if (eq) {
            size_t klen = (size_t)(eq - p);
            size_t vlen = token_len - klen - 1;
            if (klen == key_len && strncmp(p, key, key_len) == 0) {
                if (vlen + 1 > out_cap) return 0;
                memcpy(out, eq + 1, vlen);
                out[vlen] = 0;
                return 1;
            }
        }

        p = token_end ? token_end + 1 : p + token_len;
    }

    return 0;
}

static void send_simple_err(int sock, const char* rid, int code, const char* msg)
{
    proto_send_err(sock, rid && rid[0] ? rid : "0", code, msg);
}

int handle_request(ServerCtx* ctx, const char* line)
{
    ProtoMsg msg;
    if (proto_parse_line(line, &msg) != 0) {
        send_simple_err(ctx->client_sock, "0", 400, "bad_request");
        return -1;
    }

    // PING
    if (strcmp(msg.verb, "PING") == 0) {
        proto_send_ok(ctx->client_sock, msg.req_id, "pong=1");
        proto_free(&msg);
        return 0;
    }

    // REGISTER
    if (strcmp(msg.verb, "REGISTER") == 0) {
        char username[64], password[128], email[128];
        if (!kv_get(msg.payload, "username", username, sizeof(username)) ||
            !kv_get(msg.payload, "password", password, sizeof(password)) ||
            !kv_get(msg.payload, "email", email, sizeof(email))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id = 0;
        int rc = accounts_register(username, password, email, &user_id);
        if (rc == ACC_OK) {
            char payload[64];
            snprintf(payload, sizeof(payload), "user_id=%d", user_id);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        } else if (rc == ACC_ERR_EXISTS) {
            send_simple_err(ctx->client_sock, msg.req_id, 409, "username_exists");
        } else if (rc == ACC_ERR_INVALID) {
            send_simple_err(ctx->client_sock, msg.req_id, 422, "invalid_fields");
        } else {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // LOGIN
    if (strcmp(msg.verb, "LOGIN") == 0) {
        char username[64], password[128];
        if (!kv_get(msg.payload, "username", username, sizeof(username)) ||
            !kv_get(msg.payload, "password", password, sizeof(password))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id = 0;
        int rc = accounts_authenticate(username, password, &user_id);
        if (rc == ACC_OK) {
            char token[SESS_TOKEN_LEN + 1];
            int sr = sessions_create(user_id, ctx->client_sock, token);
            if (sr == SESS_OK) {
                char payload[128];
                snprintf(payload, sizeof(payload), "token=%s user_id=%d", token, user_id);
                proto_send_ok(ctx->client_sock, msg.req_id, payload);
            } else if (sr == SESS_ERR_ALREADY) {
                send_simple_err(ctx->client_sock, msg.req_id, 409, "already_logged_in");
            } else {
                send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
            }
        } else {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_credentials");
        }

        proto_free(&msg);
        return 0;
    }

    // LOGOUT
    if (strcmp(msg.verb, "LOGOUT") == 0) {
        char token[128];
        if (!kv_get(msg.payload, "token", token, sizeof(token))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int rc = sessions_destroy(token);
        if (rc == SESS_OK) {
            proto_send_ok(ctx->client_sock, msg.req_id, "ok=1");
        } else {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
        }

        proto_free(&msg);
        return 0;
    }

    // WHOAMI
    if (strcmp(msg.verb, "WHOAMI") == 0) {
        char token[128];
        if (!kv_get(msg.payload, "token", token, sizeof(token))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id = 0;
        int rc = sessions_validate(token, &user_id);
        if (rc == SESS_OK) {
            char payload[64];
            snprintf(payload, sizeof(payload), "user_id=%d", user_id);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        } else {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
        }

        proto_free(&msg);
        return 0;
    }

    send_simple_err(ctx->client_sock, msg.req_id, 404, "unknown_command");
    proto_free(&msg);
    return 0;
}
