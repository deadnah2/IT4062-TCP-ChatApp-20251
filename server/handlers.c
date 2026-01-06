#include "handlers.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

#include "../common/protocol.h"
#include "accounts.h"
#include "sessions.h"
#include "friends.h"
#include "messages.h"
#include "groups.h"

/*
 * server/handlers.c
 * - Nhận 1 line request, parse verb/req_id/payload và route sang handler tương ứng.
 * - Payload hiện dùng dạng "k=v k=v ..." (không hỗ trợ value có dấu cách).
 * - Các verb cần xác thực nên yêu cầu token=... và gọi sessions_validate().
 */

/*
 * kv_get
 * - Parse payload dạng "k=v k=v ..." và lấy value theo `key`.
 * - Token được tách bằng dấu cách; không hỗ trợ quoted value.
 * Return: 1 nếu tìm thấy và copy vào out, 0 nếu không thấy/không hợp lệ.
 */
static int kv_get(const char *payload, const char *key, char *out, size_t out_cap)
{
    if (!payload || !key || !out || out_cap == 0)
        return 0;
    out[0] = 0;

    size_t key_len = strlen(key);
    const char *p = payload;

    while (*p)
    {
        while (*p == ' ')
            p++;
        if (!*p)
            break;

        const char *token_end = strchr(p, ' ');
        size_t token_len = token_end ? (size_t)(token_end - p) : strlen(p);

        // token looks like k=v
        const char *eq = memchr(p, '=', token_len);
        if (eq)
        {
            size_t klen = (size_t)(eq - p);
            size_t vlen = token_len - klen - 1;
            if (klen == key_len && strncmp(p, key, key_len) == 0)
            {
                if (vlen + 1 > out_cap)
                    return 0;
                memcpy(out, eq + 1, vlen);
                out[vlen] = 0;
                return 1;
            }
        }

        p = token_end ? token_end + 1 : p + token_len;
    }

    return 0;
}

static void send_simple_err(int sock, const char *rid, int code, const char *msg)
{
    proto_send_err(sock, rid && rid[0] ? rid : "0", code, msg);
}

/*
 * handle_request
 * - Entry point xử lý 1 request của client trong server thread.
 * - Luôn cố gắng trả response (OK/ERR) theo đúng req_id để client match được.
 * - Khi thêm verb mới: parse payload bằng kv_get(), kiểm tra auth bằng sessions_validate().
 *
 * Return:
 * - 0  : xử lý xong (kể cả có lỗi nghiệp vụ)
 * - -1 : lỗi parse request (bad_request)
 */
