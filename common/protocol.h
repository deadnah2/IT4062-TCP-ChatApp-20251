#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

/*
 * common/protocol.*
 * Lớp "protocol" xử lý phần cú pháp tối thiểu của 1 dòng request/response:
 *   Request : <VERB> <REQ_ID> <payload...>\r\n
 *   Response: OK  <REQ_ID> <payload...>\r\n
 *             ERR <REQ_ID> <code> <message>\r\n
 *
 * Ghi chú:
 * - Payload hiện tại được giữ nguyên như chuỗi thô (ví dụ: "username=a password=b").
 * - Việc parse key/value được làm ở layer `server/handlers.*` để protocol luôn đơn giản.
 */

typedef struct {
    // VERB: REGISTER/LOGIN/... (không có dấu cách)
    char verb[32];
    // REQ_ID: client tự sinh để match response (không có dấu cách)
    char req_id[32];
    // Phần còn lại của line sau REQ_ID (nullable), cấp phát heap
    char* payload; // heap (nullable)
} ProtoMsg;

// Parse 1 line đã được framing (không gồm \r\n).
int proto_parse_line(const char* line, ProtoMsg* out);
void proto_free(ProtoMsg* msg);

// Helper gửi response theo format OK/ERR.
int proto_send_ok(int sock, const char* req_id, const char* payload);
int proto_send_err(int sock, const char* req_id, int code, const char* message);

#endif
