#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>
#include <time.h>

#include "../common/framing.h"

// ===== ANSI colors =====
#define C_RESET "\033[0m"
#define C_TITLE "\033[1;36m" // Cyan bold
#define C_MENU "\033[1;33m"  // Yellow
#define C_OK "\033[1;32m"    // Green
#define C_WARN "\033[1;31m"  // Red
#define C_INFO "\033[1;34m"  // Blue
#define C_DIM "\033[2m"
#define C_MSG_ME "\033[1;32m"    // Green for my messages
#define C_MSG_OTHER "\033[1;36m" // Cyan for other's messages

// ===== Icons =====
#define ICON_USER "👤"
#define ICON_LOGIN "🔐"
#define ICON_LOGOUT "🚪"
#define ICON_FRIEND "🤝"
#define ICON_GROUP "👥"
#define ICON_LIST "📜"
#define ICON_INVITE "📨"
#define ICON_EXIT "❌"
#define ICON_RAW "🧪"
#define ICON_ID "🆔"
#define ICON_ONLINE "🟢"
#define ICON_OFFLINE "⚫"
#define ICON_CHAT "💬"
#define ICON_SEND "➤"

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

// ============ Base64 Utilities ============

static const char b64_table[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const int b64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static int base64_encode(const unsigned char* src, size_t src_len, 
                         char* out, size_t out_cap)
{
    size_t out_len = ((src_len + 2) / 3) * 4;
    if (out_len + 1 > out_cap) return -1;
    
    size_t i = 0, j = 0;
    while (i < src_len) {
        unsigned int a = i < src_len ? src[i++] : 0;
        unsigned int b = i < src_len ? src[i++] : 0;
        unsigned int c = i < src_len ? src[i++] : 0;
        
        unsigned int triple = (a << 16) | (b << 8) | c;
        
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }
    
    size_t mod = src_len % 3;
    if (mod == 1) {
        out[j - 1] = '=';
        out[j - 2] = '=';
    } else if (mod == 2) {
        out[j - 1] = '=';
    }
    
    out[j] = '\0';
    return (int)j;
}

static int base64_decode(const char* src, unsigned char* out, size_t out_cap)
{
    size_t src_len = strlen(src);
    if (src_len % 4 != 0) return -1;
    
    size_t out_len = (src_len / 4) * 3;
    if (src_len > 0 && src[src_len - 1] == '=') out_len--;
    if (src_len > 1 && src[src_len - 2] == '=') out_len--;
    
    if (out_len + 1 > out_cap) return -1;
    
    size_t i = 0, j = 0;
    while (i < src_len) {
        int a = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]]; i++;
        int b = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]]; i++;
        int c = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]]; i++;
        int d = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]]; i++;
        
        if (a < 0 || b < 0 || c < 0 || d < 0) return -1;
        
        unsigned int triple = (a << 18) | (b << 12) | (c << 6) | d;
        
        if (j < out_len) out[j++] = (triple >> 16) & 0xFF;
        if (j < out_len) out[j++] = (triple >> 8) & 0xFF;
        if (j < out_len) out[j++] = triple & 0xFF;
    }
    
    out[j] = '\0';
    return (int)out_len;
}

// ============ End Base64 ============

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

void client_show_groups(int sock, LineFramer *fr, const char *token, int *next_id);

void client_chat_mode(int sock, LineFramer *fr, const char *token, int *next_id);

void client_group_chat_mode(int sock, LineFramer *fr, const char *token, int *next_id, int group_id);

