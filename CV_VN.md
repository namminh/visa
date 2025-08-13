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

## Kỹ Năng Chuyên Sâu (Payments/Systems)
- ISO 20022: mapping/validation `pacs.008/.002/.009`, `camt.056/.029/.053`, `pain.001`; XSD schema, choreography request/response, idempotency theo message-id.
- Liên ngân hàng: RTGS/ACH, net vs gross settlement, lịch clearing T+0/T+1, cutover EOD/EOM, quản lý thanh khoản.
- Bank adapters: H2H (REST/SOAP), SFTP/AS2, MQ (Kafka/RabbitMQ), ký số & mã hoá (PKI, CMS, JWS), chuẩn file (CSV/XML).
- Payments/ISO8583: ánh xạ trường cơ bản (PAN, amount, STAN, DE39), adapter JSON↔ISO (subset), luồng AUTH/CAPTURE/REFUND/REVERSAL (stub).
- Idempotency: `UNIQUE(request_id)`/`(tenant_id,idempotency_key)`, trả kết quả cũ khi trùng, policy mismatch an toàn.
- Rủi ro (Risk): rule velocity per‑PAN/merchant, blacklist/MCC (thiết kế), stand‑in khi downstream lỗi.
- Ledger kép: mô hình bút toán AUTH hold/CAPTURE/REFUND/REVERSAL, kiểm bất biến Nợ=Có (thiết kế và stub).
- Clearing/Settlement: ingest batch giả lập, tính phí/FX stub, reconciliation so khớp số liệu (thiết kế).
- Độ tin cậy: retry với backoff+jitter, circuit breaker (closed/open/half‑open), keep‑alive + timeout.
- Quan sát: logging có cấu trúc 1 dòng, `/metrics` JSON counters, `/version`, theo dõi p95/p99, error budget.
- Bảo mật: mask PAN (6+4), giảm phạm vi PCI, tokenization/vault (mock), TLS offload (gợi ý triển khai).
- PostgreSQL nâng cao: statement timeout, per‑thread connections, idempotent upsert, chuẩn bị cho partitioning.
- Mạng: framing newline, partial read/write, kiểm soát Nagle (`TCP_NODELAY`), thiết kế backpressure đầu vào.


## Dự Án Nổi Bật
- Payment Gateway 
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
    - Mô‑đun bổ sung: `iso8583_adapter` (chuẩn hoá input), `risk` (velocity limit), `ledger` (AUTH hold stub), `clearing` (worker stub).
    - Endpoint vận hành: `/healthz`, `/readyz`, `/metrics` (thêm `risk_declined`), `/version`.
  - Kết quả:
    - Build/run bằng `make` và scripts; hỗ trợ tuning `THREADS`/`QUEUE_CAP`.
    - Quan sát tác động cấu hình lên RPS và p95/p99; theo dõi risk decline qua metrics.
  - Công nghệ: C, POSIX threads, TCP/IP, libpq/PostgreSQL, Bash, Makefile, Linux.
 
- Hệ thống Thanh toán Điện tử Liên Ngân hàng (đang tham gia)
  - Mô tả: Nền tảng chuyển mạch/thanh toán liên ngân hàng với gateway ISO 20022/ISO 8583, clearing/settlement và đối soát đa bên.
  - Thành phần chính:
    - Gateway ISO 20022: tiếp nhận/định tuyến `pacs.008/.002`, `camt.056/.029`; ký số/mã hoá, kiểm XSD.
    - Interop ISO 8583/NAPAS: adapter, quản lý mã phản hồi, STAN/RRN.
    - Bank adapters: kênh REST/SOAP, SFTP/AS2, MQ; quản lý chứng thư và khoá.
    - Fraud/AML: sanction screening, rule velocity/blacklist; ghi nhận case (tích hợp sau).
    - Clearing/Settlement: lịch T+0/T+1, net settlement, báo cáo đối soát.
    - Reconciliation: đối soát nội bộ ↔ đối tác (Nostro/Vostro), xử lý ngoại lệ.
    - HA/DR & vận hành: giám sát SLO, cảnh báo, kịch bản DR (RPO/RTO).
  - Vai trò & đóng góp: [tùy chỉnh theo thực tế: thiết kế gateway, viết adapter, đối soát, bảo mật, vận hành...]
  - Công nghệ: [tùy chỉnh: Java/Go/C], MQ (Kafka/RabbitMQ), DB [Oracle/PostgreSQL], PKI/HSM, Linux.

## Kinh Nghiệm Liên Quan
- High‑load & Multithreading: giới hạn song song bằng thread pool + bounded queue; per‑thread DB connection để giảm contention.
- Vận Hành & Quan Sát: script hoá vận hành, lưu `pid`, tail lỗi, backup quay vòng (7 bản); dùng `ulimit -n` khi tải cao.
- Payments domain: idempotency, mask PAN, mapping ISO fields, response code (DE39), rule risk và stand‑in (stub).
- Độ tin cậy: circuit breaker, backoff+jitter, health/readiness, expose metrics và version.
- Liên ngân hàng: ISO 20022/8583, RTGS/ACH, clearing/settlement, reconciliation đa bên, quản trị chứng thư/khóa.
- Bảo mật & tuân thủ: ký số/mã hoá, mTLS, phân tách phạm vi dữ liệu nhạy cảm, runbook sự cố/DR.

## Giáo Dục
- [Cử nhân/Kỹ sư], [Ngành], [Trường], [Năm]
- Khóa ngắn: Hệ điều hành Linux, Lập trình mạng, C nâng cao (nếu có)

## Chứng Chỉ (nếu có)
- Linux Foundation/LPIC/CCNA (Networking)/PostgreSQL Associate

## Dẫn Chứng Kỹ Thuật
- Mã nguồn: `server/threadpool.c`, `server/net.c`, `server/handler.c`, `server/db.c`, `server/iso8583.c`, `server/risk.c`, `server/ledger.c`, `server/clearing.c`, `client/loadgen.c`, `scripts/*.sh`, `db/schema.sql`.
- Tài liệu: `PHONGVAN_VISUAL.md`, `PHONGVAN_FULL.md`, `HIGHLOAD_STEPS.md`, `PRACTICE_PROJECT_CHUYEN_SAU.md`.

## Mục Tiêu Công Việc
- Backend/Systems Engineer (C/C++/Rust), Platform/Infra (Networking, High‑load), hoặc Backend hiệu năng cao tích hợp DB.