int handle_request(ServerCtx *ctx, const char *line)
{
    ProtoMsg msg;
    if (proto_parse_line(line, &msg) != 0)
    {
        send_simple_err(ctx->client_sock, "0", 400, "bad_request");
        return -1;
    }

    // Lưu ý: handle_request nên luôn trả response theo req_id để client match được.
    // PING
    if (strcmp(msg.verb, "PING") == 0)
    {
        proto_send_ok(ctx->client_sock, msg.req_id, "pong=1");
        proto_free(&msg);
        return 0;
    }

    // REGISTER
    if (strcmp(msg.verb, "REGISTER") == 0)
    {
        char username[64], password[128], email[128];
        if (!kv_get(msg.payload, "username", username, sizeof(username)) ||
            !kv_get(msg.payload, "password", password, sizeof(password)) ||
            !kv_get(msg.payload, "email", email, sizeof(email)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id = 0;
        int rc = accounts_register(username, password, email, &user_id);
        if (rc == ACC_OK)
        {
            char payload[64];
            snprintf(payload, sizeof(payload), "user_id=%d", user_id);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (rc == ACC_ERR_EXISTS)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 409, "username_exists");
        }
        else if (rc == ACC_ERR_INVALID)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 422, "invalid_fields");
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // LOGIN
    if (strcmp(msg.verb, "LOGIN") == 0)
    {
        char username[64], password[128];
        if (!kv_get(msg.payload, "username", username, sizeof(username)) ||
            !kv_get(msg.payload, "password", password, sizeof(password)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id = 0;
        int rc = accounts_authenticate(username, password, &user_id);
        if (rc == ACC_OK)
        {
            char token[SESS_TOKEN_LEN + 1];
            int sr = sessions_create(user_id, ctx->client_sock, token);
            if (sr == SESS_OK)
            {
                char payload[128];
                snprintf(payload, sizeof(payload), "token=%s user_id=%d", token, user_id);
                proto_send_ok(ctx->client_sock, msg.req_id, payload);
            }
            else if (sr == SESS_ERR_ALREADY)
            {
                send_simple_err(ctx->client_sock, msg.req_id, 409, "already_logged_in");
            }
            else
            {
                send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
            }
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_credentials");
        }

        proto_free(&msg);
        return 0;
    }

    // LOGOUT
    if (strcmp(msg.verb, "LOGOUT") == 0)
    {
        char token[128];
        if (!kv_get(msg.payload, "token", token, sizeof(token)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int rc = sessions_destroy(token);
        if (rc == SESS_OK)
        {
            proto_send_ok(ctx->client_sock, msg.req_id, "ok=1");
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
        }

        proto_free(&msg);
        return 0;
    }

    // WHOAMI
    if (strcmp(msg.verb, "WHOAMI") == 0)
    {
        char token[128];
        if (!kv_get(msg.payload, "token", token, sizeof(token)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id = 0;
        int rc = sessions_validate(token, &user_id);
        if (rc == SESS_OK)
        {
            char payload[64];
            snprintf(payload, sizeof(payload), "user_id=%d", user_id);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
        }

        proto_free(&msg);
        return 0;
    }

    // FRIEND_INVITE
    if (strcmp(msg.verb, "FRIEND_INVITE") == 0)
    {
        char token[128], to[64];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "username", to, sizeof(to)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int from_user_id = 0;
        int rc = sessions_validate(token, &from_user_id);
        if (rc != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. Send unvitation
        int fr = friends_send_invite(from_user_id, to);

        if (fr == FRIEND_OK)
        {
            char payload[128];
            snprintf(payload, sizeof(payload), "username=%s status=pending", to);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (fr == FRIEND_ERR_SELF)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 422, "cannot_invite_self");
        }
        else if (fr == FRIEND_ERR_NOT_FOUND)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "user_not_found");
        }
        else if (fr == FRIEND_ERR_EXISTS)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 409, "already_friend_or_pending");
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // FRIEND_ACCEPT
    if (strcmp(msg.verb, "FRIEND_ACCEPT") == 0)
    {
        char token[128], from[64];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "username", from, sizeof(from)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int to_user_id = 0;
        int rc = sessions_validate(token, &to_user_id);
        if (rc != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. Accept invite
        int fr = friends_accept_invite(to_user_id, from);

        if (fr == FRIEND_OK)
        {
            char payload[128];
            snprintf(payload, sizeof(payload), "username=%s status=accepted", from);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (fr == FRIEND_ERR_SELF)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 422, "cannot_accept_self");
        }
        else if (fr == FRIEND_ERR_NOT_FOUND)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "invite_not_found");
        }
        else if (fr == FRIEND_ERR_EXISTS)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 409, "already_friends");
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // FRIEND_REJECT
    if (strcmp(msg.verb, "FRIEND_REJECT") == 0)
    {
        char token[128], from[64];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "username", from, sizeof(from)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int to_user_id = 0;
        int rc = sessions_validate(token, &to_user_id);
        if (rc != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. Reject invite
        int fr = friends_reject_invite(to_user_id, from);

        if (fr == FRIEND_OK)
        {
            char payload[128];
            snprintf(payload, sizeof(payload), "username=%s status=rejected", from);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (fr == FRIEND_ERR_SELF)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 422, "cannot_reject_self");
        }
        else if (fr == FRIEND_ERR_NOT_FOUND)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "invite_not_found");
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // FRIEND_PENDING
    if (strcmp(msg.verb, "FRIEND_PENDING") == 0)
    {
        char token[128];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int user_id = 0;
        int rc = sessions_validate(token, &user_id);
        if (rc != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. Get pending friends
        char list[512];
        int fr = friends_pending(user_id, list, sizeof(list));
        if (fr != FRIEND_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
            proto_free(&msg);
            return 0;
        }

        // 4. Return OK (even if empty)
        char payload[600];
        snprintf(payload, sizeof(payload), "username=%s", list);
        proto_send_ok(ctx->client_sock, msg.req_id, payload);

        proto_free(&msg);
        return 0;
    }

    // FRIEND_LIST
    if (strcmp(msg.verb, "FRIEND_LIST") == 0)
    {
        char token[128];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int user_id = 0;
        int rc = sessions_validate(token, &user_id);
        if (rc != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. Get friends list
        char list[512];
        int fr = friends_list(user_id, list, sizeof(list));
        if (fr != FRIEND_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
            proto_free(&msg);
            return 0;
        }

        // 4. Return OK (even if empty)
        char payload[600];
        snprintf(payload, sizeof(payload), "username=%s", list);
        proto_send_ok(ctx->client_sock, msg.req_id, payload);

        proto_free(&msg);
        return 0;
    }

    // FRIEND_DELETE
    if (strcmp(msg.verb, "FRIEND_DELETE") == 0)
    {
        char token[128], friend[64];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "username", friend, sizeof(friend)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int user_id = 0;
        int rc = sessions_validate(token, &user_id);
        if (rc != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. Delete friend
        int fr = friends_delete(user_id, friend);

        if (fr == FRIEND_OK)
        {
            char payload[128];
            snprintf(payload, sizeof(payload), "username=%s status=deleted", friend);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (fr == FRIEND_ERR_SELF)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 422, "cannot_delete_self");
        }
        else if (fr == FRIEND_ERR_NOT_FOUND)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "friend_not_found");
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // GROUP CREATE
    if (strcmp(msg.verb, "GROUP_CREATE") == 0) {
        char token[128], name[64];

        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "name", name, sizeof(name))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id;
        if (sessions_validate(token, &user_id) != SESS_OK) {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        int group_id = 0;
        int rc = groups_create(user_id, name, &group_id);

        if (rc == GROUP_OK) {
            char payload[128];
            snprintf(payload, sizeof(payload),
                    "group_id=%d name=%s",
                    group_id, name);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }


    // GROUP_LIST
    if (strcmp(msg.verb, "GROUP_LIST") == 0)
    {
        char token[128];
        char groups[1024];
        char payload[1100];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int user_id = 0;
        if (sessions_validate(token, &user_id) != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. Query groups
        int gr = groups_list(user_id, groups, sizeof(groups));
        if (gr != GROUP_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
            proto_free(&msg);
            return 0;
        }

        // 4. Build payload
        snprintf(payload, sizeof(payload), "groups=%s", groups);

        proto_send_ok(ctx->client_sock, msg.req_id, payload);
        proto_free(&msg);
        return 0;
    }
    // GROUP MEMBERS
    if (strcmp(msg.verb, "GROUP_MEMBERS") == 0)
    {
        char token[128];
        char group_id_str[32];
        char out[2048];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "group_id", group_id_str, sizeof(group_id_str)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int group_id = atoi(group_id_str);
        if (group_id <= 0)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "invalid_group_id");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int user_id = 0;
        if (sessions_validate(token, &user_id) != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. List members
        out[0] = '\0';
        int rc = groups_list_members(user_id, group_id, out, sizeof(out));

        if (rc == GROUP_OK)
        {
            char payload[2048];
            snprintf(payload, sizeof(payload),
                     "members=%s", out[0] ? out : "");
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (rc == GROUP_ERR_PERMISSION)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 403, "not_group_member");
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // GROUP ADD MEMBER
    if (strcmp(msg.verb, "GROUP_ADD") == 0)
    {
        char token[128];
        char group_id_str[32];
        char username[64];

        // 1. Parse payload
        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "group_id", group_id_str, sizeof(group_id_str)) ||
            !kv_get(msg.payload, "username", username, sizeof(username)))
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int group_id = atoi(group_id_str);
        if (group_id <= 0)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "invalid_group_id");
            proto_free(&msg);
            return 0;
        }

        // 2. Validate session
        int user_id = 0;
        if (sessions_validate(token, &user_id) != SESS_OK)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // 3. Add member
        int rc = groups_add_member(user_id, group_id, username);

        if (rc == GROUP_OK)
        {
            char payload[128];
            snprintf(payload, sizeof(payload),
                     "group_id=%d username=%s status=added",
                     group_id, username);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (rc == GROUP_ERR_NOT_FOUND)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "user_not_found");
        }
        else if (rc == GROUP_ERR_PERMISSION)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 403, "not_group_owner");
        }
        else if (rc == GROUP_ERR_EXISTS)
        {
            send_simple_err(ctx->client_sock, msg.req_id, 409, "already_member");
        }
        else
        {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // GROUP REMOVE MEMBER (OWNER)
    if (strcmp(msg.verb, "GROUP_REMOVE") == 0) {
        char token[128], username[64], gid_str[32];

        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "group_id", gid_str, sizeof(gid_str)) ||
            !kv_get(msg.payload, "username", username, sizeof(username))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id;
        if (sessions_validate(token, &user_id) != SESS_OK) {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        int gid = atoi(gid_str);
        int rc = groups_remove_member(user_id, gid, username);

        if (rc == GROUP_OK) {
            char payload[128];
            snprintf(payload, sizeof(payload),
                    "group_id=%d username=%s status=removed",
                    gid, username);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (rc == GROUP_ERR_PERMISSION) {
            send_simple_err(ctx->client_sock, msg.req_id, 403, "not_group_owner");
        }
        else if (rc == GROUP_ERR_NOT_FOUND) {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "member_not_found");
        }
        else {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // GROUP LEAVE (MEMBER)
    if (strcmp(msg.verb, "GROUP_LEAVE") == 0) {
        char token[128], gid_str[32];

        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "group_id", gid_str, sizeof(gid_str))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id;
        if (sessions_validate(token, &user_id) != SESS_OK) {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        int gid = atoi(gid_str);
        int rc = groups_leave(user_id, gid);

        if (rc == GROUP_OK) {
            char payload[64];
            snprintf(payload, sizeof(payload),
                    "group_id=%d status=left", gid);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (rc == GROUP_ERR_SELF) {
            send_simple_err(ctx->client_sock, msg.req_id, 422, "owner_cannot_leave");
        }
        else if (rc == GROUP_ERR_NOT_FOUND) {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "not_group_member");
        }
        else {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }


    // ============ Private Messaging (Nhắn tin 1-1) ============

    // PM_CHAT_START - Vào chat mode với user khác (để nhận real-time push)
    if (strcmp(msg.verb, "PM_CHAT_START") == 0) {
        char token[128], with_user[64];

        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "with", with_user, sizeof(with_user))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id;
        if (sessions_validate(token, &user_id) != SESS_OK) {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // Lấy username của mình
        char my_username[64];
        if (!accounts_get_username(user_id, my_username, sizeof(my_username))) {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "internal_error");
            proto_free(&msg);
            return 0;
        }

        // Lấy user_id của partner
        int partner_id = accounts_get_user_id(with_user);
        if (partner_id < 0) {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "user_not_found");
            proto_free(&msg);
            return 0;
        }

        // Set chat_partner để server biết push message tới ai
        sessions_set_chat_partner(user_id, partner_id);

        // Đánh dấu messages là đã đọc
        pm_mark_read(user_id, with_user);

        // Lấy history gần đây
        char history[8192] = {0};
        pm_get_history(user_id, with_user, history, sizeof(history), 50);

        char payload[8400];
        snprintf(payload, sizeof(payload), "with=%s me=%s history=%s", 
                 with_user, my_username, history[0] ? history : "empty");
        proto_send_ok(ctx->client_sock, msg.req_id, payload);

        proto_free(&msg);
        return 0;
    }

    // PM_CHAT_END - Thoát khỏi chat mode
    if (strcmp(msg.verb, "PM_CHAT_END") == 0) {
        char token[128];

        if (!kv_get(msg.payload, "token", token, sizeof(token))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id;
        if (sessions_validate(token, &user_id) != SESS_OK) {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // Lấy partner_id trước khi clear để mark_read
        int partner_id = sessions_get_chat_partner(user_id);
        if (partner_id > 0) {
            char partner_username[64];
            if (accounts_get_username(partner_id, partner_username, sizeof(partner_username))) {
                // Đánh dấu tất cả tin nhắn đã đọc khi thoát chat
                pm_mark_read(user_id, partner_username);
            }
        }

        // Xóa chat_partner
        sessions_set_chat_partner(user_id, 0);

        proto_send_ok(ctx->client_sock, msg.req_id, "status=chat_ended");
        proto_free(&msg);
        return 0;
    }

    // PM_SEND - Gửi private message
    if (strcmp(msg.verb, "PM_SEND") == 0) {
        char token[128], to_user[64], content[4096];

        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "to", to_user, sizeof(to_user)) ||
            !kv_get(msg.payload, "content", content, sizeof(content))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int from_user_id;
        if (sessions_validate(token, &from_user_id) != SESS_OK) {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        // Content đã được encode Base64 từ client
        int msg_id = 0;
        int rc = pm_send(from_user_id, to_user, content, &msg_id);

        if (rc == PM_OK) {
            // Gửi OK cho sender
            char payload[128];
            snprintf(payload, sizeof(payload), "msg_id=%d to=%s status=sent", msg_id, to_user);
            proto_send_ok(ctx->client_sock, msg.req_id, payload);

            // Push message tới recipient nếu họ đang trong chat mode với sender
            int to_user_id = accounts_get_user_id(to_user);
            if (to_user_id > 0 && sessions_is_chatting_with(to_user_id, from_user_id)) {
                int to_sock = sessions_get_socket(to_user_id);
                if (to_sock > 0) {
                    // Lấy username của sender
                    char from_username[64];
                    accounts_get_username(from_user_id, from_username, sizeof(from_username));

                    // Push message tới recipient
                    // Format: PUSH PM from=username content=base64 msg_id=N ts=timestamp
                    char push_msg[4500];
                    snprintf(push_msg, sizeof(push_msg), 
                             "PUSH PM from=%s content=%s msg_id=%d ts=%ld\r\n",
                             from_username, content, msg_id, (long)time(NULL));
                    send(to_sock, push_msg, strlen(push_msg), 0);
                }
            }
        }
        else if (rc == PM_ERR_SELF) {
            send_simple_err(ctx->client_sock, msg.req_id, 422, "cannot_send_to_self");
        }
        else if (rc == PM_ERR_NOT_FOUND) {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "user_not_found");
        }
        else {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // PM_HISTORY - Lấy history chat với user khác
    if (strcmp(msg.verb, "PM_HISTORY") == 0) {
        char token[128], with_user[64], limit_str[16];

        if (!kv_get(msg.payload, "token", token, sizeof(token)) ||
            !kv_get(msg.payload, "with", with_user, sizeof(with_user))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int limit = 50;
        if (kv_get(msg.payload, "limit", limit_str, sizeof(limit_str))) {
            limit = atoi(limit_str);
            if (limit <= 0 || limit > 100) limit = 50;
        }

        int user_id;
        if (sessions_validate(token, &user_id) != SESS_OK) {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        char history[8192] = {0};
        int rc = pm_get_history(user_id, with_user, history, sizeof(history), limit);

        if (rc == PM_OK) {
            char payload[8300];
            snprintf(payload, sizeof(payload), "with=%s messages=%s", with_user,
                     history[0] ? history : "empty");
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else if (rc == PM_ERR_NOT_FOUND) {
            send_simple_err(ctx->client_sock, msg.req_id, 404, "user_not_found");
        }
        else {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }

    // PM_CONVERSATIONS - Liệt kê tất cả conversations
    if (strcmp(msg.verb, "PM_CONVERSATIONS") == 0) {
        char token[128];

        if (!kv_get(msg.payload, "token", token, sizeof(token))) {
            send_simple_err(ctx->client_sock, msg.req_id, 400, "missing_fields");
            proto_free(&msg);
            return 0;
        }

        int user_id;
        if (sessions_validate(token, &user_id) != SESS_OK) {
            send_simple_err(ctx->client_sock, msg.req_id, 401, "invalid_token");
            proto_free(&msg);
            return 0;
        }

        char convos[2048] = {0};
        int rc = pm_get_conversations(user_id, convos, sizeof(convos));

        if (rc == PM_OK) {
            char payload[2200];
            snprintf(payload, sizeof(payload), "conversations=%s", 
                     convos[0] ? convos : "empty");
            proto_send_ok(ctx->client_sock, msg.req_id, payload);
        }
        else {
            send_simple_err(ctx->client_sock, msg.req_id, 500, "server_error");
        }

        proto_free(&msg);
        return 0;
    }
    
    send_simple_err(ctx->client_sock, msg.req_id, 404, "unknown_command");
    proto_free(&msg);
    return 0;
}
