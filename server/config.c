/*
 * VN (Phỏng vấn – Cấu hình):
 * - Sử dụng biến môi trường để cấu hình nhanh: DB_URI, PORT, THREADS, QUEUE_CAP.
 * - Lợi ích: không phải sửa code khi đổi tham số; phù hợp Docker/k8s/CI.
 * - Nâng cấp sau: hỗ trợ getopt để override qua CLI; validate giá trị hợp lệ.
 */
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * Parse configuration from environment variables and command‑line arguments.
 *
 * This is a minimal implementation. For a real application you would want
 * to support command‑line options (e.g. using getopt) and provide
 * sensible defaults. Here we simply read the DB_URI environment variable
 * and use a fixed port and thread count. Adjust as needed.
 */
int config_init(Config *cfg, int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    const char *db_uri_env = getenv("DB_URI"); // Bắt buộc: chuỗi kết nối PostgreSQL
    if (!db_uri_env) {
        fprintf(stderr, "Error: DB_URI environment variable not set\n");
        return -1;
    }
    cfg->db_uri = db_uri_env;
    const char *port_env = getenv("PORT");      // Tuỳ chọn: cổng lắng nghe
    cfg->listen_port = port_env ? atoi(port_env) : 9090;
    const char *threads_env = getenv("THREADS"); // Tuỳ chọn: số worker threads
    if (!threads_env) threads_env = getenv("NUM_THREADS");
    cfg->num_threads = threads_env ? atoi(threads_env) : 4;
    if (cfg->num_threads <= 0) cfg->num_threads = 4;
    const char *qcap_env = getenv("QUEUE_CAP");  // Tuỳ chọn: sức chứa hàng đợi job
    cfg->queue_cap = qcap_env ? atoi(qcap_env) : 1024;
    if (cfg->queue_cap <= 0) cfg->queue_cap = 1024;
    return 0;
}

void config_free(Config *cfg) {
    // Nothing to free currently because we point into environment variables
    (void)cfg;
}
