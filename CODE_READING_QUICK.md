# 🚀 Quick Start Code Tour — mini‑visa server

Mục tiêu: Nắm nhanh cấu trúc code, cách chạy, theo dấu 1 request end‑to‑end, và nơi chỉnh các tham số quan trọng.

---

## 1) Chạy nhanh (local)
- Yêu cầu: PostgreSQL có DB `mini_visa` (xem `HUONGDAN.md`), libpq dev.
- Build: `make`
- Chạy server (ví dụ):
  - `DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa" PORT=9090 THREADS=4 QUEUE_CAP=64 ./build/server`
- Gửi request mẫu:
  - `printf '{"pan":"4111111111111111","amount":"12.34","request_id":"req-1"}\n' | nc 127.0.0.1 9090`
- Check nhanh:
  - Health: `printf 'GET /healthz\n' | nc 127.0.0.1 9090`
  - Ready:  `printf 'GET /readyz\n'  | nc 127.0.0.1 9090`
  - Metrics:`printf 'GET /metrics\n' | nc 127.0.0.1 9090`

---

## 2) Sơ đồ file chính (đọc theo thứ tự)
- `server/main.c`: Khởi động hệ thống (config/log/DB/threadpool/network) + reversal worker.
- `server/net.c`: TCP accept → tạo `HandlerContext` → `threadpool_submit()`.
- `server/handler.c`: Xử lý 1 dòng JSON, validate/risk → 2PC → trả phản hồi; `/healthz|/readyz|/metrics|/version`.
- `server/iso8583.{c,h}`: Trích trường tối thiểu (`pan`, `amount`, `request_id`).
- `server/db.{c,h}`: Kết nối PG, insert/idempotent insert, per‑thread DB connection.
- `server/db_participant.{c,h}`: Participant cho Postgres (`BEGIN` → `PREPARE TRANSACTION` → `COMMIT/ROLLBACK PREPARED`).
- `server/clearing_participant.{c,h}`: Participant mô phỏng clearing với retry + circuit breaker + timeout.
- `server/transaction_coordinator.{c,h}`: State machine 2PC, ghi log đơn giản.
- `server/threadpool.{c,h}`: Hàng đợi bounded, worker threads.
- `server/metrics.{c,h}`: Counters in‑memory, phục vụ `/metrics`.
- `server/reversal.{c,h}`: Worker gửi reversal/void khi 2PC gặp outcome không chắc chắn.

---

## 3) Theo dấu 1 request (end‑to‑end)
1. `net.c` → `accept()` → tạo `HandlerContext` → `threadpool_submit(handler_job)`.
2. `handler.c` → đọc 1 dòng JSON → `iso_parse_request_line()` → Luhn + amount + risk.
3. Tạo `txn_id` (bao gồm `request_id`, timestamp), init `TransactionCoordinator` (TLS).
4. Tạo participants:
   - DB: `db_participant_begin()` → insert/idempotent (bảng `transactions`).
   - Clearing: `clearing_participant_set_transaction()` (pan_masked, amount, merchant).
5. Gọi `txn_commit()`:
   - Pha 1 (PREPARE): gọi `p->prepare()` từng participant.
   - Pha 2 (COMMIT/ABORT): nếu tất cả OK → `p->commit()`, ngược lại `p->abort()`.
6. Thành công: trả `APPROVED`; thất bại: trả `DECLINED` và `reversal_enqueue()` để gỡ hold.
7. `/metrics` phản ánh counters (tổng, approved/declined, 2PC, breaker, reversal).

Tips theo dấu: grep `txn_id` trong `logs/transactions.log` và stderr để xem phase.

---

