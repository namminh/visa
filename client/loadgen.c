/*
 * Simple multi-threaded load generator used for experiments:
 * - Spawns <connections> worker threads.
 * - Each worker opens a TCP connection per request, sends one JSON, reads a tiny response.
 * - Measures per-request latency (connect + send + recv) and prints summary stats (RPS, p50/p95/p99).
 *
 * VN (Phỏng vấn):
 * - Công cụ bắn tải đơn giản để đo tổng quan: bao nhiêu yêu cầu/giây, độ trễ p50/p95/p99.
 * - Mỗi request dùng 1 kết nối cho dễ minh hoạ; có thể tối ưu tái sử dụng kết nối/keep-alive.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>

// Open one TCP connection to localhost:port (mở một kết nối TCP tới localhost)
static int connect_once(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

// Per-worker parameters and latency buffer (tham số và bộ đệm độ trễ theo worker)
typedef struct {
    int port;
    int reqs;
    volatile unsigned long *ok;
    volatile unsigned long *err;
    struct { uint64_t *a; size_t n, cap; } lat;
} worker_arg;

// Worker: send <reqs> requests sequentially; record latency for each
// VN: Mỗi vòng lặp mở kết nối → gửi JSON → đọc phản hồi → đo thời gian.
static void *worker_main(void *p) {
    worker_arg *w = (worker_arg *)p;
    const char *payload = "{\"pan\":\"4111111111111111\",\"amount\":\"10.00\"}\n";
    char resp[256];
    for (int i = 0; i < w->reqs; ++i) {
        int fd = connect_once(w->port);
        if (fd < 0) { __sync_fetch_and_add(w->err, 1); continue; }
        struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
        ssize_t n = send(fd, payload, strlen(payload), 0);
        if (n < 0) { close(fd); __sync_fetch_and_add(w->err, 1); continue; }
        (void)recv(fd, resp, sizeof(resp), 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        close(fd);
        __sync_fetch_and_add(w->ok, 1);
        uint64_t us = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000ULL + (uint64_t)((t1.tv_nsec - t0.tv_nsec) / 1000);
        // push latency
        if (w->lat.n == w->lat.cap) {
            size_t nc = w->lat.cap ? w->lat.cap * 2 : 1024;
            uint64_t *na = (uint64_t *)realloc(w->lat.a, nc * sizeof(uint64_t));
            if (na) { w->lat.a = na; w->lat.cap = nc; }
        }
        if (w->lat.n < w->lat.cap) w->lat.a[w->lat.n++] = us;
    }
    return NULL;
}

/*
 * Simple load generator skeleton.
 *
 * Usage: ./loadgen <connections> <requests_per_connection> <port>
 *
 * This program is intended to create the specified number of concurrent
 * connections to localhost on the given port and send a fixed number of
 * transaction requests per connection. Each request is a simple JSON
 * string (e.g. {"pan":"4111111111111111","amount":"10.00"}). Responses
 * are ignored.
 */
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <connections> <requests_per_conn> <port>\n", argv[0]);
        return 1;
    }
    int conns = atoi(argv[1]);
    int reqs = atoi(argv[2]);
    int port = atoi(argv[3]);
    if (conns <= 0 || reqs <= 0 || port <= 0) {
        fprintf(stderr, "Invalid arguments\n");
        return 1;
    }
    fprintf(stderr, "loadgen: %d workers x %d reqs, port %d\n", conns, reqs, port);
    pthread_t *ths = calloc((size_t)conns, sizeof(pthread_t));
    worker_arg *args = calloc((size_t)conns, sizeof(worker_arg));
    volatile unsigned long ok = 0, err = 0;
    struct timespec T0, T1; clock_gettime(CLOCK_MONOTONIC, &T0);
    for (int i = 0; i < conns; ++i) {
        args[i].port = port; args[i].reqs = reqs; args[i].ok = &ok; args[i].err = &err;
        pthread_create(&ths[i], NULL, worker_main, &args[i]);
    }
    for (int i = 0; i < conns; ++i) pthread_join(ths[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &T1);
    // Merge latencies
    size_t total = 0; for (int i = 0; i < conns; ++i) total += args[i].lat.n;
    uint64_t *all = total ? (uint64_t *)malloc(total * sizeof(uint64_t)) : NULL;
    size_t off = 0;
    for (int i = 0; i < conns; ++i) {
        if (args[i].lat.n) {
            memcpy(all + off, args[i].lat.a, args[i].lat.n * sizeof(uint64_t));
            off += args[i].lat.n;
            free(args[i].lat.a);
        }
    }
    if (all && total > 1) {
        int cmp(const void *a, const void *b){
            uint64_t ua = *(const uint64_t*)a, ub = *(const uint64_t*)b;
            return (ua>ub) - (ua<ub);
        }
        qsort(all, total, sizeof(uint64_t), cmp);
    }
    double wall_s = (T1.tv_sec - T0.tv_sec) + (T1.tv_nsec - T0.tv_nsec)/1e9;
    double rps = wall_s > 0 ? ((double)ok / wall_s) : 0.0;
    uint64_t p50=0,p95=0,p99=0; if (all && total){
        p50 = all[(size_t)((total-1)*0.50)];
        p95 = all[(size_t)((total-1)*0.95)];
        p99 = all[(size_t)((total-1)*0.99)];
    }
    printf("sent_ok=%lu, sent_err=%lu, wall=%.3fs, RPS=%.2f, p50=%luus, p95=%luus, p99=%luus\n",
           ok, err, wall_s, rps, (unsigned long)p50, (unsigned long)p95, (unsigned long)p99);
    free(ths); free(args); free(all);
    return 0;
}
