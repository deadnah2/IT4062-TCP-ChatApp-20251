#include "protocol.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

/*
 * common/protocol.c
 * - Parse 1 line thành (verb, req_id, payload).
 * - Gửi response theo format: OK <rid> ... / ERR <rid> <code> <message>.
 * - Không parse sâu payload (kv) để giữ layer protocol đơn giản.
 */

/*
 * send_all
 * - send(2) có thể gửi được "một phần" buffer (short write).
 * - Hàm này lặp đến khi gửi hết `len` bytes hoặc gặp lỗi/đóng kết nối.
 * Return: 0 nếu OK, -1 nếu lỗi.
 */
static int send_all(int sock, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = (int)send(sock, data + sent, (int)(len - sent), 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

// Bỏ khoảng trắng ở đầu chuỗi (phục vụ parse token).
static void trim_left(const char** p)
{
    while (**p == ' ') (*p)++;
}

/*
 * proto_parse_line
 * - Tách line thành 3 phần: VERB, REQ_ID, và payload (phần còn lại sau REQ_ID).
 * - Payload (nếu có) sẽ được cấp phát heap để caller quản lý bằng proto_free().
 *
 * Lưu ý:
 * - Hàm chỉ dựa trên khoảng trắng để tách token; không hỗ trợ quoted string.
 * - Nếu VERB/REQ_ID rỗng => trả lỗi.
 */
int proto_parse_line(const char* line, ProtoMsg* out)
{
    memset(out, 0, sizeof(*out));

    const char* p = line;
    trim_left(&p);

    // Parse theo token cách nhau bởi dấu cách: VERB, REQ_ID, (payload...).
    // VERB
    size_t i = 0;
    while (*p && *p != ' ' && i + 1 < sizeof(out->verb)) {
        out->verb[i++] = *p++;
    }
    out->verb[i] = 0;
    if (out->verb[0] == 0) return -1;

    trim_left(&p);

    // REQ_ID
    i = 0;
    while (*p && *p != ' ' && i + 1 < sizeof(out->req_id)) {
        out->req_id[i++] = *p++;
    }
    out->req_id[i] = 0;
    if (out->req_id[0] == 0) return -1;

    trim_left(&p);

    // PAYLOAD (optional)
    if (*p) {
        out->payload = (char*)malloc(strlen(p) + 1);
        if (!out->payload) return -1;
        strcpy(out->payload, p);
    }

    return 0;
}

void proto_free(ProtoMsg* msg)
{
    if (!msg) return;
    free(msg->payload);
    msg->payload = NULL;
}

/*
 * proto_send_ok
 * - Gửi response OK theo format: "OK <req_id> <payload>\r\n".
 * - Nếu payload NULL/"" thì chỉ gửi "OK <req_id>\r\n".
 */
int proto_send_ok(int sock, const char* req_id, const char* payload)
{
    char header[128];
    if (payload && payload[0]) {
        snprintf(header, sizeof(header), "OK %s ", req_id);
    } else {
        snprintf(header, sizeof(header), "OK %s", req_id);
    }

    if (send_all(sock, header, strlen(header)) != 0) return -1;
    if (payload && payload[0]) {
        if (send_all(sock, payload, strlen(payload)) != 0) return -1;
    }
    if (send_all(sock, "\r\n", 2) != 0) return -1;
    return 0;
}

/*
 * proto_send_err
 * - Gửi response ERR theo format: "ERR <req_id> <code> <message>\r\n".
 * - message có thể NULL.
 */
int proto_send_err(int sock, const char* req_id, int code, const char* message)
{
    char line[512];
    snprintf(line, sizeof(line), "ERR %s %d %s\r\n", req_id, code, message ? message : "");
    return send_all(sock, line, strlen(line));
}
