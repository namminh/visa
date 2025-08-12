# Bộ ghi chú phỏng vấn đầy đủ (VN)

Tài liệu này mở rộng từ phong_van.txt và map vào dự án mini-visa để bạn có ví dụ thực chiến cho từng mục. Mỗi mục gồm: ý chính, điểm nói, lệnh/demo nhanh, và tham chiếu mã trong repo.

## 1) C (ngôn ngữ & quản lý bộ nhớ)
- Ý chính: C thuần, con trỏ, cấp phát động, debug, tối ưu compile.
- Điểm nói:
  - Con trỏ/hàm: `threadpool.h` dùng `typedef void (*job_fn)(void*)` (callback).
  - Quản lý cấp phát/giải phóng: `server/threadpool.c` tạo Job, free sau khi chạy; `handler.c` free context; `db.c` free PG, mutex.
  - Tránh leak/ghi thiếu: `handler.c` có `write_all` xử lý `EINTR`/partial write.
  - Cờ biên dịch: `Makefile` với `-Wall -Wextra -O2 -g -pthread`.
- Demo nhanh:
  - `make clean && make` (bắt cảnh báo).
  - Valgrind (kiểm tra rò rỉ): `valgrind --leak-check=full ./build/server` (sau khi server đầy đủ tính năng).

## 2) Cấu trúc dữ liệu & giải thuật
- Ý chính: hàng đợi FIFO, Luhn, phân tích độ phức tạp.
- Điểm nói:
  - Hàng đợi liên kết: `server/threadpool.c` (push O(1), pop O(1)).
  - Thuật toán Luhn: `server/handler.c::luhn_check` (O(n) theo độ dài PAN).
  - Phân tích Queue Cap: `QUEUE_CAP` quyết định backpressure, cân bằng latency vs drop rate.
- Demo nhanh: mở `server/threadpool.c` giải thích head/tail/condvar.

## 3) High‑load & Multi‑threaded
- Ý chính: acceptor + thread pool, backpressure, per‑thread DB.
- Điểm nói:
  - `server/net.c`: accept → submit job; nếu đầy trả `server_busy` JSON.
  - `server/threadpool.c`: N worker, queue giới hạn.
  - `server/db.c`: `db_thread_get` tạo PGconn theo luồng (TLS) giảm contention.
- Demo tải:
  - `export THREADS=8 QUEUE_CAP=2048` rồi chạy `./build/loadgen 50 200 9090`.
  - Kết quả: `RPS`, `p50/p95/p99` in từ loadgen.
- Tài liệu: `HIGHLOAD_STEPS.md`, `HIGHLOAD_PROGRESS.md`.

## 4) UNIX cơ bản
- Ý chính: quy trình, service, file/permission, quan sát hệ thống.
- Điểm nói:
  - Theo dõi log: `./scripts/tail-errs.sh server.err`.
  - Quản lý tiến trình: `ps`, `kill $(cat server.pid)`.
  - FD limit: `ulimit -n` (nâng khi high‑load), quyền file `chmod/chown`.
- Demo nhanh:
  - `PORT=9090 ./scripts/run.sh 2>server.err & echo $! >server.pid`
  - `ps -o pid,cmd -p $(cat server.pid)`; `kill $(cat server.pid)`.

## 5) TCP/IP networking
- Ý chính: socket TCP, timeout, so sánh UDP, test thủ công.
- Điểm nói:
  - `server/net.c`: `socket/bind/listen/accept`, `SO_REUSEADDR`.
  - `server/handler.c`: `SO_RCVTIMEO/SO_SNDTIMEO` chống treo I/O.
  - Cân nhắc nâng cấp: epoll, keep‑alive (nhiều request/connection), frame protocol.
- Demo nhanh:
  - Gửi tay: `echo '{"pan": "4111111111111111", "amount":"10.00"}' | nc 127.0.0.1 9090`.

## 6) Shell scripting
- Ý chính: tự động hóa chạy, backup, stress, theo dõi.
- Điểm nói:
  - `scripts/run.sh`: build if needed, đọc `DB_URI`/`PORT`.
  - `scripts/backup.sh`: pg_dump + xoay vòng 7 bản.
  - `tests/stress.sh`, `tests/chaos.sh`, `tests/send-json.sh`.
- Demo nhanh: `./scripts/backup.sh` (tạo `backups/*.sql.gz`).

## 7) SVN/Git
- Ý chính: quy trình nhánh, merge, rebase, giải quyết conflict; so sánh SVN/Git.
- Điểm nói:
  - Git: branch feature, mở PR, rebase giữ lịch sử gọn; stash/patch.
  - SVN: central repo, không có commit local; khi nào tổ chức chọn SVN (quy định chặt, tooling cũ).
- Demo lệnh (tham khảo):
  - `git checkout -b feature/threadpool`
  - `git rebase main` / `git merge` / `git revert <sha>`

## 8) SQL, Oracle, PostgreSQL
- Ý chính: schema, transaction, index, backup/restore, khác biệt.
- Điểm nói:
  - Bảng `transactions` + index theo `created_at`; INSERT qua `PQexecParams`.
  - Transaction & idempotency (định hướng): thêm `request_id` unique.
  - Oracle vs Postgres: khác PL/SQL vs PL/pgSQL, sequence/identity, extension (PostGIS, etc.).
- Demo nhanh:
  - Xem dữ liệu: `psql "$DB_URI" -At -F ' | ' -c "SELECT id, pan_masked, amount, status, to_char(created_at,'YYYY-MM-DD HH24:MI:SS') FROM transactions ORDER BY id DESC LIMIT 10;"`
  - Backup: `./scripts/backup.sh`; Restore: `psql -d mini_visa -f backup.sql` (ví dụ).
- Tài liệu: `DB_PERMISSIONS.md`, `DB_QUERIES.md`.

## Checklist nói nhanh (30–60s)
- Kiến trúc: acceptor + thread pool + backpressure; per‑thread DB conn.
- An toàn: timeout I/O, write_all; mask PAN; (định hướng) idempotency.
- Đo đạc: loadgen đa luồng, RPS, p95.
- Sẵn sàng mở rộng: epoll/keep‑alive, structured logging/metrics.

## Bài tập mở rộng (để kể trong phỏng vấn)
- Thêm `request_id` unique + idempotency.
- Implement keep‑alive + khung framing JSON.
- Structured logging (JSON) + Prometheus exporter.
- Loadgen: lưu CSV p50/p95/p99/p999, so sánh cấu hình THREADS/QUEUE_CAP.
