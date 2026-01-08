/**
 * client/client_auth.c
 * Authentication: Register, Login, Logout, Whoami
 * Xác thực: Đăng ký, Đăng nhập, Đăng xuất, Xem thông tin user
 * 
 * Các hàm:
 * - cmd_register(): Đăng ký tài khoản mới (username, password, email)
 * - cmd_login(): Đăng nhập và lưu token nếu thành công
 * - cmd_logout(): Đăng xuất và xóa token
 * - cmd_whoami(): Xem thông tin user từ token
 * - cmd_raw_send(): Gửi raw request để debug
 * - cmd_disconnect(): Ngắt kết nối sạch sẽ trước khi thoát
 */

#include "client.h"

/**
 * cmd_register
 * Đăng ký tài khoản mới
 * Request: REGISTER req_id username=... password=... email=...
 */
void cmd_register(ClientState *cs)
{
    char username[64], password[128], email[128];

    printf("Username: ");
    if (!fgets(username, sizeof(username), stdin))
        return;
    trim_line(username);

    printf("Password: ");
    if (!fgets(password, sizeof(password), stdin))
        return;
    trim_line(password);

    printf("Email: ");
    if (!fgets(email, sizeof(email), stdin))
        return;
    trim_line(email);

    char rid[32];
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);

    char req[2048];
    snprintf(req, sizeof(req), "REGISTER %s username=%s password=%s email=%s",
             rid, username, password, email);
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
}

/**
 * cmd_login
 * Đăng nhập vào hệ thống
 * Request: LOGIN req_id username=... password=...
 * Nếu thành công: Lưu token vào cs->token để dùng cho các request sau
 */
void cmd_login(ClientState *cs)
{
    char username[64], password[128];

    printf("Username: ");
    if (!fgets(username, sizeof(username), stdin))
        return;
    trim_line(username);

    printf("Password: ");
    if (!fgets(password, sizeof(password), stdin))
        return;
    trim_line(password);

    // Tạo request ID tự tăng
    char rid[32];
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);

    // Gửi LOGIN request
    char req[2048];
    snprintf(req, sizeof(req), "LOGIN %s username=%s password=%s",
             rid, username, password);
    send_line(cs->sock, req);

    // Đọc response
    char resp[4096];
    int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }

    char kind[32], rrid[32], rest[4096];
    parse_response(resp, kind, sizeof(kind), rrid, sizeof(rrid), rest, sizeof(rest));
    printf("< %s\n", resp);

    // Nếu login thành công, lưu token
    if (strcmp(kind, "OK") == 0) {
        char t[128];
        if (kv_get(rest, "token", t, sizeof(t))) {
            strncpy(cs->token, t, sizeof(cs->token) - 1);
            cs->token[sizeof(cs->token) - 1] = 0;
        }
    }
}

/**
 * cmd_logout
 * Đăng xuất khỏi hệ thống
 * Request: LOGOUT req_id token=...
 * Nếu thành công: Xóa token local
 */
void cmd_logout(ClientState *cs)
{
    // Kiểm tra đã login chưa
    if (!cs->token[0]) {
        printf("Not logged in.\n");
        return;
    }

    char rid[32];
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);

    char req[512];
    snprintf(req, sizeof(req), "LOGOUT %s token=%s", rid, cs->token);
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

    // Xóa token local nếu logout thành công
    if (strcmp(kind, "OK") == 0) {
        cs->token[0] = 0;
    }
}

/**
 * cmd_whoami
 * Xem thông tin user hiện tại từ token
 * Request: WHOAMI req_id token=...
 * Response: OK ... user_id=... username=...
 */
void cmd_whoami(ClientState *cs)
{
    if (!cs->token[0]) {
        printf("Not logged in.\n");
        return;
    }

    char rid[32];
    snprintf(rid, sizeof(rid), "%d", cs->next_id++);

    char req[512];
    snprintf(req, sizeof(req), "WHOAMI %s token=%s", rid, cs->token);
    send_line(cs->sock, req);

    char resp[4096];
    int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }

    printf("< %s\n", resp);
}

/**
 * cmd_raw_send
 * Gửi raw request để debug
 * Cho phép user nhập trực tiếp 1 dòng request theo protocol
 * Hữu ích để test các verb mới hoặc debug
 */
void cmd_raw_send(ClientState *cs)
{
    printf("Type raw request line: ");

    char req[2048];
    if (!fgets(req, sizeof(req), stdin))
        return;
    trim_line(req);

    if (!req[0])
        return;

    send_line(cs->sock, req);

    char resp[4096];
    int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
    if (r <= 0) {
        printf("Disconnected\n");
        return;
    }

    printf("< %s\n", resp);
}

/**
 * cmd_disconnect
 * Ngắt kết nối sạch sẽ trước khi thoát
 * Gửi DISCONNECT để server biết và dọn dẹp session
 */
void cmd_disconnect(ClientState *cs)
{
    // Chỉ gửi DISCONNECT nếu đang login
    if (cs->token[0]) {
        char rid[32];
        snprintf(rid, sizeof(rid), "%d", cs->next_id++);

        char req[256];
        snprintf(req, sizeof(req), "DISCONNECT %s token=%s", rid, cs->token);
        send_line(cs->sock, req);

        char resp[4096];
        int r = framer_recv_line(cs->sock, &cs->framer, resp, sizeof(resp));
        if (r > 0) {
            printf("< %s\n", resp);
        }
        printf(C_OK "Disconnected from server.\n" C_RESET);
    }
}
