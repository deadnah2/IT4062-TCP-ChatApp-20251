#include "protocol.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

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

static void trim_left(const char** p)
{
    while (**p == ' ') (*p)++;
}

int proto_parse_line(const char* line, ProtoMsg* out)
{
    memset(out, 0, sizeof(*out));

    const char* p = line;
    trim_left(&p);

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

int proto_send_err(int sock, const char* req_id, int code, const char* message)
{
    char line[512];
    snprintf(line, sizeof(line), "ERR %s %d %s\r\n", req_id, code, message ? message : "");
    return send_all(sock, line, strlen(line));
}