static void menu(int logged_in)
{
    printf("\n" C_TITLE "══════════════════════════════════\n");
    printf("        💬 CHAT CLIENT MENU        \n");
    printf("══════════════════════════════════\n" C_RESET);

    if (!logged_in)
    {
        printf(C_MENU " 1. " ICON_USER " Register\n");
        printf(" 2. " ICON_LOGIN " Login\n");
    }
    printf(C_MENU " 3. " ICON_ID " Whoami\n");
    printf(" 4. " ICON_RAW " Raw send\n");

    if (logged_in)
    {
        printf(" 5. " ICON_LOGOUT " Logout\n");
        printf(" 6. " ICON_INVITE " Add friend (send invite)\n");
        printf(" 7. " ICON_LIST " View friend invites\n");
        printf(" 8. " ICON_FRIEND " View friend list\n");
        printf(" 9. " ICON_GROUP " Group\n");
        printf("10. " ICON_CHAT " Chat (Private Message)\n");
    }

    printf(" 0. " ICON_EXIT " Exit\n");
    printf(C_TITLE "══════════════════════════════════\n" C_RESET);

    if (logged_in)
        printf(C_OK "✔ Logged in\n" C_RESET);
    else
        printf(C_DIM "Not logged in\n" C_RESET);
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
        {
            // Exit - gửi DISCONNECT nếu đang login để thoát sạch
            if (token[0]) {
                char disc_rid[32];
                snprintf(disc_rid, sizeof(disc_rid), "%d", next_id++);
                char disc_req[256];
                snprintf(disc_req, sizeof(disc_req), "DISCONNECT %s token=%s", disc_rid, token);
                send_line(s, disc_req);
                
                // Đọc response
                char resp[4096];
                int r = framer_recv_line(s, &fr, resp, sizeof(resp));
                if (r > 0) {
                    printf("< %s\n", resp);
                }
                printf(C_OK "Disconnected from server.\n" C_RESET);
            }
            break;
        }

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

            printf("\n" C_TITLE ICON_INVITE " Pending friend invites\n");
            printf("────────────────────────\n" C_RESET);

            char tmp[2048];
            snprintf(tmp, sizeof(tmp), "%s", users);

            int idx = 1;
            char *tok = strtok(tmp, ",");
            while (tok)
            {
                printf(C_INFO " %2d. " ICON_USER " %s\n" C_RESET, idx++, tok);
                tok = strtok(NULL, ",");
            }

            if (idx == 1)
            {
                printf(C_DIM " (No pending invites)\n" C_RESET);
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

        else if (choice == 9)
        {
            if (!token[0])
            {
                printf("Not logged in.\n");
                continue;
            }

            client_show_groups(s, &fr, token, &next_id);
            continue;
        }

        else if (choice == 10)
        {
            if (!token[0])
            {
                printf("Not logged in.\n");
                continue;
            }

            // Start chat mode
            client_chat_mode(s, &fr, token, &next_id);
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
                // token[0] = 0;
            }
        }
    }

    framer_free(&fr);
    close(s);
    return 0;
}

