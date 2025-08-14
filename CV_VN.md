# CV — Kỹ sư Backend/Systems (VN)

## Thông Tin
- Họ tên: Nguyễn Minh Nam
- Giới tính: Nam
- Ngày sinh: 01/10/1989 — Quốc tịch: Việt Nam
- Email: nammta@gmail.com — Điện thoại: 0963426305
- Địa điểm: Hà Nội/Remote
- GitHub: https://github.com/nammta — LinkedIn: https://www.linkedin.com/in/minh-nam-nguyen-5539656a/

## Tóm Tắt
- Kỹ sư backend/systems tập trung C/POSIX, TCP/IP, đa luồng và Oracle/PostgreSQL.
- Xây dựng hệ thống thanh toán tối giản: backpressure với thread‑pool, timeout I/O, kết nối DB theo luồng, đo tải RPS và p50/p95/p99.
- Ưu tiên thiết kế đơn giản, dễ vận hành bằng shell scripts, Oracle job scheduling, backup rotation, và giám sát log.

## Kỹ Năng Chính
- C/POSIX: con trỏ, quản lý bộ nhớ, `pthread`, tối ưu build (`-O2 -Wall -Wextra -g -pthread`).
- Đa luồng: thread pool cố định, hàng đợi FIFO giới hạn, backpressure, xử lý partial write/`EINTR`.
- TCP/IP: `socket/bind/listen/accept`, `SO_REUSEADDR`, `SO_RCVTIMEO`/`SO_SNDTIMEO`.
- Oracle/libpq: schema, index, `PQexecParams`, backup/restore, per‑thread connection (TLS).
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
- Oracle nâng cao: statement timeout, per‑thread connections, idempotent upsert, chuẩn bị cho partitioning.
- Mạng: framing newline, partial read/write, kiểm soát Nagle (`TCP_NODELAY`), thiết kế backpressure đầu vào.


## Dự Án Nổi Bật
- Hệ thống Thanh toán Điện tử Liên Ngân hàng (đang tham gia)
  - Mô tả: Nền tảng chuyển mạch/thanh toán liên ngân hàng với gateway ISO 20022/ISO 8583, clearing/settlement và đối soát đa bên.
  - Thành phần chính:
    - Gateway ISO 20022: tiếp nhận/định tuyến `pacs.008/.002`, `camt.056/.029`; ký số/mã hoá, kiểm XSD.
    - Interop ISO 8583/NAPAS: adapter, quản lý mã phản hồi, STAN/RRN.
    - Bank adapters: kênh REST/SOAP, SFTP/AS2, MQ; quản lý chứng thư và khoá.
    - Tích hợp với Core Banking System (CBS) thông qua API SOAP.
    - Fraud/AML: sanction screening, rule velocity/blacklist; ghi nhận case (tích hợp sau).
    - Clearing/Settlement: lịch T+0/T+1, net settlement, báo cáo đối soát.
    - Reconciliation: đối soát nội bộ ↔ đối tác (Nostro/Vostro), xử lý ngoại lệ.
    - HA/DR & vận hành: giám sát SLO, cảnh báo, kịch bản DR (RPO/RTO).
  - Vai trò & đóng góp: thiết kế gateway, viết adapter, đối soát, bảo mật, vận hành
  - Công nghệ: Pro*c, C/POSIX threads, TCP/IP, Oracle, Bash, Makefile, AIX.

## Kinh Nghiệm Liên Quan
- High‑load & Multithreading: giới hạn song song bằng thread pool + bounded queue; per‑thread DB connection để giảm contention.
- Vận Hành & Quan Sát: script hoá vận hành, lưu `pid`, tail lỗi, backup quay vòng (7 bản); dùng `ulimit -n` khi tải cao.
- Payments domain: idempotency, mask PAN, mapping ISO fields, response code (DE39), rule risk và stand‑in (stub).
- Độ tin cậy: circuit breaker, backoff+jitter, health/readiness, expose metrics và version.
- Liên ngân hàng: ISO 20022/8583, RTGS/ACH, clearing/settlement, reconciliation đa bên, quản trị chứng thư/khóa.
- Bảo mật & tuân thủ: ký số/mã hoá, mTLS, phân tách phạm vi dữ liệu nhạy cảm, runbook sự cố/DR.

## Giáo Dục
- Cử nhân: Điện-Điện Tử

## Mục Tiêu Công Việc
- Backend/Systems Engineer (C/C++/Rust), Platform/Infra (Networking, High‑load), Backend hiệu năng cao tích hợp DB.
