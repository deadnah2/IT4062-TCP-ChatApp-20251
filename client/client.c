#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../common/framing.h"

/*
 * client/client.c
 * - Client dòng lệnh để test nhanh server.
 * - Sau khi LOGIN thành công, client lưu token trong biến `token` để dùng cho WHOAMI/LOGOUT.
 * - "Raw send" cho phép gõ trực tiếp 1 dòng request theo protocol để debug verb mới.
 */

// Connect TCP đến server.
static int connect_to(const char *ip, unsigned short port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(s);
        return -1;
    }

    return s;
}

static void trim_line(char *s)
{
    // Loại bỏ \n/\r ở cuối input fgets.
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
    {
        s[n - 1] = 0;
        n--;
    }
}

static int send_line(int sock, const char *line)
{
    // Gửi 1 dòng theo framing: line + "\r\n".
    if (!line)
        return -1;
    if (send(sock, line, (int)strlen(line), 0) <= 0)
        return -1;
    if (send(sock, "\r\n", 2, 0) <= 0)
        return -1;
    return 0;
}

/*
 * kv_get
 * - Parse payload dạng "k=v k=v ..." và lấy value theo `key`.
 * - Bản sao của server/handlers.c (đủ dùng cho client demo).
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

static int parse_response(const char *line,
                          char *kind, size_t kind_cap,
                          char *rid, size_t rid_cap,
                          char *rest, size_t rest_cap)
{
    (void)kind_cap;
    (void)rid_cap;

    if (!line)
        return -1;
    kind[0] = 0;
    rid[0] = 0;
    rest[0] = 0;

    const char *p = line;
    while (*p == ' ')
        p++;

    if (sscanf(p, "%31s %31s", kind, rid) != 2)
        return -1;

    const char *t = strstr(p, rid);
    if (!t)
        return 0;
    t += strlen(rid);
    while (*t == ' ')
        t++;

    strncpy(rest, t, rest_cap - 1);
    rest[rest_cap - 1] = 0;
    return 0;
}

void client_show_friend_list(
    int sock,
    LineFramer *fr,
    const char *token,
    int *next_id);

static void menu(int logged_in)
{
    // UI đơn giản để test; chỉ hiện Logout khi đang có token.
    printf("\n=== MENU ===\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Whoami\n");
    printf("4. Raw send\n");
    if (logged_in)
    {
        printf("5. Logout\n");
        printf("6. Add friend (send invite)\n");
        printf("7. View friend invites\n");
        printf("8. View friend list\n");
    }
    printf("0. Exit\n");
    printf("============\n");
    if (logged_in)
        printf("(Logged in)\n");
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    unsigned short port = (unsigned short)atoi(argv[2]);

    int s = connect_to(ip, port);
    if (s < 0)
    {
        printf("Failed to connect\n");
        return 1;
    }

    LineFramer fr;
    framer_init(&fr, 2048);

    // Token session hiện tại (rỗng nếu chưa login).
    char token[128] = {0};
    int next_id = 1;

    for (;;)
    {
        menu(token[0] != 0);
        printf("> ");
        fflush(stdout);

        int choice = 0;
        if (scanf("%d", &choice) != 1)
        {
            while (getchar() != '\n')
                ;
            continue;
        }
        while (getchar() != '\n')
            ;

        if (choice == 0)
            break;

        char rid[32];
        snprintf(rid, sizeof(rid), "%d", next_id++);

        char req[2048];
        memset(req, 0, sizeof(req));

        if (choice == 1)
        {
            char username[64], password[128], email[128];

            printf("Username: ");
            if (!fgets(username, sizeof(username), stdin))
                break;
            trim_line(username);

            printf("Password: ");
            if (!fgets(password, sizeof(password), stdin))
                break;
            trim_line(password);

            printf("Email: ");
            if (!fgets(email, sizeof(email), stdin))
                break;
            trim_line(email);

            snprintf(req, sizeof(req), "REGISTER %s username=%s password=%s email=%s", rid, username, password, email);
            send_line(s, req);
        }
        else if (choice == 2)
        {
            char username[64], password[128];

            printf("Username: ");
            if (!fgets(username, sizeof(username), stdin))
                break;
            trim_line(username);

            printf("Password: ");
            if (!fgets(password, sizeof(password), stdin))
                break;
            trim_line(password);

            snprintf(req, sizeof(req), "LOGIN %s username=%s password=%s", rid, username, password);
            send_line(s, req);
        }
        else if (choice == 3)
        {
            if (!token[0])
            {
                printf("Not logged in.\n");
                continue;
            }
            snprintf(req, sizeof(req), "WHOAMI %s token=%s", rid, token);
            send_line(s, req);
        }
        else if (choice == 4)
        {
            printf("Type raw request line: ");
            if (!fgets(req, sizeof(req), stdin))
                break;
            trim_line(req);
            if (req[0])
                send_line(s, req);
        }
        else if (choice == 5)
        {
            if (!token[0])
            {
                printf("Not logged in.\n");
                continue;
            }
            snprintf(req, sizeof(req), "LOGOUT %s token=%s", rid, token);
            send_line(s, req);
        }
        else if (choice == 6)
        {
            if (!token[0])
            {
                printf("Not logged in.\n");
                continue;
            }

            char to[64];
            printf("Friend username: ");
            if (!fgets(to, sizeof(to), stdin))
                break;
            trim_line(to);

            if (!to[0])
            {
                printf("Username cannot be empty\n");
                continue;
            }

            snprintf(req, sizeof(req),
                     "FRIEND_INVITE %s token=%s username=%s",
                     rid, token, to);

            send_line(s, req);
        }
        else if (choice == 7)
        {
            if (!token[0])
            {
                printf("Not logged in.\n");
                continue;
            }

            // 1. Request pending list
            snprintf(req, sizeof(req),
                     "FRIEND_PENDING %s token=%s",
                     rid, token);
            send_line(s, req);

            char resp[4096];
            int r = framer_recv_line(s, &fr, resp, sizeof(resp));
            if (r <= 0)
            {
                printf("Disconnected\n");
                break;
            }

            char kind[32], rrid[32], rest[4096];
            parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));

            printf("< %s\n", resp);

            if (strcmp(kind, "OK") != 0)
            {
                continue;
            }

            char users[2048] = {0};
            if (!kv_get(rest, "username", users, sizeof(users)) || users[0] == 0)
            {
                printf("No pending friend invites.\n");
                continue;
            }

            printf("\nPending friend invites:\n");

            // In danh sách
            char tmp[2048];
            snprintf(tmp, sizeof(tmp), "%s", users);
            char *tok = strtok(tmp, ",");
            while (tok)
            {
                printf(" - %s\n", tok);
                tok = strtok(NULL, ",");
            }

            // 2. Sub menu
            for (;;)
            {
                printf("\nType username to accept/reject, or 'c' to cancel\n");
                printf("Format: a <username> | r <username> | c\n> ");

                char line[256];
                if (!fgets(line, sizeof(line), stdin))
                    break;
                trim_line(line);
                if (strcmp(line, "c") == 0)
                {
                    printf("Cancel!\n");
                    break;
                }

                char cmd;
                char uname[64];
                if (sscanf(line, "%c %63s", &cmd, uname) != 2)
                {
                    printf("Invalid input\n");
                    continue;
                }

                char rid2[32];
                snprintf(rid2, sizeof(rid2), "%d", next_id++);

                if (cmd == 'a')
                {
                    snprintf(req, sizeof(req),
                             "FRIEND_ACCEPT %s token=%s username=%s",
                             rid2, token, uname);
                }
                else if (cmd == 'r')
                {
                    snprintf(req, sizeof(req),
                             "FRIEND_REJECT %s token=%s username=%s",
                             rid2, token, uname);
                }
                else
                {
                    printf("Unknown command\n");
                    continue;
                }

                send_line(s, req);

                r = framer_recv_line(s, &fr, resp, sizeof(resp));
                if (r <= 0)
                {
                    printf("Disconnected\n");
                    break;
                }

                parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
                printf("< %s\n", resp);
                // break;
            }
            continue;
        }
        else if (choice == 8)
        {
            if (!token[0])
            {
                printf("Not logged in.\n");
                continue;
            }

            client_show_friend_list(s, &fr, token, &next_id);

            continue;
        }

        else
        {
            printf("Invalid choice\n");
            continue;
        }

        char resp[4096];
        int r = framer_recv_line(s, &fr, resp, sizeof(resp));
        if (r == 0)
        {
            printf("Disconnected\n");
            break;
        }
        if (r < 0)
        {
            printf("Receive error\n");
            break;
        }

        char kind[32], rrid[32], rest[4096];
        parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));

        printf("< %s\n", resp);

        if (strcmp(kind, "OK") == 0)
        {
            char t[128];
            if (kv_get(rest, "token", t, sizeof(t)))
            {
                strncpy(token, t, sizeof(token) - 1);
                token[sizeof(token) - 1] = 0;
            }
            if (choice == 5)
            {
                // LOGOUT thành công: xoá token local để UI/state phản ánh đúng trạng thái đăng nhập.
                token[0] = 0;
            }
            if (choice == 4)
            {
                // Demo behaviour: raw send dùng để debug nên reset token để tránh nhầm trạng thái.
                // Nếu muốn giữ phiên sau khi raw send, có thể bỏ đoạn này.
                token[0] = 0;
            }
        }
    }

    framer_free(&fr);
    close(s);
    return 0;
}

void client_show_friend_list( int sock, LineFramer *fr, const char *token, int *next_id){
    for (;;)
    {
        char req[512];
        char rid[32];

        snprintf(rid, sizeof(rid), "%d", (*next_id)++);
        snprintf(req, sizeof(req),
                 "FRIEND_LIST %s token=%s",
                 rid, token);

        send_line(sock, req);

        char resp[4096];
        int r = framer_recv_line(sock, fr, resp, sizeof(resp));
        if (r <= 0)
        {
            printf("Disconnected\n");
            return;
        }

        char kind[32], rrid[32], rest[4096];
        parse_response(resp,
                       kind, sizeof(kind),
                       rrid, sizeof(rrid),
                       rest, sizeof(rest));

        printf("< %s\n", resp);

        if (strcmp(kind, "OK") != 0)
        {
            return;
        }

        char users[2048] = {0};
        if (!kv_get(rest, "username", users, sizeof(users)) || users[0] == 0)
        {
            printf("No friend yet.\n");
            return;
        }

        printf("\nFriend list:\n");

        char tmp[2048];
        snprintf(tmp, sizeof(tmp), "%s", users);

        char *tok = strtok(tmp, ",");
        while (tok)
        {
            printf(" - %s\n", tok);
            tok = strtok(NULL, ",");
        }

        // 2. Sub menu
        // for (;;)
        // {
        printf("\nType username to delete, or 'c' to cancel\n");
        printf("Format: d <username> | c\n> ");

        char line[256];
        if (!fgets(line, sizeof(line), stdin))
            break;
        trim_line(line);
        if (strcmp(line, "c") == 0)
        {
            printf("Cancel!\n");
            break;
        }

        char cmd;
        char uname[64];
        if (sscanf(line, "%c %63s", &cmd, uname) != 2)
        {
            printf("Invalid input\n");
            continue;
        }

        char rid2[32];
        snprintf(rid2, sizeof(rid2), "%d", (*next_id)++);

        if (cmd == 'd')
        {
            snprintf(req, sizeof(req),
                     "FRIEND_DELETE %s token=%s username=%s",
                     rid2, token, uname);
        }
        else
        {
            printf("Unknown command\n");
            continue;
        }

        send_line(sock, req);

        r = framer_recv_line(sock, fr, resp, sizeof(resp));
        if (r <= 0)
        {
            printf("Disconnected\n");
            break;
        }

        parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
        printf("< %s\n", resp);
        // break;
    }
}
