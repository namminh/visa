/*
 * VN (Phỏng vấn – ý tưởng chính)
 * - Đây là điểm vào (entry point) của server.
 * - Trình tự khởi động: đọc cấu hình (ENV), khởi tạo log, kết nối DB,
 *   tạo thread pool, chạy TCP server (vòng lặp accept), dọn tài nguyên.
 * - Vì sao dùng ENV (PORT/THREADS/QUEUE_CAP)? Triển khai đơn giản, dễ đổi cấu hình
 *   khi chạy container/CI; có thể bổ sung getopt cho CLI sau.
 * - Điểm cần hỏi/giải thích:
 *   + Khi DB không kết nối được → fail sớm, log rõ.
 *   + Thread pool tạo trước giúp giới hạn mức song song (backpressure qua queue).
 *   + Dọn tài nguyên theo thứ tự ngược: net → pool → DB → log.
 */
#include "config.h"
#include "threadpool.h"
#include "net.h"
#include "db.h"
#include "log.h"
#include "metrics.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    Config cfg;
    if (config_init(&cfg, argc, argv) != 0) {
        return 1;
    }
    log_init();
    metrics_init();
    // Kết nối cơ sở dữ liệu (sẽ dùng làm bootstrap cho per-thread DB)
    DBConnection *dbc = db_connect(cfg.db_uri);
    if (!dbc) {
        return 1;
    }
    // Tạo thread pool: số luồng và sức chứa hàng đợi đọc từ ENV
    ThreadPool *pool = threadpool_create(cfg.num_threads, cfg.queue_cap);
    if (!pool) {
        db_disconnect(dbc);
        return 1;
    }
    // Bắt đầu server TCP (blocking): accept kết nối và giao việc cho thread pool
    int rc = net_server_run(&cfg, pool, dbc);
    // Dọn tài nguyên (đảm bảo không rò rỉ)
    threadpool_destroy(pool);
    db_disconnect(dbc);
    log_close();
    config_free(&cfg);
    return rc;
}