void client_show_friend_list(int sock, LineFramer *fr, const char *token, int *next_id)
{
    for (;;)
    {
        char req[512];
        char rid[32];
        // printf("next_id = %d\n", *next_id);

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

        printf("\n" C_TITLE ICON_FRIEND " Friend list\n");
        printf("────────────────────────\n" C_RESET);

        char tmp[2048];
        snprintf(tmp, sizeof(tmp), "%s", users);

        int idx = 1;
        char *tok = strtok(tmp, ",");
        while (tok)
        {
            // Parse "username:status" format
            char *colon = strchr(tok, ':');
            if (colon)
            {
                *colon = '\0';
                const char *username = tok;
                const char *status = colon + 1;

                if (strcmp(status, "online") == 0)
                {
                    printf(C_OK " %2d. " ICON_USER " %s  " ICON_ONLINE " online\n" C_RESET, idx++, username);
                }
                else
                {
                    printf(C_DIM " %2d. " ICON_USER " %s  " ICON_OFFLINE " offline\n" C_RESET, idx++, username);
                }
            }
            else
            {
                printf(C_OK " %2d. " ICON_USER " %s\n" C_RESET, idx++, tok);
            }
            tok = strtok(NULL, ",");
        }

        if (idx == 1)
        {
            printf(C_DIM " (No friends yet)\n" C_RESET);
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

void client_show_groups(int sock, LineFramer *fr, const char *token, int *next_id)
{
    for (;;)
    {
        char req[512];
        char rid[32];

        snprintf(rid, sizeof(rid), "%d", (*next_id)++);
        snprintf(req, sizeof(req),
                 "GROUP_LIST %s token=%s",
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
        parse_response(resp, kind, sizeof(kind),
                       rrid, sizeof(rrid),
                       rest, sizeof(rest));

        printf("< %s\n", resp);

        if (strcmp(kind, "OK") != 0)
            return;

        char groups[2048] = {0};
        kv_get(rest, "groups", groups, sizeof(groups));

        printf("\n" C_TITLE "👥 Your Groups\n");
        printf("────────────────────────\n" C_RESET);

        if (!groups[0])
        {
            printf(C_DIM " (You are not in any group)\n" C_RESET);
        }
        else
        {
            char tmp[2048];
            snprintf(tmp, sizeof(tmp), "%s", groups);

            int idx = 1;
            char *tok = strtok(tmp, ",");
            while (tok)
            {
                printf(C_OK " %2d. 🆔 Group ID: %s\n" C_RESET, idx++, tok);
                tok = strtok(NULL, ",");
            }
        }

        printf("\nCommands:\n");
        printf(" c <name>              Create group\n");
        printf(" a <gid> <user>        Add member (owner)\n");
        printf(" r <gid> <user>        Remove member (owner)\n");
        printf(" m <gid>               View members\n");
        printf(" l <gid>               Leave group\n");
        printf(" " C_OK "g <gid>               💬 Enter group chat\n" C_RESET);
        printf(" q                     Back to menu\n");
        printf("> ");


        char line[256];
        if (!fgets(line, sizeof(line), stdin))
            return;
        trim_line(line);

        if (strcmp(line, "q") == 0)
            return;

        char cmd;
        int gid;
        char arg1[64];

        if (sscanf(line, "%c %d %63s", &cmd, &gid, arg1) == 3 && cmd == 'a')
        {
            // ADD MEMBER
            snprintf(rid, sizeof(rid), "%d", (*next_id)++);
            snprintf(req, sizeof(req),
                    "GROUP_ADD %s token=%s group_id=%d username=%s",
                    rid, token, gid, arg1);
        }
        else if (sscanf(line, "%c %d %63s", &cmd, &gid, arg1) == 3 && cmd == 'r')
        {
            // REMOVE MEMBER
            snprintf(rid, sizeof(rid), "%d", (*next_id)++);
            snprintf(req, sizeof(req),
                    "GROUP_REMOVE %s token=%s group_id=%d username=%s",
                    rid, token, gid, arg1);
        }
        else if (sscanf(line, "%c %63s", &cmd, arg1) == 2 && cmd == 'c')
        {
            // CREATE GROUP
            snprintf(rid, sizeof(rid), "%d", (*next_id)++);
            snprintf(req, sizeof(req),
                    "GROUP_CREATE %s token=%s name=%s",
                    rid, token, arg1);
        }
        else if (sscanf(line, "%c %d", &cmd, &gid) == 2 && cmd == 'm')
        {
            // VIEW MEMBERS
            snprintf(rid, sizeof(rid), "%d", (*next_id)++);
            snprintf(req, sizeof(req),
                    "GROUP_MEMBERS %s token=%s group_id=%d",
                    rid, token, gid);
        }
        else if (sscanf(line, "%c %d", &cmd, &gid) == 2 && cmd == 'l')
        {
            // LEAVE GROUP
            snprintf(rid, sizeof(rid), "%d", (*next_id)++);
            snprintf(req, sizeof(req),
                    "GROUP_LEAVE %s token=%s group_id=%d",
                    rid, token, gid);
        }
        else if (sscanf(line, "%c %d", &cmd, &gid) == 2 && cmd == 'g')
        {
            // ENTER GROUP CHAT
            client_group_chat_mode(sock, fr, token, next_id, gid);
            continue;  // Refresh group list after chat
        }
        else
        {
            printf("Invalid command\n");
            continue;
        }


        send_line(sock, req);

        r = framer_recv_line(sock, fr, resp, sizeof(resp));
        if (r <= 0)
        {
            printf("Disconnected\n");
            return;
        }

        parse_response(resp, kind, sizeof(kind),
                       rrid, sizeof(rrid),
                       rest, sizeof(rest));

        printf("< %s\n", resp);

        if (strcmp(kind, "OK") == 0 && cmd == 'm')
        {
            char users[2048] = {0};
            kv_get(rest, "members", users, sizeof(users));

            printf("\nMembers:\n");
            char tmp[2048];
            snprintf(tmp, sizeof(tmp), "%s", users);

            char *tok = strtok(tmp, ",");
            while (tok)
            {
                printf(" - 👤 %s\n", tok);
                tok = strtok(NULL, ",");
            }
        }
    }
}

// ============ Chat Mode Implementation ============
// Phần xử lý chế độ chat real-time 1-1
// - Main thread: đọc input từ user, gửi message
// - Receive thread: lắng nghe PUSH message từ server

// Global state cho chat mode
static volatile int g_in_chat_mode = 0;   // Flag đang trong chat mode
static volatile int g_chat_sock = -1;     // Socket dùng chung
static char g_chat_partner[64] = {0};     // Username người đang chat
static char g_my_username[64] = {0};      // Username của mình
static pthread_t g_recv_thread;           // Thread nhận message
static pthread_mutex_t g_print_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex cho printf

static void format_timestamp(long ts, char *out, size_t cap)
{
    time_t t = (time_t)ts;
    struct tm *tm_info = localtime(&t);
    strftime(out, cap, "%H:%M", tm_info);
}

static void print_message(const char *from, const char *content_b64, long ts)
{
    // Decode Base64 content thành plain text
    char content[2048];
    if (base64_decode(content_b64, (unsigned char *)content, sizeof(content)) < 0) {
        strcpy(content, "[decode error]");
    }
    
    char time_str[32];
    format_timestamp(ts, time_str, sizeof(time_str));
    
    pthread_mutex_lock(&g_print_mutex);
    
    int is_me = (g_my_username[0] && strcmp(from, g_my_username) == 0);
    
    if (is_me) {
        printf(C_MSG_ME "[%s] [You]: %s\n" C_RESET, time_str, content);
    } else {
        printf(C_MSG_OTHER "[%s] [%s]: %s\n" C_RESET, time_str, from, content);
    }
    fflush(stdout);
    
    pthread_mutex_unlock(&g_print_mutex);
}

// Thread function nhận message từ server (chạy song song với main thread)
// Lắng nghe PUSH PM và hiển thị real-time
static void *chat_recv_thread(void *arg)
{
    (void)arg;
    
    char buf[8192];
    int buf_len = 0;
    
    while (g_in_chat_mode && g_chat_sock >= 0) {
        // Dùng select() với timeout 200ms để poll socket
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms
        
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_chat_sock, &fds);
        
        int ready = select(g_chat_sock + 1, &fds, NULL, NULL, &tv);
        
        if (ready <= 0) continue;
        
        // Đọc data từ socket
        char tmp[1024];
        int n = recv(g_chat_sock, tmp, sizeof(tmp) - 1, 0);
        
        if (n <= 0) {
            if (n == 0) {
                pthread_mutex_lock(&g_print_mutex);
                printf(C_WARN "\n[Disconnected from server]\n" C_RESET);
                fflush(stdout);
                pthread_mutex_unlock(&g_print_mutex);
            }
            g_in_chat_mode = 0;
            break;
        }
        
        // Thêm data vào buffer
        if (buf_len + n < (int)sizeof(buf) - 1) {
            memcpy(buf + buf_len, tmp, n);
            buf_len += n;
            buf[buf_len] = '\0';
        }
        
        // Xử lý từng line hoàn chỉnh (kết thúc bằng \r\n)
        char *line_start = buf;
        char *crlf;
        while ((crlf = strstr(line_start, "\r\n")) != NULL) {
            *crlf = '\0';
            
            // Kiểm tra nếu là PUSH message từ server
            if (strncmp(line_start, "PUSH PM ", 8) == 0) {
                char *payload = line_start + 8;
                
                char from[64] = {0}, content[4096] = {0}, ts_str[32] = {0};
                
                // Parse các field từ payload
                kv_get(payload, "from", from, sizeof(from));
                kv_get(payload, "content", content, sizeof(content));
                kv_get(payload, "ts", ts_str, sizeof(ts_str));
                
                if (from[0] && content[0]) {
                    long ts = ts_str[0] ? atol(ts_str) : (long)time(NULL);
                    print_message(from, content, ts);
                }
            }
            // PUSH JOIN - đối phương vào cuộc trò chuyện
            else if (strncmp(line_start, "PUSH JOIN ", 10) == 0) {
                char *payload = line_start + 10;
                char user[64] = {0};
                kv_get(payload, "user", user, sizeof(user));
                if (user[0]) {
                    printf(C_INFO "\n  >>> %s đã vào cuộc trò chuyện <<<\n" C_RESET, user);
                    printf("> ");
                    fflush(stdout);
                }
            }
            // PUSH LEAVE - đối phương rời cuộc trò chuyện
            else if (strncmp(line_start, "PUSH LEAVE ", 11) == 0) {
                char *payload = line_start + 11;
                char user[64] = {0};
                kv_get(payload, "user", user, sizeof(user));
                if (user[0]) {
                    printf(C_WARN "\n  <<< %s đã rời cuộc trò chuyện >>>\n" C_RESET, user);
                    printf("> ");
                    fflush(stdout);
                }
            }
            
            line_start = crlf + 2;
        }
        
        // Di chuyển data chưa xử lý về đầu buffer
        if (line_start > buf) {
            int remaining = buf_len - (line_start - buf);
            if (remaining > 0) {
                memmove(buf, line_start, remaining);
            }
            buf_len = remaining;
            buf[buf_len] = '\0';
        }
    }
    
    return NULL;
}

static void display_chat_history(const char *history, const char *my_username)
{
    if (!history || strcmp(history, "empty") == 0 || !history[0]) {
        printf(C_DIM "  (No messages yet. Start the conversation!)\n" C_RESET);
        return;
    }
    
    // History format: msg_id:from:content_b64:ts,msg_id:from:content_b64:ts,...
    // Messages theo thứ tự mới nhất trước, cần reverse để hiển thị
    
    char tmp[8192];
    snprintf(tmp, sizeof(tmp), "%s", history);
    
    typedef struct {
        char from[64];
        char content[2048];
        long ts;
    } Msg;
    
    Msg msgs[100];
    int count = 0;
    
    char *saveptr;
    char *msg_tok = strtok_r(tmp, ",", &saveptr);
    while (msg_tok && count < 100) {
        char from[64], content[2048];
        long ts;
        
        // Parse msg_id:from:content:ts
        char *p = msg_tok;
        char *colon1 = strchr(p, ':');
        if (!colon1) { msg_tok = strtok_r(NULL, ",", &saveptr); continue; }
        
        // Skip msg_id
        p = colon1 + 1;
        
        char *colon2 = strchr(p, ':');
        if (!colon2) { msg_tok = strtok_r(NULL, ",", &saveptr); continue; }
        
        size_t from_len = colon2 - p;
        if (from_len >= sizeof(from)) from_len = sizeof(from) - 1;
        memcpy(from, p, from_len);
        from[from_len] = '\0';
        
        p = colon2 + 1;
        
        // Find last colon for timestamp
        char *last_colon = strrchr(p, ':');
        if (!last_colon) { msg_tok = strtok_r(NULL, ",", &saveptr); continue; }
        
        ts = atol(last_colon + 1);
        
        size_t content_len = last_colon - p;
        if (content_len >= sizeof(content)) content_len = sizeof(content) - 1;
        memcpy(content, p, content_len);
        content[content_len] = '\0';
        
        snprintf(msgs[count].from, sizeof(msgs[count].from), "%s", from);
        snprintf(msgs[count].content, sizeof(msgs[count].content), "%s", content);
        msgs[count].ts = ts;
        count++;
        
        msg_tok = strtok_r(NULL, ",", &saveptr);
    }
    
    // Display in reverse order (oldest first)
    for (int i = count - 1; i >= 0; i--) {
        // Decode and display
        char decoded[2048];
        if (base64_decode(msgs[i].content, (unsigned char *)decoded, sizeof(decoded)) < 0) {
            strcpy(decoded, "[decode error]");
        }
        
        char time_str[32];
        format_timestamp(msgs[i].ts, time_str, sizeof(time_str));
        
        int is_me = (my_username[0] && strcmp(msgs[i].from, my_username) == 0);
        
        if (is_me) {
            printf(C_MSG_ME "[%s] [You]: %s\n" C_RESET, time_str, decoded);
        } else {
            printf(C_MSG_OTHER "[%s] [%s]: %s\n" C_RESET, time_str, msgs[i].from, decoded);
        }
    }
}

void client_chat_mode(int sock, LineFramer *fr, const char *token, int *next_id)
{
    printf("\n" C_TITLE ICON_CHAT " Private Message\n");
    printf("══════════════════════════════════\n" C_RESET);
    
    // First, show conversations list
    char rid[32];
    snprintf(rid, sizeof(rid), "%d", (*next_id)++);
    
    char req[4096];
    snprintf(req, sizeof(req), "PM_CONVERSATIONS %s token=%s", rid, token);
    send_line(sock, req);
    
    char resp[8192];
    int r = framer_recv_line(sock, fr, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }
    
    char kind[32], rrid[32], rest[8192];
    parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
    
    if (strcmp(kind, "OK") == 0) {
        char convos[2048];
        if (kv_get(rest, "conversations", convos, sizeof(convos)) && 
            strcmp(convos, "empty") != 0 && convos[0]) {
            
            printf(C_INFO "\nRecent conversations:\n" C_RESET);
            
            char tmp[2048];
            snprintf(tmp, sizeof(tmp), "%s", convos);
            
            char *saveptr;
            char *tok = strtok_r(tmp, ",", &saveptr);
            while (tok) {
                char username[64];
                int unread = 0;
                
                char *colon = strchr(tok, ':');
                if (colon) {
                    *colon = '\0';
                    snprintf(username, sizeof(username), "%s", tok);
                    unread = atoi(colon + 1);
                } else {
                    snprintf(username, sizeof(username), "%s", tok);
                }
                
                if (unread > 0) {
                    printf("  " ICON_USER " %s " C_WARN "(%d new)\n" C_RESET, username, unread);
                } else {
                    printf("  " ICON_USER " %s\n", username);
                }
                
                tok = strtok_r(NULL, ",", &saveptr);
            }
        } else {
            printf(C_DIM "\nNo conversations yet.\n" C_RESET);
        }
    }
    
    // Ask who to chat with
    printf("\n" C_MENU "Enter username to chat with (or 'q' to cancel): " C_RESET);
    fflush(stdout);
    
    char partner[64];
    if (!fgets(partner, sizeof(partner), stdin)) return;
    trim_line(partner);
    
    if (partner[0] == '\0' || strcmp(partner, "q") == 0) {
        printf("Cancelled.\n");
        return;
    }
    
    // Start chat session with server
    snprintf(rid, sizeof(rid), "%d", (*next_id)++);
    snprintf(req, sizeof(req), "PM_CHAT_START %s token=%s with=%s", rid, token, partner);
    send_line(sock, req);
    
    r = framer_recv_line(sock, fr, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }
    
    parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
    
    if (strcmp(kind, "OK") != 0) {
        printf(C_WARN "Error: %s\n" C_RESET, rest);
        return;
    }
    
    // Store partner name and my username
    snprintf(g_chat_partner, sizeof(g_chat_partner), "%s", partner);
    
    // Get my username from server response
    if (!kv_get(rest, "me", g_my_username, sizeof(g_my_username))) {
        snprintf(g_my_username, sizeof(g_my_username), "You"); // Default
    }
    
    // Display header
    printf("\n");
    printf(C_TITLE "════════════════════════════════════════════\n");
    printf("       " ICON_CHAT " Chat with %s\n", partner);
    printf("════════════════════════════════════════════\n" C_RESET);
    printf(C_DIM "Type your message and press Enter to send.\n");
    printf("Type 'quit' or 'q' to exit chat.\n" C_RESET);
    printf(C_TITLE "────────────────────────────────────────────\n" C_RESET);
    
    // Get and display history
    char history[8192];
    if (kv_get(rest, "history", history, sizeof(history))) {
        display_chat_history(history, g_my_username);
    }
    
    printf(C_TITLE "────────────────────────────────────────────\n" C_RESET);
    
    // Set global state and start receive thread
    g_in_chat_mode = 1;
    g_chat_sock = sock;
    
    if (pthread_create(&g_recv_thread, NULL, chat_recv_thread, NULL) != 0) {
        printf(C_WARN "Failed to start receive thread\n" C_RESET);
        g_in_chat_mode = 0;
        return;
    }
    
    // Main chat loop - read user input and send (fire and forget)
    while (g_in_chat_mode) {
        char input[2048];
        
        printf(C_OK "> " C_RESET);
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        trim_line(input);
        
        if (input[0] == '\0') {
            continue;
        }
        
        // Kiểm tra lệnh quit
        if (strcmp(input, "quit") == 0 || strcmp(input, "q") == 0 || 
            strcmp(input, "/quit") == 0 || strcmp(input, "/q") == 0) {
            break;
        }
        
        // Encode message sang Base64 để gửi qua protocol
        char content_b64[4096];
        if (base64_encode((unsigned char *)input, strlen(input), 
                          content_b64, sizeof(content_b64)) < 0) {
            printf(C_WARN "Message too long\n" C_RESET);
            continue;
        }
        
        // Gửi message (fire and forget - không chờ response)
        // Tránh race condition với receive thread
        snprintf(rid, sizeof(rid), "%d", (*next_id)++);
        snprintf(req, sizeof(req), "PM_SEND %s token=%s to=%s content=%s",
                 rid, token, partner, content_b64);
        send_line(sock, req);
        
        // Hiển thị message ngay (optimistic UI)
        long ts = (long)time(NULL);
        print_message(g_my_username, content_b64, ts);
    }
    
    // Cleanup - báo hiệu receive thread dừng
    g_in_chat_mode = 0;
    
    // Gửi PM_CHAT_END (fire and forget - không chờ response)
    // Nếu chờ sẽ bị deadlock do race condition với receive thread
    snprintf(rid, sizeof(rid), "%d", (*next_id)++);
    snprintf(req, sizeof(req), "PM_CHAT_END %s token=%s", rid, token);
    send_line(sock, req);
    
    // Đợi receive thread thấy flag và tự thoát
    // Thread check mỗi 200ms trong select timeout
    struct timespec ts_sleep = {0, 300000000}; // 300ms
    nanosleep(&ts_sleep, NULL);
    
    // Đợi thread kết thúc hoàn toàn
    pthread_join(g_recv_thread, NULL);
    
    // Flush data còn sót trong socket (response của PM_CHAT_END)
    // Dùng non-blocking read để tránh bị treo
    struct timeval tv_flush;
    tv_flush.tv_sec = 0;
    tv_flush.tv_usec = 100000; // 100ms
    fd_set flush_fds;
    FD_ZERO(&flush_fds);
    FD_SET(sock, &flush_fds);
    if (select(sock + 1, &flush_fds, NULL, NULL, &tv_flush) > 0) {
        char flush_buf[4096];
        recv(sock, flush_buf, sizeof(flush_buf), 0);
    }
    
    g_chat_partner[0] = '\0';
    g_my_username[0] = '\0';
    g_chat_sock = -1;
    
    printf(C_INFO "\nChat ended. Returning to menu...\n" C_RESET);
}

// ============ Group Chat Mode Implementation ============
// Tương tự PM chat mode nhưng broadcast cho nhiều người

static volatile int g_group_chat_id = 0;       // Group ID đang chat
static char g_group_name[64] = {0};            // Tên group đang chat

static void *group_chat_recv_thread(void *arg)
{
    (void)arg;
    
    char buf[8192];
    int buf_len = 0;
    
    while (g_in_chat_mode && g_chat_sock > 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_chat_sock, &fds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        
        int ret = select(g_chat_sock + 1, &fds, NULL, NULL, &tv);
        
        if (!g_in_chat_mode) break;
        
        if (ret <= 0) continue;
        
        char tmp[4096];
        int n = recv(g_chat_sock, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) {
            g_in_chat_mode = 0;
            break;
        }
        
        tmp[n] = '\0';
        
        if (buf_len + n < (int)sizeof(buf) - 1) {
            memcpy(buf + buf_len, tmp, n + 1);
            buf_len += n;
        }
        
        char *line_start = buf;
        char *crlf;
        while ((crlf = strstr(line_start, "\r\n")) != NULL) {
            *crlf = '\0';
            
            // PUSH GM - tin nhắn group
            if (strncmp(line_start, "PUSH GM ", 8) == 0) {
                char *payload = line_start + 8;
                
                char from[64] = {0}, content[4096] = {0}, ts_str[32] = {0};
                
                kv_get(payload, "from", from, sizeof(from));
                kv_get(payload, "content", content, sizeof(content));
                kv_get(payload, "ts", ts_str, sizeof(ts_str));
                
                if (from[0] && content[0]) {
                    long ts = ts_str[0] ? atol(ts_str) : (long)time(NULL);
                    print_message(from, content, ts);
                }
            }
            // PUSH GM_JOIN - ai đó vào group chat
            else if (strncmp(line_start, "PUSH GM_JOIN ", 13) == 0) {
                char *payload = line_start + 13;
                char user[64] = {0};
                kv_get(payload, "user", user, sizeof(user));
                if (user[0]) {
                    printf(C_INFO "\n  >>> %s đã vào nhóm chat <<<\n" C_RESET, user);
                    printf("> ");
                    fflush(stdout);
                }
            }
            // PUSH GM_LEAVE - ai đó rời group chat
            else if (strncmp(line_start, "PUSH GM_LEAVE ", 14) == 0) {
                char *payload = line_start + 14;
                char user[64] = {0};
                kv_get(payload, "user", user, sizeof(user));
                if (user[0]) {
                    printf(C_WARN "\n  <<< %s đã rời nhóm chat >>>\n" C_RESET, user);
                    printf("> ");
                    fflush(stdout);
                }
            }
            // PUSH GM_KICKED - bị kick khỏi group
            else if (strncmp(line_start, "PUSH GM_KICKED ", 15) == 0) {
                printf(C_WARN "\n  !!! Bạn đã bị xóa khỏi nhóm. Thoát chat mode... !!!\n" C_RESET);
                fflush(stdout);
                g_in_chat_mode = 0;  // Thoát chat loop
            }
            
            line_start = crlf + 2;
        }
        
        if (line_start > buf) {
            int remaining = buf_len - (line_start - buf);
            if (remaining > 0) {
                memmove(buf, line_start, remaining);
            }
            buf_len = remaining;
            buf[buf_len] = '\0';
        }
    }
    
    return NULL;
}

void client_group_chat_mode(int sock, LineFramer *fr, const char *token, int *next_id, int group_id)
{
    char req[4096], resp[8192];
    char rid[32];
    
    // Gửi GM_CHAT_START
    snprintf(rid, sizeof(rid), "%d", (*next_id)++);
    snprintf(req, sizeof(req), "GM_CHAT_START %s token=%s group_id=%d", rid, token, group_id);
    send_line(sock, req);
    
    int r = framer_recv_line(sock, fr, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }
    
    char kind[32], rrid[32], rest[8192];
    parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
    
    if (strcmp(kind, "OK") != 0) {
        printf(C_WARN "Failed to start group chat: %s\n" C_RESET, rest);
        return;
    }
    
    // Parse response
    char history[8192] = {0};
    char group_name[64] = {0};
    char my_username[64] = {0};
    
    kv_get(rest, "history", history, sizeof(history));
    kv_get(rest, "group_name", group_name, sizeof(group_name));
    kv_get(rest, "me", my_username, sizeof(my_username));
    
    // Lưu state
    g_group_chat_id = group_id;
    snprintf(g_group_name, sizeof(g_group_name), "%s", group_name);
    snprintf(g_my_username, sizeof(g_my_username), "%s", my_username);
    g_chat_sock = sock;
    g_in_chat_mode = 1;
    
    // Hiển thị header
    printf("\n" C_TITLE "══════════════════════════════════\n");
    printf("      💬 Group: %s (ID: %d)\n", group_name, group_id);
    printf("══════════════════════════════════\n" C_RESET);
    printf("Type your message and press Enter to send.\n");
    printf("Type 'quit' or 'q' to exit chat.\n");
    printf(C_TITLE "──────────────────────────────────\n" C_RESET);
    
    // Hiển thị history
    display_chat_history(history, my_username);
    
    // Start receive thread
    if (pthread_create(&g_recv_thread, NULL, group_chat_recv_thread, NULL) != 0) {
        printf("Failed to create receive thread\n");
        g_in_chat_mode = 0;
        return;
    }
    
    // Main input loop
    while (g_in_chat_mode) {
        printf("> ");
        fflush(stdout);
        
        char input[2048];
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        trim_line(input);
        
        if (!input[0]) continue;
        
        // Check quit
        if (strcasecmp(input, "quit") == 0 || strcasecmp(input, "q") == 0) {
            g_in_chat_mode = 0;
            break;
        }
        
        // Encode message
        char content_b64[4096];
        if (base64_encode((unsigned char *)input, strlen(input), content_b64, sizeof(content_b64)) < 0) {
            printf(C_WARN "Message too long\n" C_RESET);
            continue;
        }
        
        // Gửi tin nhắn (fire-and-forget style)
        snprintf(rid, sizeof(rid), "%d", (*next_id)++);
        snprintf(req, sizeof(req), "GM_SEND %s token=%s group_id=%d content=%s",
                 rid, token, group_id, content_b64);
        send_line(sock, req);
        
        // Hiển thị message của mình ngay lập tức
        print_message(my_username, content_b64, (long)time(NULL));
        
        // Đọc response trong background (bỏ qua)
        // Receive thread sẽ xử lý các PUSH messages
    }
    
    // Gửi GM_CHAT_END (fire-and-forget)
    snprintf(rid, sizeof(rid), "%d", (*next_id)++);
    snprintf(req, sizeof(req), "GM_CHAT_END %s token=%s", rid, token);
    send_line(sock, req);
    
    // Đợi receive thread
    struct timespec ts_sleep = {0, 300000000};
    nanosleep(&ts_sleep, NULL);
    pthread_join(g_recv_thread, NULL);
    
    // Flush socket
    struct timeval tv_flush;
    tv_flush.tv_sec = 0;
    tv_flush.tv_usec = 100000;
    fd_set flush_fds;
    FD_ZERO(&flush_fds);
    FD_SET(sock, &flush_fds);
    if (select(sock + 1, &flush_fds, NULL, NULL, &tv_flush) > 0) {
        char flush_buf[4096];
        recv(sock, flush_buf, sizeof(flush_buf), 0);
    }
    
    g_group_chat_id = 0;
    g_group_name[0] = '\0';
    g_my_username[0] = '\0';
    g_chat_sock = -1;
    
    printf(C_INFO "\nGroup chat ended. Returning to group menu...\n" C_RESET);
}
