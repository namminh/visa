/* [ANCHOR:NET_OVERVIEW]
 * Network Accept Loop — Visual Overview / Sơ đồ tổng quan (EN + VN)
 *
 *  socket/bind/listen(backlog=128)  →  accept(fd)
 *                                        |
 *                                        v
 *                         build HandlerContext (fd, db)
 *                                        |
 *                                        v
 *                submit to threadpool (handler_job)
 *                      |                     |
 *        queue full → fast-fail    queue ok → worker handles I/O
 *             send {"server_busy"}           (newline framing, timeouts)
 *             close(fd)
 *
 * EN:
 * - Backpressure is enforced by the bounded queue in threadpool. When full,
 *   the server fails fast with a short JSON and closes the connection.
 * - The kernel listen backlog absorbs short bursts of TCP connect storms.
 * - Extensible: SO_REUSEPORT (multi-acceptors), TCP_NODELAY, epoll, TLS.
 *
 * VN:
 * - Chống quá tải nằm ở hàng đợi giới hạn của threadpool. Khi đầy, trả phản hồi
 *   JSON ngắn "server_busy" rồi đóng kết nối (fail fast) thay vì để chờ lâu.
 * - Backlog (listen) giúp hấp thụ các đợt kết nối dồn dập ở mức kernel.
 * - Dễ mở rộng: SO_REUSEPORT (nhiều tiến trình accept), TCP_NODELAY, epoll, TLS.
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
// Optional: enable TCP_NODELAY after accept to reduce latency of small writes
// #define ENABLE_TCP_NODELAY 1

int net_server_run(const Config *cfg, ThreadPool *pool, DBConnection *dbc) {
    (void)pool; // not used yet
    // [ANCHOR:NET_SOCKET_SETUP] Tạo socket, cấu hình REUSEADDR (và gợi ý REUSEPORT)
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    // EN: Allow quick restart after close
    // VN: Cho phép khởi động lại nhanh sau khi đóng socket
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // EN: Optional — distribute accept load across multiple processes
    // VN: Tuỳ chọn — chia tải accept ở mức kernel cho nhiều tiến trình
    // setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

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

    // [ANCHOR:NET_ACCEPT_LOOP] Vòng accept: mỗi kết nối TCP tương ứng một request
    for (;;) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int fd = accept(listen_fd, (struct sockaddr *)&cli, &clilen);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

#ifdef ENABLE_TCP_NODELAY
        // [ANCHOR:NET_TCP_NODELAY_HINT]
        // EN: Reduce latency for small responses on this connection
        // VN: Giảm độ trễ cho gói phản hồi nhỏ trên kết nối này
        int one = 1;
        (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif

        // Create per-connection context for the handler
        HandlerContext *ctx = (HandlerContext *)malloc(sizeof(*ctx));
        if (!ctx) {
            perror("malloc");
            close(fd);
            continue;
        }
        ctx->client_fd = fd;
        ctx->db = dbc;

        // EN: Submit to thread pool; if queue is full, send an error and drop
        // VN: Đẩy vào threadpool; nếu hàng đợi đầy, trả "server_busy" rồi đóng
            if (threadpool_submit(pool, handler_job, ctx) != 0) {
                // [ANCHOR:NET_FAST_FAIL_BUSY] Fast-fail khi backpressure (queue đầy)
                const char *busy = "{\"status\":\"DECLINED\",\"reason\":\"server_busy\"}\n";
                (void)send(fd, busy, strlen(busy), 0);
                metrics_inc_server_busy();
                close(fd);
                free(ctx);
            }
    }

    // [ANCHOR:NET_CLOSE_LISTENER] Đóng socket lắng nghe khi thoát vòng accept
    close(listen_fd);
    return 0;
}
