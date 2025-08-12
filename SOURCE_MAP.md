# mini‑visa C Source Map (VN)

Tài liệu tóm lược cấu trúc mã C, luồng gọi hàm, và vai trò của từng file trong server và client. Dùng để “nhìn nhanh” khi đọc code hoặc phỏng vấn.

## Sơ đồ tổng thể (ASCII)

```
           +-------------------+
           |  server/main.c    |
           |  - entrypoint     |
           |  - init cfg/log   |
           +---------+---------+
                     |
                     v
           +---------+---------+
           |  server/net.c     |
           |  net_server_run() |
           |  - listen/accept  |
           +---------+---------+
                     |
      submit(handler_job, ctx) if queue has cap
                     |
                     v
           +---------+---------+
           | server/threadpool |
           |  - fixed workers  |
           |  - bounded queue  |
           +---------+---------+
                     |
                     v
           +---------+---------+
           | server/handler.c  |
           |  handler_job()    |
           |  - read (\n frame)|
           |  - parse/validate |
           |  - idempotency    |
           |  - write JSON     |
           +----+----------+---+
                |          |
                |          |
                v          v
       +--------+---+   +--+----------------+
       | server/db |   | server/log.[ch]   |
       |  - libpq  |   |  log_message_json |
       |  - txn/uq |   +-------------------+
       +--------+--+
                |
                v
       +--------+----------------+
       | server/metrics.[ch]     |
       |  - counters snapshot    |
       |  - GET /metrics         |
       +-------------------------+

Client: client/loadgen.c -> mở nhiều kết nối, gửi JSON, đo RPS/p50/p95/p99.
```

## Sơ đồ luồng xử lý request (từng bước)

```
TCP accept -> enqueue job -> worker takes job ->
  read line (newline-delimited) ->
  if "GET /healthz|/readyz|/metrics" -> trả nhanh và log
  else parse {pan, amount, [request_id]} ->
    validate Luhn + amount ->
    DB per-thread (db_thread_get) ->
      if request_id: INSERT ON CONFLICT -> SELECT cũ
      else: INSERT thường
    update metrics -> log JSON -> write response + '\n'
close on timeout/EOF
```

## Bản đồ file và vai trò

- `server/main.c`
  - Khởi động: `config_init`, `log_init`, `metrics_init`, `db_connect` (bootstrap), `threadpool_create`, `net_server_run`.
  - Dọn dẹp: `threadpool_destroy`, `db_disconnect`, `log_close`.

- `server/config.[ch]`
  - `config_init`: đọc `DB_URI`, `PORT`, `THREADS`, `QUEUE_CAP` từ env; cung cấp `Config` cho server.

- `server/net.[ch]`
  - `net_server_run(&cfg, pool, dbc)`: bind/listen, vòng lặp `accept`, tạo `HandlerContext`, gọi `threadpool_submit`.
  - Khi queue đầy: gửi `{\"status\":\"DECLINED\",\"reason\":\"server_busy\"}\\n` và tăng counter.

- `server/threadpool.[ch]`
  - Hàng đợi FIFO giới hạn (bounded) + số worker cố định.
  - API: `threadpool_create`, `threadpool_submit`, `threadpool_destroy`.

- `server/handler.[ch]`
  - `handler_job(ctx)`: vòng lặp keep‑alive đọc nhiều dòng:
    - Health/Ready/Metrics: phát hiện chuỗi `GET /...` và phản hồi nhanh (`OK`, `NOT_READY`, snapshot JSON).
    - Parse JSON tối giản: `parse_field(\"pan\"|\"amount\"|\"request_id\")`.
    - Kiểm tra: `luhn_check`, `amount` trong ngưỡng; `mask_pan` thành `pan_masked`.
    - Idempotency: gọi DB theo `request_id` nếu có; trả APPROVED/DECLINED tương ứng.
    - Ghi log một dòng JSON qua `log_message_json`, cập nhật counters, gọi `write_all` trả response.

- `server/db.[ch]`
  - Kết nối: `db_connect`, `db_disconnect`, `db_thread_get` (per‑thread TLS).
  - Tác vụ: `db_insert_transaction`, `db_insert_or_get_by_reqid`, `db_is_ready`.
  - Ghi chú: dùng `PQexecParams`; khi UNIQUE `request_id` trùng → SELECT trạng thái cũ.

- `server/log.[ch]`
  - `log_message_json(level, event, request_id, status, latency_us)` in một dòng JSON (stderr).

- `server/metrics.[ch]`
  - Counters: `metrics_inc_total/approved/declined/server_busy`, `metrics_snapshot`.
  - Endpoint: handler xử lý `GET /metrics` trả JSON snapshot.

- `client/loadgen.c`
  - Tạo N luồng client; mỗi luồng gửi R yêu cầu (mỗi yêu cầu 1 TCP connect), đọc phản hồi.
  - Đo và in: `RPS`, `p50/p95/p99` (micro giây), `sent_ok/sent_err`.

## Quan hệ phụ thuộc (headers)

- `main.c` -> `config.h`, `threadpool.h`, `net.h`, `db.h`, `log.h`, `metrics.h`
- `net.c` -> `net.h`, `threadpool.h`, `handler.h`
- `handler.c` -> `handler.h`, `log.h`, `metrics.h`, `db.h`, `net.h`, libc
- `db.c` -> `db.h`, `libpq-fe.h` (libpq), pthread (TLS)
- `threadpool.c` -> `threadpool.h`, pthread
- `log.c` -> `log.h`, libc time
- `metrics.c` -> `metrics.h`, stdatomic/locks (tuỳ triển khai)
- `client/loadgen.c` -> sockets, pthread

## Điểm mở rộng/thực hành đề xuất

- Retry/backoff + circuit‑breaker (M3) ở layer DB/handler.
- Deadline tổng per‑request; giới hạn kích thước dòng (anti‑DoS).
- Prepared statements cho DB; `SET LOCAL statement_timeout` trong txn.
- Sampling log INFO; thêm histogram thô cho latency ở server.

## Ví dụ nhanh (request/response)

- Gửi:
  - `{\"pan\":\"4111111111111111\",\"amount\":\"10.00\",\"request_id\":\"abc123\"}\\n`
- Trả:
  - `{\"status\":\"APPROVED\"}\\n` hoặc `{\"status\":\"DECLINED\",\"reason\":\"luhn_failed\"}\\n`
- Health/Ready/Metrics:
  - `GET /healthz\\r\\n` -> `OK\\n`
  - `GET /readyz\\r\\n`  -> `OK\\n` hoặc `NOT_READY\\n`
  - `GET /metrics\\r\\n` -> `{...}\\n`

