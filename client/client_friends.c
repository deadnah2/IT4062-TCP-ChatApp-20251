/*
 * client/client_friends.c
 * - Các thao tác bạn bè: gửi lời mời, xem/xử lý lời mời, danh sách, xóa bạn.
 * - Các verb: FRIEND_INVITE, FRIEND_PENDING, FRIEND_ACCEPT, FRIEND_REJECT,
 *             FRIEND_LIST, FRIEND_DELETE.
 */

#include "client.h"

/*
 * cmd_friend_invite
 * - Gửi lời mời kết bạn đến user khác.
 * - Request: FRIEND_INVITE req_id token=... username=...
 */
void cmd_friend_invite(ClientState *cs)
{
    if (!cs->token[0]) {
        printf("Not logged in.\n");
        return;
    }

    char to[64];
    printf("Friend username: ");
    if (!fgets(to, sizeof(to), stdin))
        return;
    trim_line(to);

    if (!to[0]) {
        printf("Username cannot be empty\n");
        return;
    }

    char rid[32];
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);

    char req[512];
    snprintf(req, sizeof(req), "FRIEND_INVITE %s token=%s username=%s",
             rid, cs->token, to);
    send_line(cs->sock, req);

    char resp[4096];
    int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }

    printf("< %s\n", resp);
}

/*
 * cmd_friend_pending
 * - Hiển thị và quản lý các lời mời kết bạn đang chờ.
 * - Luồng xử lý:
 *   1. Gửi FRIEND_PENDING request
 *   2. Parse response, lấy danh sách username
 *   3. Hiển thị danh sách có icon đẹp
 *   4. Vòng lặp cho phép user accept/reject
 *
 * Request: FRIEND_PENDING <rid> token=<token>
 * Response: OK <rid> username=user1,user2,...
 *
 * Lệnh con:
 *   a <username> - Chấp nhận lời mời
 *   r <username> - Từ chối lời mời
 *   c            - Hủy và quay lại menu
 */
void cmd_friend_pending(ClientState *cs)
{
    if (!cs->token[0]) {
        printf("Not logged in.\n");
        return;
    }

    // 1. Lấy danh sách lời mời đang chờ
    char rid[32];
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);

    char req[512];
    snprintf(req, sizeof(req), "FRIEND_PENDING %s token=%s", rid, cs->token);
    send_line(cs->sock, req);

    char resp[4096];
    int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }

    char kind[32], rrid[32], rest[4096];
    parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
    printf("< %s\n", resp);

    if (strcmp(kind, "OK") != 0)
        return;

    // 2. Trích xuất danh sách username từ response
    char users[2048] = {0};
    if (!kv_get(rest, "username", users, sizeof(users)) || users[0] == 0) {
        printf("No pending friend invites.\n");
        return;
    }

    // 3. Hiển thị danh sách chờ
    printf("\n" C_TITLE ICON_INVITE " Pending friend invites\n");
    printf("────────────────────────\n" C_RESET);

    // Tách username bằng dấu phẩy
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", users);

    int idx = 1;
    char *tok = strtok(tmp, ",");
    while (tok) {
        printf(C_INFO " %2d. " ICON_USER " %s\n" C_RESET, idx++, tok);
        tok = strtok(NULL, ",");
    }

    if (idx == 1) {
        printf(C_DIM " (No pending invites)\n" C_RESET);
    }

    // 4. Vòng lặp menu phụ
    for (;;) {
        printf("\nType username to accept/reject, or 'c' to cancel\n");
        printf("Format: a <username> | r <username> | c\n> ");

        char line[256];
        if (!fgets(line, sizeof(line), stdin))
            break;
        trim_line(line);

        if (strcmp(line, "c") == 0) {
            printf("Cancel!\n");
            break;
        }

        // Parse lệnh và tên người dùng
        char cmd;
        char uname[64];
        if (sscanf(line, "%c %63s", &cmd, uname) != 2) {
            printf("Invalid input\n");
            continue;
        }

        snprintf(rid, sizeof(rid), "%d", cs->next_id++);

        // Tạo request theo lệnh
        if (cmd == 'a') {
            // Chấp nhận lời mời kết bạn
            snprintf(req, sizeof(req), "FRIEND_ACCEPT %s token=%s username=%s",
                     rid, cs->token, uname);
        } else if (cmd == 'r') {
            // Từ chối lời mời kết bạn
            snprintf(req, sizeof(req), "FRIEND_REJECT %s token=%s username=%s",
                     rid, cs->token, uname);
        } else {
            printf("Unknown command\n");
            continue;
        }

        send_line(cs->sock, req);

        r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
        if (r <= 0) {
            printf("Disconnected\n");
            break;
        }

        parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
        printf("< %s\n", resp);
    }
}

