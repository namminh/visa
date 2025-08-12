# CV — Kỹ sư Backend/Systems (VN)

## Thông Tin
- Họ tên: [Điền tên của bạn]
- Email: [email@example.com] — Điện thoại: [0xxx xxx xxx]
- Địa điểm: [TP.HCM/Hà Nội/Remote]
- GitHub: [https://github.com/yourname] — LinkedIn: [https://linkedin.com/in/yourname]

## Tóm Tắt
- Kỹ sư backend/systems tập trung C/POSIX, TCP/IP, đa luồng và PostgreSQL.
- Xây dựng payment gateway tối giản: backpressure với thread‑pool, timeout I/O, kết nối DB theo luồng, đo tải RPS và p50/p95/p99.
- Ưu tiên thiết kế đơn giản, dễ vận hành bằng shell scripts và giám sát log.

## Kỹ Năng Chính
- C/POSIX: con trỏ, quản lý bộ nhớ, `pthread`, tối ưu build (`-O2 -Wall -Wextra -g -pthread`).
- Đa luồng: thread pool cố định, hàng đợi FIFO giới hạn, backpressure, xử lý partial write/`EINTR`.
- TCP/IP: `socket/bind/listen/accept`, `SO_REUSEADDR`, `SO_RCVTIMEO`/`SO_SNDTIMEO`.
- PostgreSQL/libpq: schema, index, `PQexecParams`, backup/restore, per‑thread connection (TLS).
- Shell/Linux: script chạy/backup/chaos, tail log, `ulimit`, quan sát hệ thống.
- Hiệu năng: loadgen đa luồng, đo RPS, p50/p95/p99, điều chỉnh `THREADS`/`QUEUE_CAP`.
- Công cụ: Git, Makefile, GDB, Valgrind, `nc`, `psql`.

## Dự Án Nổi Bật
- Mini‑Visa Payment Gateway (lab thực chiến)
  - Mô tả: Dịch vụ TCP xử lý 1 request/connection, parse JSON tối giản, kiểm tra Luhn, mask PAN, ghi DB, trả JSON APPROVED/DECLINED.
  - Trách nhiệm:
    - Thiết kế & hiện thực thread pool (N worker, queue giới hạn) và backpressure.
    - Vòng lặp `accept`, dispatch job; trả `server_busy` khi quá tải.
    - Timeout I/O, `write_all` chống partial write/`EINTR`.
    - `luhn_check`, `mask_pan`, parser trường `pan`/`amount`.
    - Tích hợp PostgreSQL (libpq); `db_thread_get` tạo kết nối per‑thread; `INSERT` tham số hoá.
    - Loadgen đa luồng (C) đo `RPS`, `p50/p95/p99`.
    - Shell scripts: `run.sh`, `backup.sh`, `tests/*`, `tail-errs.sh`.
    - Logging timestamp stderr; hướng dẫn vận hành & high‑load notes.
  - Kết quả:
    - Build/run bằng `make` và scripts; hỗ trợ tuning `THREADS`/`QUEUE_CAP`.
    - Quan sát tác động cấu hình lên RPS và p95/p99.
  - Công nghệ: C, POSIX threads, TCP/IP, libpq/PostgreSQL, Bash, Makefile, Linux.

## Kinh Nghiệm Liên Quan
- High‑load & Multithreading: giới hạn song song bằng thread pool + bounded queue; per‑thread DB connection để giảm contention.
- Vận Hành & Quan Sát: script hoá vận hành, lưu `pid`, tail lỗi, backup quay vòng (7 bản); dùng `ulimit -n` khi tải cao.

## Giáo Dục
- [Cử nhân/Kỹ sư], [Ngành], [Trường], [Năm]
- Khóa ngắn: Hệ điều hành Linux, Lập trình mạng, C nâng cao (nếu có)

## Chứng Chỉ (nếu có)
- Linux Foundation/LPIC/CCNA (Networking)/PostgreSQL Associate

## Dẫn Chứng Kỹ Thuật
- Mã nguồn: `server/threadpool.c`, `server/net.c`, `server/handler.c`, `server/db.c`, `client/loadgen.c`, `scripts/*.sh`, `db/schema.sql`.
- Tài liệu: `PHONGVAN_VISUAL.md`, `PHONGVAN_FULL.md`, `HIGHLOAD_STEPS.md`.

## Mục Tiêu Công Việc
- Backend/Systems Engineer (C/C++/Rust), Platform/Infra (Networking, High‑load), hoặc Backend hiệu năng cao tích hợp DB.

