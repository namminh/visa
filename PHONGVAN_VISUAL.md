# Sổ tay phỏng vấn – Bản trực quan (VN)

Mục tiêu: học nhanh – nhớ lâu – kể chuyện rõ. Tài liệu này chắt lọc PHONGVAN_FULL.md thành dạng trực quan: sơ đồ, checklist, lệnh nhanh, lab từng bước, và “flashcard” câu hỏi.

## 0) Sơ đồ 1 trang

```
        +---------------------------+
        |        Client (loadgen)  |
        |  JSON 1 req/connection   |
        +-------------+-------------+
                      |
                      v TCP (PORT)
        +-------------+-------------+
        |        net.c (accept)     |
        |  socket/bind/listen/accept|
        +-------------+-------------+
                      |
                 submit job
                      v
        +-------------+-------------+
        |    threadpool (N workers) |
        |  bounded queue (cap)      |
        +-------------+-------------+
                      |
                 handler.c
        +-------------+-------------+
        |  parse -> validate (Luhn) |
        |  mask PAN -> DB insert    |
        |  write APPROVED/DECLINED  |
        +-------------+-------------+
                      |
                      v
                  db.c (libpq)
        +-------------+-------------+
        | per-thread PG connection  |
        | INSERT transactions(...)   |
        +---------------------------+
```

Tư duy chính: backpressure (queue giới hạn) + timeout I/O + mỗi luồng một kết nối DB.

## 1) Checklist học nhanh

- C căn bản: con trỏ, cấp phát, build flags → `Makefile`, `threadpool.h`.
- Cấu trúc dữ liệu: hàng đợi FIFO → `server/threadpool.c` (O(1) push/pop).
- Luhn + mask PAN → `server/handler.c` (`luhn_check`, `mask_pan`).
- Multi-thread & backpressure → `threadpool_{create,submit,destroy}`.
- TCP/IP cơ bản → `server/net.c` (SO_REUSEADDR, accept loop).
- Timeout I/O → `SO_RCVTIMEO`/`SO_SNDTIMEO` trong `handler.c`.
- PostgreSQL libpq → `server/db.c` (PQexecParams), `db/schema.sql`.
- Shell ops → `scripts/run.sh`, `backup.sh`, `tests/*`.

Đo lường: `build/loadgen` in RPS, p50/p95/p99.

## 2) Lệnh nhanh theo chủ đề

- Build:
  - `make` hoặc `make server client`
- Chạy server:
  - `DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa" PORT=9090 ./scripts/run.sh 2>server.err & echo $! > server.pid`
- Bắn tải (ví dụ):
  - `THREADS=8 QUEUE_CAP=2048 ./build/loadgen 50 200 9090`
- Test tay:
  - `echo '{"pan":"4111111111111111","amount":"10.00"}' | nc 127.0.0.1 9090`
- Quan sát log lỗi:
  - `./scripts/tail-errs.sh server.err`
- DB (xem dữ liệu):
  - `psql "$DB_URI" -c "SELECT id, pan_masked, amount, status, created_at FROM transactions ORDER BY id DESC LIMIT 5;"`
- Backup:
  - `./scripts/backup.sh` (tạo `backups/*.sql.gz` – giữ 7 bản mới nhất)

## 3) Lab thực chiến (15–30 phút)

Lab A: E2E tối thiểu
1. Tạo DB/schema:
   - `psql -c 'CREATE DATABASE mini_visa;'`
   - `psql -d mini_visa -f db/schema.sql`
2. Build: `make`
3. Chạy server: đặt `DB_URI`, `PORT` → chạy `scripts/run.sh` (ghi PID, stderr vào `server.err`).
4. Gửi thử bằng `nc` hoặc `tests/send-json.sh 9090`.
5. Kiểm tra `transactions` đã có dòng mới; đọc `server.err` để xem log timestamp.

Lab B: Backpressure + đo p95
1. Xuất `THREADS=2 QUEUE_CAP=64` → chạy server.
2. `./build/loadgen 100 200 9090` và ghi lại RPS, p95.
3. Tăng `THREADS=8 QUEUE_CAP=2048` → chạy lại → so sánh RPS/p95.
4. Ghi chú tác động queue/threads vào `HIGHLOAD_PROGRESS.md`.

Lab C: Timeout I/O
1. Sửa `SO_RCVTIMEO/SO_SNDTIMEO` trong `handler.c` (nếu muốn thử khác nhau).
2. Dùng `tests/chaos.sh` để tạm dừng tiến trình → quan sát log và lỗi phía client.

## 4) Flashcards (câu hỏi nhanh)

- Tại sao cần backpressure? → Bảo vệ hệ thống khi quá tải, tránh out-of-memory.
- Luhn làm gì? → Kiểm tra checksum PAN; O(n) theo độ dài số thẻ.
- Vì sao mỗi luồng một kết nối DB? → Giảm tranh chấp mutex, tăng thông lượng.
- Khi queue đầy làm gì? → Trả JSON `server_busy` rồi đóng kết nối.
- Mask PAN như thế nào? → Giữ 6 đầu + 4 cuối, giữa là `*`.
- Timeout I/O để làm gì? → Tránh treo vĩnh viễn khi client chậm/hỏng.

## 5) Liên kết mã nguồn (map nhanh)

- `server/net.c`: accept loop, trả `server_busy` khi `threadpool_submit` fail.
- `server/threadpool.c/.h`: bounded FIFO queue, N workers.
- `server/handler.c`: đọc JSON tối giản → `parse_field`, `luhn_check`, `mask_pan`, `write_all`.
- `server/db.c/.h`: `db_connect`, `db_thread_get` (TLS), `db_insert_transaction`.
- `server/config.c/.h`: lấy `DB_URI`, `PORT`, `THREADS`, `QUEUE_CAP` từ ENV.
- `server/log.c`: log timestamp ra stderr.
- `client/loadgen.c`: multi-thread loadgen, RPS/p50/p95/p99.
- `db/schema.sql`: bảng `transactions` + index.
- `scripts/*.sh`: chạy, backup, tail lỗi; `tests/*.sh`: stress/chaos.

## 6) “Nâng cấp” gợi ý (kể trong phỏng vấn)

- Idempotency: thêm `request_id` unique; upsert khi retry.
- Keep‑alive + framing: tái sử dụng kết nối, giảm chi phí TCP.
- Structured logging (JSON) + metrics Prometheus.
- Loadgen: lưu CSV p50/p95/p99/p999 để so sánh cấu hình.

## 7) Lỗi hay gặp & mẹo

- Partial write/`EINTR`: luôn dùng `write_all` kiểu vòng lặp.
- Đọc JSON ngây thơ: demo OK; sản phẩm thật nên dùng thư viện JSON.
- FD limit: `ulimit -n` khi high‑load; kiểm tra kernel params nếu cần.
- Kiểm soát lỗi DB: log rõ `PQerrorMessage`, có thể retry có điều kiện.

---

Gợi ý sử dụng: đọc nhanh mục 0–1, thực hiện Lab A, sau đó dùng Flashcards mục 4 để ôn. Khi đã vững, đo đạc p95/p99 theo Lab B và thử nâng cấp tại mục 6.

