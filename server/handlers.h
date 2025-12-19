#ifndef HANDLERS_H
#define HANDLERS_H

/*
 * server/handlers.*
 * - Router xử lý request theo VERB (PING/REGISTER/LOGIN/LOGOUT/WHOAMI).
 * - Đây là nơi phù hợp để thêm verb mới cho các tính năng tiếp theo.
 */

typedef struct {
    int client_sock;
} ServerCtx;

// Xử lý 1 request line (không gồm \r\n).
int handle_request(ServerCtx* ctx, const char* line);

#endif
