/*
 * client/client_gm.c
 * - Tin nhắn nhóm (GM) với real-time messaging.
 * - Nhận tin nhắn thời gian thực qua background thread.
 * - Hiển thị lịch sử tin nhắn (cũ nhất trước).
 * - Base64 encoding cho message content (hỗ trợ Unicode).
 * - Thread-safe output với mutex protection.
 * - Thông báo vào/rời/bị đuổi cho thành viên nhóm.
 *
 * Các verb protocol:
 *   GM_CHAT_START - Bắt đầu phiên chat nhóm
 *   GM_SEND       - Gửi tin nhắn đến nhóm
 *   GM_CHAT_END   - Kết thúc phiên chat nhóm
 *
 * PUSH notifications:
 *   PUSH GM        - Tin nhắn nhóm mới
 *   PUSH GM_JOIN   - Thành viên vào chat
 *   PUSH GM_LEAVE  - Thành viên rời chat
 *   PUSH GM_KICKED - Bạn bị đuổi khỏi nhóm
 */

#include "client.h"

// ============ Group Chat Global State ============
// Các biến này được chia sẻ giữa thread chính và thread nhận

static volatile int g_in_chat_mode = 0;        // Cờ: đang trong chế độ chat
static volatile int g_chat_sock = -1;           // Socket để nhận
static volatile int g_group_chat_id = 0;        // ID nhóm hiện tại
static char g_group_name[64] = {0};             // Tên nhóm hiện tại
static char g_my_username[64] = {0};            // Tên của tôi
static pthread_t g_recv_thread;                 // Handle thread nhận
static pthread_mutex_t g_print_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex xuất

// ============ Helper Functions ============

/*
 * format_timestamp
 * - Chuyển đổi Unix timestamp sang định dạng HH:MM.
 */
static void format_timestamp(long ts, char *out, size_t cap)
{
    time_t t = (time_t)ts;
    struct tm *tm_info = localtime(&t);
    strftime(out, cap, "%H:%M", tm_info);
}

/*
 * print_message
 * - Hiển thị tin nhắn an toàn thread với định dạng.
 * - Giải mã Base64, format timestamp, màu theo người gửi.
 */
static void print_message(const char *from, const char *content_b64, long ts)
{
    // Giải mã nội dung Base64
    char content[2048];
    if (base64_decode(content_b64, (unsigned char *)content, sizeof(content)) < 0) {
        strcpy(content, "[decode error]");
    }

    // Format timestamp
    char time_str[32];
    format_timestamp(ts, time_str, sizeof(time_str));

    // In an toàn thread
    pthread_mutex_lock(&g_print_mutex);

    // Xác định tin nhắn có phải của tôi không
    int is_me = (g_my_username[0] && strcmp(from, g_my_username) == 0);

    if (is_me) {
        printf(C_MSG_ME "[%s] [You]: %s\n" C_RESET, time_str, content);
    } else {
        printf(C_MSG_OTHER "[%s] [%s]: %s\n" C_RESET, time_str, from, content);
    }
    fflush(stdout);

    pthread_mutex_unlock(&g_print_mutex);
}

// ============ Receive Thread ============

/*
 * group_chat_recv_thread
 * - Thread nền để nhận tin nhắn nhóm.
 * - Dùng select() với timeout để poll socket.
 * - Xử lý các thông báo PUSH từ server.
 *
 * Các loại PUSH:
 *   PUSH GM        - Hiển thị tin nhắn nhóm đến
 *   PUSH GM_JOIN   - Thông báo thành viên vào
 *   PUSH GM_LEAVE  - Thông báo thành viên rời
 *   PUSH GM_KICKED - Bạn bị xóa khỏi nhóm
 */