## 3.1) Sequence ASCII – 2PC
```
Client        Handler           Coordinator         DB Participant        Clearing
  |   JSON       |                    |                    |                   |
  |------------->|                    |                    |                   |
  |              | parse/validate     |                    |                   |
  |              | begin txn_id       |                    |                   |
  |              |------------------->| TXN_PREPARING      |                   |
  |              |                    | prepare(db)        |                   |
  |              |                    |------------------->| BEGIN, PREPARE    |
  |              |                    | prepare(clearing)  |                   |
  |              |                    |------------------------------->| hold   |
  |              |                    | TXN_PREPARED       |                   |
  |              |                    | TXN_COMMITTING     |                   |
  |              |                    | commit(db)         |                   |
  |              |                    |------------------->| COMMIT PREPARED   |
  |              |                    | commit(clearing)   |                   |
  |              |                    |------------------------------->| settle |
  |              |                    | TXN_COMMITTED      |                   |
  |  APPROVED    |                    |                    |                   |
  |<-------------|                    |                    |                   |
```

Failure path: nếu bất kỳ prepare/commit lỗi → TXN_ABORTING → abort(db/clearing). Khi abort vẫn có thể enqueue reversal.

---

## 3.2) Sequence ASCII – Reversal Worker
```
Handler            ReversalQueue       ReversalWorker          Clearing
  | enqueue fail      |                     |                    |
  |------------------>| (task: txn_id,amt) |                    |
  |                   |                     | dequeue/backoff   |
  |                   |                     |------------------>| abort/void
  |                   |                     | <----- OK/ERR ----|
  |                   |                     | retry or done     |
```

---

## 4) Nâng cấp “smart” đáng chú ý
- 2PC: Không giữ global mutex khi gọi I/O (prepare/commit/abort) → throughput tốt hơn.
- Clearing: Retry + exponential backoff; circuit breaker theo cửa sổ lỗi; timeout có thể cấu hình.
- Reversal worker: Hàng đợi nền tự động gửi abort/void khi commit fail (unknown outcome).
- Metrics: Bổ sung `twopc_committed|twopc_aborted|clearing_cb_short_circuit|reversal_*`.

---

## 5) Tham số môi trường hữu ích
- Cốt lõi: `DB_URI`, `PORT`, `THREADS`/`NUM_THREADS`, `QUEUE_CAP`.
- 2PC: `TWOPC_PREPARE_TIMEOUT`, `TWOPC_COMMIT_TIMEOUT` (giây).
- Clearing: `CLEARING_TIMEOUT`, `CLEARING_RETRY_MAX`, `CLEARING_CB_WINDOW`, `CLEARING_CB_FAILS`, `CLEARING_CB_OPEN_SECS`.
- Reversal: `REVERSAL_MAX_ATTEMPTS`, `REVERSAL_BASE_DELAY_MS`.

---

## 6) Payload mẫu (newline‑delimited)
Auth đơn giản:
```
{"pan":"4111111111111111","amount":"12.34","request_id":"req-123"}
```
Phản hồi mẫu:
```
{"status":"APPROVED","txn_id":"visa_req-123_1690000000"}
```

---

## 7) Quan sát nhanh
- `printf 'GET /metrics\n' | nc 127.0.0.1 9090` → xem counters.
- `tail -f server.err` → log JSON 1 dòng/transaction.
- `tail -f logs/transactions.log` → dấu vết 2PC (BEGIN/PREPARE/COMMIT/ABORT).

---

## 8) Góc dev: thử kịch bản lỗi
- Tăng thất bại mạng clearing (giả lập): giữ mặc định, breaker sẽ mở khi lỗi nhiều.
- Ép timeout ngắn: `CLEARING_TIMEOUT=2` + `CLEARING_RETRY_MAX=0` → nhiều abort.
- Đẩy throughput: `THREADS=8 QUEUE_CAP=1024` → quan sát latency/counters.

---

## 9) Đường tắt đọc code
- Bắt đầu ở `server/handler.c` tìm các `[ANCHOR:...]` để lần theo các bước.
- Mở `server/transaction_coordinator.c` để xem cách set state và vị trí unlock/lock.
- Xem `server/clearing_participant.c` phần circuit breaker + retry logic.
- Kiểm tra `server/reversal.c` để hiểu backoff và tiêu chí dừng.
