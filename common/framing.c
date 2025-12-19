#include "framing.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/*
 * common/framing.c
 * - Nhận dữ liệu TCP vào buffer nội bộ và tách theo delimiter "\r\n".
 * - Cho phép recv() trả về mảnh/ghép (stream) nhưng app vẫn xử lý theo từng dòng.
 * - Nếu không gặp "\r\n" mà buffer vượt ~64KB => coi là dòng quá dài.
 */

static int ensure_capacity(LineFramer* framer, size_t need)
{
    if (need <= framer->cap) return 0;

    size_t new_cap = framer->cap ? framer->cap : 256;
    while (new_cap < need) new_cap *= 2;

    char* next = (char*)realloc(framer->data, new_cap);
    if (!next) return -1;

    framer->data = next;
    framer->cap = new_cap;
    return 0;
}

/*
 * framer_init
 * - Khởi tạo bộ đệm nội bộ cho LineFramer.
 * - initial_cap: dung lượng khởi tạo (0 => dùng mặc định 1024).
 * Return: 0 nếu OK, -1 nếu lỗi cấp phát.
 */
int framer_init(LineFramer* framer, size_t initial_cap)
{
    memset(framer, 0, sizeof(*framer));
    if (initial_cap == 0) initial_cap = 1024;
    framer->data = (char*)malloc(initial_cap);
    if (!framer->data) return -1;
    framer->cap = initial_cap;
    framer->len = 0;
    return 0;
}

/*
 * framer_free
 * - Giải phóng bộ đệm của framer.
 */
void framer_free(LineFramer* framer)
{
    if (!framer) return;
    free(framer->data);
    framer->data = NULL;
    framer->len = 0;
    framer->cap = 0;
}

static char* find_crlf(LineFramer* framer)
{
    if (framer->len < 2) return NULL;
    for (size_t i = 0; i + 1 < framer->len; i++) {
        if (framer->data[i] == '\r' && framer->data[i + 1] == '\n') {
            return framer->data + i;
        }
    }
    return NULL;
}

/*
 * framer_pop_line
 * - Tìm "\r\n" trong buffer, nếu có thì copy ra `out` và remove khỏi buffer.
 * Return:
 *   1  : đã pop được 1 line
 *   0  : chưa đủ dữ liệu (chưa thấy "\r\n")
 *  -2  : out_cap không đủ (line quá dài so với buffer output)
 */
int framer_pop_line(LineFramer* framer, char* out, size_t out_cap)
{
    char* crlf = find_crlf(framer);
    if (!crlf) return 0;

    size_t line_len = (size_t)(crlf - framer->data);
    if (line_len + 1 > out_cap) return -2;

    memcpy(out, framer->data, line_len);
    out[line_len] = 0;

    size_t remain = framer->len - (line_len + 2);
    memmove(framer->data, crlf + 2, remain);
    framer->len = remain;

    return 1;
}

/*
 * framer_recv_line
 * - Đọc từ socket và append vào buffer đến khi pop được 1 line (kết thúc bằng "\r\n").
 * Return:
 *  >0  : độ dài line (không gồm "\r\n")
 *   0  : peer đóng kết nối
 *  -1  : lỗi recv / lỗi cấp phát
 *  -2  : line quá dài (~64KB) hoặc out_cap không đủ
 */
int framer_recv_line(int sock, LineFramer* framer, char* out, size_t out_cap)
{
    for (;;) {
        int popped = framer_pop_line(framer, out, out_cap);
        if (popped == 1) return (int)strlen(out);
        if (popped < 0) return popped;

        char tmp[512];
        int r = (int)recv(sock, tmp, (int)sizeof(tmp), 0);
        if (r == 0) return 0;
        if (r < 0) return -1;

        if (ensure_capacity(framer, framer->len + (size_t)r + 1) != 0) return -1;
        memcpy(framer->data + framer->len, tmp, (size_t)r);
        framer->len += (size_t)r;
        framer->data[framer->len] = 0;

        // Guard: tránh trường hợp client gửi 1 line không có delimiter.
        if (framer->len > 64 * 1024) {
            return -2;
        }
    }
}
