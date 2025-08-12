# Ghi chú tiến trình: High-load & Multi-threaded (bước 1-3)

Mục tiêu: chuyển server từ xử lý tuần tự sang đa luồng với thread pool, có hàng đợi để chịu tải tốt hơn và tránh nghẽn.

## Thay đổi chính trong mã (đến hiện tại)
- `server/threadpool.c`: cài đặt đầy đủ ThreadPool
  - Hàng đợi công việc (bounded, mặc định 1024), `pthread_mutex_t`, `pthread_cond_t`.
  - N worker threads chờ việc, lấy job và chạy `job_fn(arg)`.
- `server/net.c`:
  - Sau khi `accept()` tạo `HandlerContext`, không xử lý inline nữa.
  - Gọi `threadpool_submit(pool, handler_job, ctx)`; nếu hàng đợi đầy → đóng kết nối (backpressure).
- `server/db.c`:
  - Bổ sung `pthread_mutex_t mu` trong `DBConnection` để bảo vệ `PGconn` khi nhiều worker cùng ghi.
  - `db_insert_transaction(...)` khóa/mở khóa mutex quanh `PQexecParams`.
  - THÊM per‑worker DB connection: `db_thread_get(bootstrap)` tạo kết nối theo luồng dựa trên URI gốc, lưu TLS; giảm tranh chấp mutex.

- `server/config.c`:
  - Đọc cấu hình qua biến môi trường: `PORT`, `THREADS`/`NUM_THREADS`, `QUEUE_CAP`.
  - Mặc định: port 9090, threads 4, queue 1024.

- `client/loadgen.c`:
  - Đã hiện thực worker đa luồng: mỗi worker gửi `reqs` yêu cầu (1 request/connection) và đếm `sent_ok/sent_err`.

Lưu ý: Cách này dùng chung 1 kết nối DB có khóa (đơn giản, an toàn). Bước tiếp theo có thể nâng cấp thành “mỗi worker một kết nối” để giảm điểm nghẽn mutex.

## Cách build và chạy
```bash
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
make clean && make
PORT=9090 ./scripts/run.sh 2>server.err & echo $! >server.pid
```

Kiểm tra server đã lắng nghe:
```bash
./scripts/tail-errs.sh server.err   # thấy “Server listening on port 9090”
```

## Kiểm thử song song
- Gửi nhanh 1 yêu cầu hợp lệ:
```bash
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'
```
- Gửi tải (nhiều yêu cầu):
```bash
CONNS=50 REQS=200 PORT=9090 ./tests/stress.sh
# Hoặc tăng dần: CONNS=100 REQS=500 ...

# Dùng loadgen mới để đo tổng kết nhanh:
./build/loadgen 50 200 9090

Quan sát: `sent_ok` tăng nhanh hơn, server vẫn ổn định dưới tải; khi hàng đợi đầy, client nhận JSON `{"status":"DECLINED","reason":"server_busy"}` thay vì bị đóng im lặng.
```

Kỳ vọng:
- Server không crash khi nhiều kết nối.
- Khi hàng đợi đầy, một số kết nối bị đóng sớm (bảo vệ hệ thống).
- DB có thêm bản ghi APPROVED tương ứng (tuỳ dữ liệu hợp lệ được gửi).

## Quan sát kết quả
- DB (10 bản ghi mới nhất):
```bash
psql "$DB_URI" -At -F ' | ' -c "SELECT id, pan_masked, amount, status, to_char(created_at,'YYYY-MM-DD HH24:MI:SS') FROM transactions ORDER BY id DESC LIMIT 10;"
```
- Log lỗi/cảnh báo theo thời gian thực:
```bash
./scripts/tail-errs.sh server.err
```

## Định hướng bước tiếp theo
- Kết nối DB per‑worker (loại bỏ mutex bottleneck): mỗi worker giữ `PGconn` riêng.
- Thread pool: cấu hình `cap` từ `Config` và trả thông báo “server busy” thay vì đóng ngay.
- Timeouts: đặt timeout đọc/ghi socket và `statement_timeout` cho DB.
- Loadgen: hoàn thiện `client/loadgen.c` để đo RPS, p50/p95 latency.
- Logging/metrics: thêm `request_id`, thời gian xử lý, tỉ lệ lỗi.

Khi thực hiện bước tiếp theo, cập nhật tài liệu này để theo dõi tiến trình.