static void *group_chat_recv_thread(void *arg)
{
    (void)arg;  // Tham số không dùng

    // Buffer dòng cho tin chưa đầy đủ
    char buf[8192];
    int buf_len = 0;

    // Vòng lặp nhận chính
    while (g_in_chat_mode && g_chat_sock > 0) {
        // Thiết lập select() cho socket
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_chat_sock, &fds);

        // 200ms timeout để phản hồi nhanh
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int ret = select(g_chat_sock + 1, &fds, NULL, NULL, &tv);

        // Kiểm tra xem chế độ chat đã kết thúc chưa
        if (!g_in_chat_mode)
            break;

        // Không có dữ liệu, tiếp tục poll
        if (ret <= 0)
            continue;

        // Nhận dữ liệu từ socket
        char tmp[4096];
        int n = recv(g_chat_sock, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) {
            g_in_chat_mode = 0;
            break;
        }

        tmp[n] = '\0';

        // Thêm vào buffer
        if (buf_len + n < (int)sizeof(buf) - 1) {
            memcpy(buf + buf_len, tmp, n + 1);
            buf_len += n;
        }

        // Xử lý các dòng hoàn chỉnh (kết thúc bằng \r\n)
        char *line_start = buf;
        char *crlf;
        while ((crlf = strstr(line_start, "\r\n")) != NULL) {
            *crlf = '\0';

            // Xử lý PUSH GM - Tin nhắn nhóm mới
            if (strncmp(line_start, "PUSH GM ", 8) == 0) {
                char *payload = line_start + 8;

                // Trích xuất các trường tin nhắn
                char from[64] = {0}, content[4096] = {0}, ts_str[32] = {0};

                kv_get(payload, "from", from, sizeof(from));
                kv_get(payload, "content", content, sizeof(content));
                kv_get(payload, "ts", ts_str, sizeof(ts_str));

                // Hiển thị tin nhắn nếu hợp lệ
                if (from[0] && content[0]) {
                    long ts = ts_str[0] ? atol(ts_str) : (long)time(NULL);
                    print_message(from, content, ts);
                }
            // Xử lý PUSH GM_JOIN - Thành viên vào
            } else if (strncmp(line_start, "PUSH GM_JOIN ", 13) == 0) {
                char *payload = line_start + 13;
                char user[64] = {0};
                kv_get(payload, "user", user, sizeof(user));
                if (user[0]) {
                    printf(C_INFO "\n  >>> %s đã vào nhóm chat <<<\n" C_RESET, user);
                    fflush(stdout);
                }
            // Xử lý PUSH GM_LEAVE - Thành viên rời
            } else if (strncmp(line_start, "PUSH GM_LEAVE ", 14) == 0) {
                char *payload = line_start + 14;
                char user[64] = {0};
                kv_get(payload, "user", user, sizeof(user));
                if (user[0]) {
                    printf(C_WARN "\n  <<< %s đã rời nhóm chat >>>\n" C_RESET, user);
                    fflush(stdout);
                }
            // Xử lý PUSH GM_KICKED - Bạn bị xóa
            } else if (strncmp(line_start, "PUSH GM_KICKED ", 15) == 0) {
                printf(C_WARN "\n  !!! Bạn đã bị xóa khỏi nhóm. Thoát chat mode... !!!\n" C_RESET);
                fflush(stdout);
                g_in_chat_mode = 0;  // Buộc thoát chế độ chat
            }

            // Chuyển đến dòng tiếp theo
            line_start = crlf + 2;
        }

        // Xử lý dòng chưa đầy đủ trong buffer
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

// ============ Display History ============

/*
 * display_chat_history
 * - Parse và hiển thị lịch sử tin nhắn nhóm.
 * - Định dạng từ server: id1:from1:content1_b64:ts1,id2:from2:content2_b64:ts2,...
 * - Tin nhắn lưu mới nhất trước, hiển thị cũ nhất trước (theo thời gian).
 */
static void display_chat_history(const char *history, const char *my_username)
{
    // Xử lý lịch sử rỗng
    if (!history || strcmp(history, "empty") == 0 || !history[0]) {
        printf(C_DIM "  (No messages yet. Start the conversation!)\n" C_RESET);
        return;
    }

    char tmp[8192];
    snprintf(tmp, sizeof(tmp), "%s", history);

    // Cấu trúc lưu tin đã parse
    typedef struct {
        char from[64];      // Tên người gửi
        char content[2048]; // Nội dung mã hóa Base64
        long ts;            // Thời gian Unix
    } Msg;

    Msg msgs[100];  // Tối đa 100 tin
    int count = 0;

    // Parse từng tin nhắn
    char *saveptr;
    char *msg_tok = strtok_r(tmp, ",", &saveptr);
    while (msg_tok && count < 100) {
        char *p = msg_tok;
        
        // Bỏ qua ID tin nhắn (trường đầu tiên)
        char *colon1 = strchr(p, ':');
        if (!colon1) {
            msg_tok = strtok_r(NULL, ",", &saveptr);
            continue;
        }

        p = colon1 + 1;

        // Trích xuất tên người gửi
        char *colon2 = strchr(p, ':');
        if (!colon2) {
            msg_tok = strtok_r(NULL, ",", &saveptr);
            continue;
        }

        size_t from_len = colon2 - p;
        if (from_len >= 64)
            from_len = 63;
        memcpy(msgs[count].from, p, from_len);
        msgs[count].from[from_len] = '\0';

        p = colon2 + 1;

        // Trích xuất timestamp và nội dung
        char *last_colon = strrchr(p, ':');
        if (!last_colon) {
            msg_tok = strtok_r(NULL, ",", &saveptr);
            continue;
        }

        msgs[count].ts = atol(last_colon + 1);

        // Trích xuất nội dung Base64
        size_t content_len = last_colon - p;
        if (content_len >= 2048)
            content_len = 2047;
        memcpy(msgs[count].content, p, content_len);
        msgs[count].content[content_len] = '\0';

        count++;
        msg_tok = strtok_r(NULL, ",", &saveptr);
    }

    // Hiển thị tin nhắn theo thứ tự thời gian (cũ nhất trước)
    for (int i = count - 1; i >= 0; i--) {
        // Giải mã nội dung Base64
        char decoded[2048];
        if (base64_decode(msgs[i].content, (unsigned char *)decoded, sizeof(decoded)) < 0) {
            strcpy(decoded, "[decode error]");
        }

        // Format timestamp
        char time_str[32];
        format_timestamp(msgs[i].ts, time_str, sizeof(time_str));

        // Highlight tin của tôi
        int is_me = (my_username[0] && strcmp(msgs[i].from, my_username) == 0);

        if (is_me) {
            printf(C_MSG_ME "[%s] [You]: %s\n" C_RESET, time_str, decoded);
        } else {
            printf(C_MSG_OTHER "[%s] [%s]: %s\n" C_RESET, time_str, msgs[i].from, decoded);
        }
    }
}

// ============ Main Group Chat Mode ============

/*
 * cmd_group_chat_mode
 * - Vào chế độ chat nhóm cho một nhóm cụ thể.
 * - Luồng xử lý:
 *   1. Gửi yêu cầu GM_CHAT_START
 *   2. Parse response (history, group_name, me)
 *   3. Hiển thị header và lịch sử
 *   4. Khởi động thread nhận
 *   5. Vòng lặp chính: đọc đầu vào, gửi tin
 *   6. Dọn dẹp khi thoát
 *
 * Lệnh thoát: quit, q (không phân biệt hoa thường)
 */
void cmd_group_chat_mode(ClientState *cs, int group_id)
{
    char req[8192], resp[8192];
    char rid[32];

    // 1. Gửi GM_CHAT_START để vào chat nhóm
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);
    snprintf(req, sizeof(req), "GM_CHAT_START %s token=%s group_id=%d",
             rid, cs->token, group_id);
    send_line(cs->sock, req);

    // Nhận phản hồi
    int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }

    // Parse phản hồi
    char kind[32], rrid[32], rest[8192];
    parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));

    // Xử lý lỗi
    if (strcmp(kind, "OK") != 0) {
        printf(C_WARN "Failed to start group chat: %s\n" C_RESET, rest);
        return;
    }

    // 2. Trích xuất dữ liệu phản hồi
    char history[8192] = {0};     // Lịch sử tin nhắn
    char group_name[64] = {0};    // Tên nhóm
    char my_username[64] = {0};   // Tên của tôi

    kv_get(rest, "history", history, sizeof(history));
    kv_get(rest, "group_name", group_name, sizeof(group_name));
    kv_get(rest, "me", my_username, sizeof(my_username));

    // Lưu state toàn cục cho thread nhận
    g_group_chat_id = group_id;
    snprintf(g_group_name, sizeof(g_group_name), "%s", group_name);
    snprintf(g_my_username, sizeof(g_my_username), "%s", my_username);
    g_chat_sock = cs->sock;
    g_in_chat_mode = 1;

    // 3. Hiển thị header chat
    printf("\n" C_TITLE "══════════════════════════════════\n");
    printf("      💬 Group: %s (ID: %d)\n", group_name, group_id);
    printf("══════════════════════════════════\n" C_RESET);
    printf("Type your message and press Enter to send.\n");
    printf("Type 'quit' or 'q' to exit chat.\n");
    printf(C_TITLE "──────────────────────────────────\n" C_RESET);

    // Hiển thị lịch sử tin nhắn
    display_chat_history(history, my_username);

    // 4. Khởi động thread nhận tin thời gian thực
    if (pthread_create(&g_recv_thread, NULL, group_chat_recv_thread, NULL) != 0) {
        printf("Failed to create receive thread\n");
        g_in_chat_mode = 0;
        return;
    }

    // 5. Vòng lặp nhập chính
    while (g_in_chat_mode) {

        // Đọc đầu vào người dùng
        char input[2048];
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        trim_line(input);

        // Bỏ qua đầu vào rỗng
        if (!input[0])
            continue;

        // Kiểm tra lệnh thoát (không phân biệt hoa thường)
        if (strcasecmp(input, "quit") == 0 || strcasecmp(input, "q") == 0) {
            g_in_chat_mode = 0;
            break;
        }

        // Mã hóa Base64 tin nhắn để truyền
        char content_b64[4096];
        if (base64_encode((unsigned char *)input, strlen(input),
                          content_b64, sizeof(content_b64)) < 0) {
            printf(C_WARN "Message too long\n" C_RESET);
            continue;
        }

        // Gửi tin nhắn đến nhóm
        snprintf(rid, sizeof(rid), "%d", cs->next_id++);
        snprintf(req, sizeof(req), "GM_SEND %s token=%s group_id=%d content=%s",
                 rid, cs->token, group_id, content_b64);
        send_line(cs->sock, req);

        // Hiển thị tin của mình ngay (optimistic UI)
        print_message(my_username, content_b64, (long)time(NULL));
    }

    // 6. Dọn dẹp khi thoát

    // Thông báo server rằng chat nhóm kết thúc
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);
    snprintf(req, sizeof(req), "GM_CHAT_END %s token=%s", rid, cs->token);
    send_line(cs->sock, req);

    // Chờ thread nhận kết thúc
    struct timespec ts_sleep = {0, 300000000};  // 300ms chờ
    nanosleep(&ts_sleep, NULL);
    pthread_join(g_recv_thread, NULL);

    // Xả dữ liệu còn lại trong socket
    struct timeval tv_flush;
    tv_flush.tv_sec = 0;
    tv_flush.tv_usec = 100000;  // 100ms timeout
    fd_set flush_fds;
    FD_ZERO(&flush_fds);
    FD_SET(cs->sock, &flush_fds);
    if (select(cs->sock + 1, &flush_fds, NULL, NULL, &tv_flush) > 0) {
        char flush_buf[4096];
        recv(cs->sock, flush_buf, sizeof(flush_buf), 0);
    }

    // Đặt lại state toàn cục
    g_group_chat_id = 0;
    g_group_name[0] = '\0';
    g_my_username[0] = '\0';
    g_chat_sock = -1;

    printf(C_INFO "\nGroup chat ended. Returning to group menu...\n" C_RESET);
}
