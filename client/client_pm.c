/*
 * client/client_pm.c
 * - Tin nhắn riêng (PM) 1-1 với real-time messaging.
 * - Nhận tin nhắn thời gian thực qua background thread.
 * - Hiển thị lịch sử tin nhắn (cũ nhất trước).
 * - Base64 encoding cho message content.
 * - Thread-safe output với mutex protection.
 *
 * Các verb protocol:
 *   PM_CONVERSATIONS - Liệt kê cuộc trò chuyện gần đây
 *   PM_CHAT_START    - Bắt đầu chat session
 *   PM_SEND          - Gửi tin nhắn
 *   PM_CHAT_END      - Kết thúc chat session
 *
 * PUSH notifications:
 *   PUSH PM    - Nhận tin nhắn mới
 *   PUSH JOIN  - Đối phương vào chat
 *   PUSH LEAVE - Đối phương rời chat
 */

#include "client.h"

// ============ Chat Mode Global State ============
// Các biến này được chia sẻ giữa thread chính và thread nhận

static volatile int g_in_chat_mode = 0;     // Cờ: đang trong chế độ chat
static volatile int g_chat_sock = -1;        // Socket để nhận
static char g_chat_partner[64] = {0};        // Tên đối phương đang chat
static char g_my_username[64] = {0};         // Tên của tôi
static pthread_t g_recv_thread;              // Handle thread nhận
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
 * chat_recv_thread
 * - Thread nền để nhận tin nhắn thời gian thực.
 * - Dùng select() với timeout để poll socket.
 * - Xử lý các thông báo PUSH từ server.
 *
 * Các loại PUSH:
 *   PUSH PM    - Hiển thị tin nhắn đến
 *   PUSH JOIN  - Thông báo đối phương vào
 *   PUSH LEAVE - Thông báo đối phương rời
 */
