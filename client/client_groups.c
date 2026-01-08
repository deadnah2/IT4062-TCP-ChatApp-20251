/*
 * client/client_groups.c
 * - Các thao tác quản lý nhóm: tạo, thêm, xóa thành viên, rời nhóm, xem thành viên.
 * - Các verb: GROUP_LIST, GROUP_CREATE, GROUP_ADD, GROUP_REMOVE, GROUP_LEAVE, GROUP_MEMBERS.
 * - Điều hướng đến chế độ chat nhóm.
 */

#include "client.h"

/*
 * cmd_groups_menu
 * - Menu quản lý nhóm tương tác.
 * - Luồng xử lý:
 *   1. Lấy và hiển thị danh sách nhóm
 *   2. Hiển thị các lệnh có sẵn
 *   3. Parse và thực thi lệnh từ user
 *   4. Lặp cho đến khi user nhập 'q'
 *
 * Các lệnh:
 *   c <name>           - Tạo nhóm mới
 *   a <gid> <user>     - Thêm thành viên (chỉ owner)
 *   r <gid> <user>     - Xóa thành viên (chỉ owner)
 *   m <gid>            - Xem thành viên
 *   l <gid>            - Rời nhóm
 *   g <gid>            - Vào chat nhóm
 *   q                  - Quay lại menu chính
 */
void cmd_groups_menu(ClientState *cs)
{
    if (!cs->token[0]) {
        printf("Not logged in.\n");
        return;
    }

    // Vòng lặp tương tác chính
    for (;;) {
        // 1. Yêu cầu danh sách nhóm
        char rid[32];
        snprintf(rid, sizeof(rid), "%d", cs->next_id++);

        char req[512];
        snprintf(req, sizeof(req), "GROUP_LIST %s token=%s", rid, cs->token);
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

        // 2. Trích xuất và hiển thị nhóm
        char groups[2048] = {0};
        kv_get(rest, "groups", groups, sizeof(groups));

        printf("\n" C_TITLE "👥 Your Groups\n");
        printf("────────────────────────\n" C_RESET);

        if (!groups[0]) {
            printf(C_DIM " (You are not in any group)\n" C_RESET);
        } else {
            // Tách các ID nhóm bằng dấu phẩy
            char tmp[2048];
            snprintf(tmp, sizeof(tmp), "%s", groups);

            int idx = 1;
            char *tok = strtok(tmp, ",");
            while (tok) {
                printf(C_OK " %2d. 🆔 Group ID: %s\n" C_RESET, idx++, tok);
                tok = strtok(NULL, ",");
            }
        }

        // 3. Hiển thị menu lệnh
        printf("\nCommands:\n");
        printf(" c <name>              Create group\n");
        printf(" a <gid> <user>        Add member (owner)\n");
        printf(" r <gid> <user>        Remove member (owner)\n");
        printf(" m <gid>               View members\n");
        printf(" l <gid>               Leave group\n");
        printf(" " C_OK "g <gid>               💬 Enter group chat\n" C_RESET);
        printf(" q                     Back to menu\n");
        printf("> ");

        // 4. Đọc và parse đầu vào
        char line[256];
        if (!fgets(line, sizeof(line), stdin))
            return;
        trim_line(line);

        if (strcmp(line, "q") == 0)
            return;

        // Parse các thành phần lệnh
        char cmd;
        int gid;
        char arg1[64];

        // 5. Thực thi lệnh theo đầu vào
        if (sscanf(line, "%c %d %63s", &cmd, &gid, arg1) == 3 && cmd == 'a') {
            // ADD MEMBER - Chủ nhóm thêm user
            snprintf(rid, sizeof(rid), "%d", cs->next_id++);
            snprintf(req, sizeof(req), "GROUP_ADD %s token=%s group_id=%d username=%s",
                     rid, cs->token, gid, arg1);
        } else if (sscanf(line, "%c %d %63s", &cmd, &gid, arg1) == 3 && cmd == 'r') {
            // REMOVE MEMBER - Chủ nhóm xóa user
            snprintf(rid, sizeof(rid), "%d", cs->next_id++);
            snprintf(req, sizeof(req), "GROUP_REMOVE %s token=%s group_id=%d username=%s",
                     rid, cs->token, gid, arg1);
        } else if (sscanf(line, "%c %63s", &cmd, arg1) == 2 && cmd == 'c') {
            // CREATE GROUP - Tạo nhóm mới, user thành owner
            snprintf(rid, sizeof(rid), "%d", cs->next_id++);
            snprintf(req, sizeof(req), "GROUP_CREATE %s token=%s name=%s",
                     rid, cs->token, arg1);
        } else if (sscanf(line, "%c %d", &cmd, &gid) == 2 && cmd == 'm') {
            // VIEW MEMBERS - Liệt kê tất cả thành viên nhóm
            snprintf(rid, sizeof(rid), "%d", cs->next_id++);
            snprintf(req, sizeof(req), "GROUP_MEMBERS %s token=%s group_id=%d",
                     rid, cs->token, gid);
        } else if (sscanf(line, "%c %d", &cmd, &gid) == 2 && cmd == 'l') {
            // LEAVE GROUP - User rời khỏi nhóm
            snprintf(rid, sizeof(rid), "%d", cs->next_id++);
            snprintf(req, sizeof(req), "GROUP_LEAVE %s token=%s group_id=%d",
                     rid, cs->token, gid);
        } else if (sscanf(line, "%c %d", &cmd, &gid) == 2 && cmd == 'g') {
            // ENTER GROUP CHAT - Vào chế độ chat nhóm thời gian thực
            cmd_group_chat_mode(cs, gid);
            continue;  // Bỏ qua xử lý response, quay lại menu
        } else {
            printf("Invalid command\n");
            continue;
        }

        // Gửi request và nhận response
        send_line(cs->sock, req);

        r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
        if (r <= 0) {
            printf("Disconnected\n");
            return;
        }

        parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
        printf("< %s\n", resp);

        // Xử lý đặc biệt cho response xem thành viên
        if (strcmp(kind, "OK") == 0 && cmd == 'm') {
            char users[2048] = {0};
            kv_get(rest, "members", users, sizeof(users));

            printf("\nMembers:\n");
            char tmp[2048];
            snprintf(tmp, sizeof(tmp), "%s", users);

            // Tách và hiển thị từng thành viên
            char *tok = strtok(tmp, ",");
            while (tok) {
                printf(" - 👤 %s\n", tok);
                tok = strtok(NULL, ",");
            }
        }
        // Vòng lặp tiếp tục để làm mới danh sách
    }
}
