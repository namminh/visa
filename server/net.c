/*
 * Minimal TCP server loop with thread-pool dispatch.
 *
 * EN:
 * - Create listening socket -> accept loop -> submit connection jobs to thread pool.
 * - If queue is full (backpressure), return a short JSON error then close.
 *
 * VN (Phỏng vấn):
 * - Một tiến trình accept duy nhất (đơn giản) + thread pool xử lý song song.
 * - Backpressure: giới hạn queue để bảo vệ hệ thống; khi đầy trả "server_busy".
 * - Có thể mở rộng: SO_REUSEPORT (nhiều acceptors), epoll, TLS, protocol framing.
 */
#include "net.h"
#include "config.h"
#include "threadpool.h"
#include "handler.h"
#include "db.h"
#include "metrics.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * Minimal network stub.
 *
 * In a complete implementation you would create a TCP socket, bind
 * it to cfg->listen_port, listen for connections, accept them, and
 * enqueue each client socket into the thread pool for handling. This
 * stub simply prints a message and returns immediately.
 */
int net_server_run(const Config *cfg, ThreadPool *pool, DBConnection *dbc) {
    (void)pool; // not used yet
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)cfg->listen_port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    fprintf(stderr, "Server listening on port %d\n", cfg->listen_port);

    // Accept loop: one TCP connection == one request
    for (;;) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int fd = accept(listen_fd, (struct sockaddr *)&cli, &clilen);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        // Create per-connection context for the handler
        HandlerContext *ctx = (HandlerContext *)malloc(sizeof(*ctx));
        if (!ctx) {
            perror("malloc");
            close(fd);
            continue;
        }
        ctx->client_fd = fd;
        ctx->db = dbc;

        // Submit to thread pool; if queue is full, send a friendly error and drop
            if (threadpool_submit(pool, handler_job, ctx) != 0) {
                const char *busy = "{\"status\":\"DECLINED\",\"reason\":\"server_busy\"}\n";
                (void)send(fd, busy, strlen(busy), 0);
                metrics_inc_server_busy();
                close(fd);
                free(ctx);
            }
    }

    close(listen_fd);
    return 0;
}