static void *chat_recv_thread(void *arg)
{
    (void)arg;  // Tham số không dùng

    // Buffer dòng cho tin chưa đầy đủ
    char buf[8192];
    int buf_len = 0;

    // Vòng lặp nhận chính
    while (g_in_chat_mode && g_chat_sock >= 0) {
        // Thiết lập select() với timeout 200ms
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;  // 200ms để phản hồi nhanh

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_chat_sock, &fds);

        int ready = select(g_chat_sock + 1, &fds, NULL, NULL, &tv);

        // Không có dữ liệu, tiếp tục poll
        if (ready <= 0)
            continue;

        // Nhận dữ liệu từ socket
        char tmp[1024];
        int n = recv(g_chat_sock, tmp, sizeof(tmp) - 1, 0);

        // Xử lý ngắt kết nối
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

        // Thêm vào buffer
        if (buf_len + n < (int)sizeof(buf) - 1) {
            memcpy(buf + buf_len, tmp, n);
            buf_len += n;
            buf[buf_len] = '\0';
        }

        // Xử lý các dòng hoàn chỉnh (kết thúc bằng \r\n)
        char *line_start = buf;
        char *crlf;
        while ((crlf = strstr(line_start, "\r\n")) != NULL) {
            *crlf = '\0';

            // Xử lý PUSH PM - Tin nhắn mới
            if (strncmp(line_start, "PUSH PM ", 8) == 0) {
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
            // Xử lý PUSH JOIN - Đối phương vào
            } else if (strncmp(line_start, "PUSH JOIN ", 10) == 0) {
                char *payload = line_start + 10;
                char user[64] = {0};
                kv_get(payload, "user", user, sizeof(user));
                if (user[0]) {
                    printf(C_INFO "\n  >>> %s đã vào cuộc trò chuyện <<<\n" C_RESET, user);
                    fflush(stdout);
                }
            // Xử lý PUSH LEAVE - Đối phương rời
            } else if (strncmp(line_start, "PUSH LEAVE ", 11) == 0) {
                char *payload = line_start + 11;
                char user[64] = {0};
                kv_get(payload, "user", user, sizeof(user));
                if (user[0]) {
                    printf(C_WARN "\n  <<< %s đã rời cuộc trò chuyện >>>\n" C_RESET, user);
                    fflush(stdout);
                }
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
 * - Parse và hiển thị lịch sử tin nhắn từ server.
 * - Định dạng từ server: id1:from1:content1_b64:ts1,id2:from2:content2_b64:ts2,...
 * - Lịch sử lưu mới nhất trước, hiển thị cũ nhất trước (theo thời gian).
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

    Msg msgs[100];  // Tối đa 100 tin trong lịch sử
    int count = 0;

    // Parse từng tin nhắn (định dạng: id:from:content:ts)
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

        // Trích xuất tên người gửi (trường thứ hai)
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

        // Trích xuất timestamp (trường cuối) và nội dung (giữa)
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

        // Highlight tin của tôi vs của người khác
        int is_me = (my_username[0] && strcmp(msgs[i].from, my_username) == 0);

        if (is_me) {
            printf(C_MSG_ME "[%s] [You]: %s\n" C_RESET, time_str, decoded);
        } else {
            printf(C_MSG_OTHER "[%s] [%s]: %s\n" C_RESET, time_str, msgs[i].from, decoded);
        }
    }
}

// ============ Main Chat Mode ============

/*
 * cmd_chat_mode
 * - Vào chế độ tin nhắn riêng.
 * - Luồng xử lý:
 *   1. Hiển thị cuộc trò chuyện gần đây với số chưa đọc
 *   2. Hỏi user muốn chat với ai
 *   3. Bắt đầu chat session (PM_CHAT_START)
 *   4. Hiển thị lịch sử tin nhắn
 *   5. Khởi động thread nhận
 *   6. Vòng lặp chính: đọc đầu vào, gửi tin
 *   7. Dọn dẹp khi thoát
 *
 * Lệnh thoát: quit, q, /quit, /q
 */
void cmd_chat_mode(ClientState *cs)
{
    // Hiển thị tiêu đề
    printf("\n" C_TITLE ICON_CHAT " Private Message\n");
    printf("══════════════════════════════════\n" C_RESET);

    // 1. Yêu cầu các cuộc trò chuyện gần đây
    char rid[32];
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);

    char req[8192];
    snprintf(req, sizeof(req), "PM_CONVERSATIONS %s token=%s", rid, cs->token);
    send_line(cs->sock, req);

    char resp[8192];
    int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }

    char kind[32], rrid[32], rest[8192];
    parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));

    // Hiển thị danh sách với chỉ báo chưa đọc
    if (strcmp(kind, "OK") == 0) {
        char convos[2048];
        if (kv_get(rest, "conversations", convos, sizeof(convos)) &&
            strcmp(convos, "empty") != 0 && convos[0]) {

            printf(C_INFO "\nRecent conversations:\n" C_RESET);

            char tmp[2048];
            snprintf(tmp, sizeof(tmp), "%s", convos);

            // Parse danh sách cuộc trò chuyện (định dạng: user1:unread1,...)
            char *saveptr;
            char *tok = strtok_r(tmp, ",", &saveptr);
            while (tok) {
                char username[64];
                int unread = 0;

                // Trích xuất tên và số chưa đọc
                char *colon = strchr(tok, ':');
                if (colon) {
                    *colon = '\0';
                    snprintf(username, sizeof(username), "%s", tok);
                    unread = atoi(colon + 1);
                } else {
                    snprintf(username, sizeof(username), "%s", tok);
                }

                // Hiển thị với chỉ báo chưa đọc nếu có
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

    // 2. Hỏi muốn chat với ai
    printf("\n" C_MENU "Enter username to chat with (or 'q' to cancel): " C_RESET);
    fflush(stdout);

    char partner[64];
    if (!fgets(partner, sizeof(partner), stdin))
        return;
    trim_line(partner);

    // Xử lý hủy
    if (partner[0] == '\0' || strcmp(partner, "q") == 0) {
        printf("Cancelled.\n");
        return;
    }

    // 3. Bắt đầu chat session với đối phương
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);
    snprintf(req, sizeof(req), "PM_CHAT_START %s token=%s with=%s", rid, cs->token, partner);
    send_line(cs->sock, req);

    r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }

    parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));

    // Xử lý phản hồi lỗi
    if (strcmp(kind, "OK") != 0) {
        printf(C_WARN "Error: %s\n" C_RESET, rest);
        return;
    }

    // Lưu tên đối phương và tên tôi
    snprintf(g_chat_partner, sizeof(g_chat_partner), "%s", partner);

    if (!kv_get(rest, "me", g_my_username, sizeof(g_my_username))) {
        snprintf(g_my_username, sizeof(g_my_username), "You");
    }

    // 4. Hiển thị header chat
    printf("\n");
    printf(C_TITLE "════════════════════════════════════════════\n");
    printf("       " ICON_CHAT " Chat with %s\n", partner);
    printf("════════════════════════════════════════════\n" C_RESET);
    printf(C_DIM "Type your message and press Enter to send.\n");
    printf("Type 'quit' or 'q' to exit chat.\n" C_RESET);
    printf(C_TITLE "────────────────────────────────────────────\n" C_RESET);

    // Hiển thị lịch sử tin nhắn
    char history[8192];
    if (kv_get(rest, "history", history, sizeof(history))) {
        display_chat_history(history, g_my_username);
    }

    printf(C_TITLE "────────────────────────────────────────────\n" C_RESET);

    // 5. Đặt state toàn cục và khởi động thread nhận
    g_in_chat_mode = 1;
    g_chat_sock = cs->sock;

    if (pthread_create(&g_recv_thread, NULL, chat_recv_thread, NULL) != 0) {
        printf(C_WARN "Failed to start receive thread\n" C_RESET);
        g_in_chat_mode = 0;
        return;
    }

    // 6. Vòng lặp nhập chat chính
    while (g_in_chat_mode) {
        char input[2048];

        // Đọc đầu vào người dùng
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        trim_line(input);

        // Bỏ qua đầu vào rỗng
        if (input[0] == '\0') {
            continue;
        }

        // Kiểm tra lệnh thoát
        if (strcmp(input, "quit") == 0 || strcmp(input, "q") == 0 ||
            strcmp(input, "/quit") == 0 || strcmp(input, "/q") == 0) {
            break;
        }

        // Mã hóa Base64 tin nhắn để truyền
        char content_b64[4096];
        if (base64_encode((unsigned char *)input, strlen(input),
                          content_b64, sizeof(content_b64)) < 0) {
            printf(C_WARN "Message too long\n" C_RESET);
            continue;
        }

        // Gửi tin nhắn đến server
        snprintf(rid, sizeof(rid), "%d", cs->next_id++);
        snprintf(req, sizeof(req), "PM_SEND %s token=%s to=%s content=%s",
                 rid, cs->token, partner, content_b64);
        send_line(cs->sock, req);

        // Hiển thị tin của mình ngay (optimistic UI)
        long ts = (long)time(NULL);
        print_message(g_my_username, content_b64, ts);
    }

    // 7. Dọn dẹp khi thoát
    g_in_chat_mode = 0;

    // Thông báo server rằng chat kết thúc
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);
    snprintf(req, sizeof(req), "PM_CHAT_END %s token=%s", rid, cs->token);
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
    g_chat_partner[0] = '\0';
    g_my_username[0] = '\0';
    g_chat_sock = -1;

    printf(C_INFO "\nChat ended. Returning to menu...\n" C_RESET);
}
