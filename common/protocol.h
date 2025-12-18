#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

typedef struct {
    char verb[32];
    char req_id[32];
    char* payload; // heap (nullable)
} ProtoMsg;

int proto_parse_line(const char* line, ProtoMsg* out);
void proto_free(ProtoMsg* msg);

int proto_send_ok(int sock, const char* req_id, const char* payload);
int proto_send_err(int sock, const char* req_id, int code, const char* message);

#endif
