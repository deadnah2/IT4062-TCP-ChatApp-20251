#include "framing.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

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

        if (framer->len > 64 * 1024) {
            return -2;
        }
    }
}
