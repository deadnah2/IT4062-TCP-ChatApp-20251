#ifndef HANDLERS_H
#define HANDLERS_H

typedef struct {
    int client_sock;
} ServerCtx;

int handle_request(ServerCtx* ctx, const char* line);

#endif
