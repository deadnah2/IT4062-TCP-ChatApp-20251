#ifndef FRAMING_H
#define FRAMING_H

#include <stddef.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} LineFramer;

int framer_init(LineFramer* framer, size_t initial_cap);
void framer_free(LineFramer* framer);

// Trả về: 1 nếu pop được 1 dòng, 0 nếu chưa đủ dữ liệu, -1 lỗi, -2 dòng quá dài.
int framer_pop_line(LineFramer* framer, char* out, size_t out_cap);

// Đọc từ socket đến khi có 1 dòng (kết thúc bằng \r\n) hoặc disconnect.
// Trả về: >0 độ dài dòng (không gồm \r\n), 0 nếu peer đóng, -1 lỗi, -2 dòng quá dài.
int framer_recv_line(int sock, LineFramer* framer, char* out, size_t out_cap);

#endif
