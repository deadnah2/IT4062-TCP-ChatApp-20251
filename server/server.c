#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../common/framing.h"
#include "handlers.h"
#include "accounts.h"
#include "sessions.h"

typedef struct {
    int sock;
    struct sockaddr_in addr;
} ClientArgs;

static void* client_thread(void* arg)
{
    ClientArgs* a = (ClientArgs*)arg;
    int c = a->sock;

    LineFramer fr;
    framer_init(&fr, 2048);

    ServerCtx ctx;
    ctx.client_sock = c;

    char line[4096];
    for (;;) {
        int r = framer_recv_line(c, &fr, line, sizeof(line));
        if (r == 0) break;
        if (r < 0) break;
        handle_request(&ctx, line);
    }

    // Auto-destroy session when socket disconnects
    sessions_remove_by_socket(c);

    framer_free(&fr);
    close(c);
    free(a);
    return NULL;
}

static int listen_on(unsigned short port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(s);
        return -1;
    }

    if (listen(s, 64) != 0) {
        close(s);
        return -1;
    }

    return s;
}

int main(int argc, char** argv)
{
    // Ensure logs show up even when stdout is piped (e.g., test runner)
    setvbuf(stdout, NULL, _IOLBF, 0);

    unsigned short port = 8888;
    int session_timeout_seconds = 3600;

    if (argc >= 2) port = (unsigned short)atoi(argv[1]);
    if (argc >= 3) session_timeout_seconds = atoi(argv[2]);

    if (accounts_init("data/users.db") != 0) {
        printf("Failed to init accounts DB\n");
        return 1;
    }

    sessions_init(session_timeout_seconds);

    int s = listen_on(port);
    if (s < 0) {
        printf("Failed to listen on port %d\n", (int)port);
        return 1;
    }

    printf("Server listening on 0.0.0.0:%d (session_timeout=%ds)\n", (int)port, session_timeout_seconds);

    for (;;) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int c = accept(s, (struct sockaddr*)&caddr, &clen);
        if (c < 0) continue;

        ClientArgs* args = (ClientArgs*)calloc(1, sizeof(ClientArgs));
        args->sock = c;
        memcpy(&args->addr, &caddr, sizeof(caddr));

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, args);
        pthread_detach(tid);
    }

    close(s);
    return 0;
}