/*
 * cmd_friend_list
 * - Hiển thị danh sách bạn bè với trạng thái online/offline.
 * - Cho phép xóa bạn bè.
 *
 * Request: FRIEND_LIST <rid> token=<token>
 * Response: OK <rid> username=user1:online,user2:offline,...
 *
 * Lệnh con:
 *   d <username> - Xóa bạn bè
 *   c            - Hủy/quay lại
 */
void cmd_friend_list(ClientState *cs)
{
    if (!cs->token[0]) {
        printf("Not logged in.\n");
        return;
    }

    // Vòng lặp chính - làm mới danh sách sau mỗi thao tác
    for (;;) {
        char rid[32];
        snprintf(rid, sizeof(rid), "%d", cs->next_id++);

        char req[512];
        snprintf(req, sizeof(req), "FRIEND_LIST %s token=%s", rid, cs->token);
        send_line(cs->sock, req);

        char resp[4096];
        int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
        if (r <= 0) {
            printf("Disconnected\n");
            return;
        }

        char kind[32], rrid[32], rest[4096];
        parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
        printf("< %s\n", resp);

        if (strcmp(kind, "OK") != 0)
            return;

        // Lấy danh sách từ response
        char users[2048] = {0};
        if (!kv_get(rest, "username", users, sizeof(users)) || users[0] == 0) {
            printf("No friend yet.\n");
            return;
        }

        // Hiển thị header
        printf("\n" C_TITLE ICON_FRIEND " Friend list\n");
        printf("────────────────────────\n" C_RESET);

        char tmp[2048];
        snprintf(tmp, sizeof(tmp), "%s", users);

        // Hiển thị từng bạn với trạng thái
        int idx = 1;
        char *tok = strtok(tmp, ",");
        while (tok) {
            // Parse định dạng "username:status"
            char *colon = strchr(tok, ':');
            if (colon) {
                *colon = '\0';
                const char *username = tok;
                const char *status = colon + 1;

                // Hiển thị màu theo trạng thái
                if (strcmp(status, "online") == 0) {
                    printf(C_OK " %2d. " ICON_USER " %s  " ICON_ONLINE " online\n" C_RESET,
                           idx++, username);
                } else {
                    printf(C_DIM " %2d. " ICON_USER " %s  " ICON_OFFLINE " offline\n" C_RESET,
                           idx++, username);
                }
            } else {
                // Dự phòng nếu không có trạng thái
                printf(C_OK " %2d. " ICON_USER " %s\n" C_RESET, idx++, tok);
            }
            tok = strtok(NULL, ",");
        }

        if (idx == 1) {
            printf(C_DIM " (No friends yet)\n" C_RESET);
        }

        // Menu phụ
        printf("\nType username to delete, or 'c' to cancel\n");
        printf("Format: d <username> | c\n> ");

        char line[256];
        if (!fgets(line, sizeof(line), stdin))
            break;
        trim_line(line);

        if (strcmp(line, "c") == 0) {
            printf("Cancel!\n");
            break;
        }

        // Parse lệnh và tên
        char cmd;
        char uname[64];
        if (sscanf(line, "%c %63s", &cmd, uname) != 2) {
            printf("Invalid input\n");
            continue;
        }

        snprintf(rid, sizeof(rid), "%d", cs->next_id++);

        // Xử lý lệnh xóa bạn
        if (cmd == 'd') {
            snprintf(req, sizeof(req), "FRIEND_DELETE %s token=%s username=%s",
                     rid, cs->token, uname);
        } else {
            printf("Unknown command\n");
            continue;
        }

        send_line(cs->sock, req);

        r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
        if (r <= 0) {
            printf("Disconnected\n");
            break;
        }

        parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
        printf("< %s\n", resp);
        // Vòng lặp tiếp tục để làm mới danh sách
    }
}
