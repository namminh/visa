# Mini‑Visa: Tài liệu chuyên sâu (VN)

Tài liệu này mở rộng `PRACTICE_PROJECT_GIAI_THICH.md` theo chiều sâu: đi vào chi tiết kiến trúc, hiệu năng, độ tin cậy, bảo mật, kiểm thử và vận hành. Mục tiêu là giúp bạn không chỉ “dùng được” mà còn “giải thích được” như khi phỏng vấn kỹ sư hệ thống thực chiến.

## 1) Kiến trúc và các lựa chọn thiết kế
- Thành phần chính:
  - Acceptor TCP: chấp nhận kết nối, đặt socket non‑blocking/timeout, đẩy việc vào hàng đợi.
  - Thread pool (bounded queue): giới hạn độ sâu để tạo backpressure, tránh OOM/thrashing.
  - Worker per thread + PostgreSQL per thread: giảm tranh chấp (mutex) tại lớp DB client; tránh chi phí pool chung.
- Lý do chọn newline‑delimited JSON:
  - Đơn giản, dễ thử bằng `nc`, giảm phụ thuộc thư viện.
  - Cho phép keep‑alive: nhiều request/response trên một kết nối.
- Trade‑offs:
  - JSON thủ công vs thư viện: thủ công nhẹ hơn, kiểm soát lỗi tốt; thư viện an toàn hơn về edge cases.
  - 1 request/connection vs keep‑alive: keep‑alive giảm chi phí TCP handshake, nhưng cần framing và timeout để an toàn.
  - Per‑thread PG connection vs pool chung: per‑thread giảm contention, nhưng cần giới hạn số kết nối tổng tới DB.

## 2) Dòng dữ liệu và trạng thái (state machine)
- Client gửi 1 dòng JSON → parser đọc vào buffer theo từng `read()`.
- Khi gặp `\n` → parse 1 thông điệp hoàn chỉnh.
- Xác thực đầu vào: `pan` (Luhn), `amount` (định dạng số tiền), `request_id` (tùy chọn).
- Idempotency:
  - Nếu không có `request_id` → xử lý chuẩn (non‑idempotent theo spec tối thiểu).
  - Nếu có `request_id` → transaction: INSERT với UNIQUE; nếu `unique_violation` → SELECT bản ghi cũ, trả kết quả cũ.
- Ghi log 1 dòng JSON kèm `latency_us`; tăng counters; ghi response + `\n`.
- Kết thúc vòng lặp khi client đóng hoặc timeout đọc/ghi.

## 3) Xử lý I/O an toàn (EINTR/EAGAIN), partial read/write
- Đọc:
  - Luôn kiểm `EINTR` (tiếp tục) và `EAGAIN/EWOULDBLOCK` (đợi thêm hoặc timeout).
  - Buffer hóa và tìm `\n`; nếu không có, tích lũy cho lần đọc tiếp.
  - Giới hạn kích thước dòng tối đa (ví dụ 8–32KB) để chống DoS.
- Ghi:
  - `write_all()` lặp đến khi ghi đủ hoặc timeout; kiểm `EINTR/EAGAIN` tương tự.
  - Cắt nhỏ output nếu cần; tránh buffer khổng lồ.

## 4) Hiệu năng và backpressure (nguyên lý và thực hành)
- Bounded queue và backpressure:
  - Tránh chấp nhận công việc vô hạn rồi chết vì RAM; khi đầy, trả `server_busy` để client biết retry sau.
- Sizing thread pool:
  - Nếu tác vụ I/O‑bound (nặng DB), số luồng ≈ số core đến vài lần số core (tùy blocking); đo để quyết định.
  - Nếu CPU‑bound (parse/tính), số luồng ≈ số core hoặc số core ± α, giữ cache locality tốt.
- Little’s Law (xấp xỉ trực giác): L ≈ λ × W; queue sâu quá làm tăng độ trễ p95/p99.
- TCP và socket options:
  - Nagle (`TCP_NODELAY`) cân nhắc tắt nếu cần latency thấp cho gói nhỏ.
  - SO_KEEPALIVE/idle time để dọn kết nối chết; nhưng đã có timeout chủ động ở app layer.
- Đo đạc:
  - Chạy ma trận THREADS × QUEUE_CAP × (C,R) và ghi `rps,p50,p95,p99,reject_rate`; tối ưu theo mục tiêu p95.

## 5) Idempotency chuẩn mực
- Ràng buộc: UNIQUE(request_id) trên bảng giao dịch.
- Giao thức an toàn:
  - INSERT trong transaction; nếu conflict → đọc trạng thái cũ; tuyệt đối không “INSERT rồi DELETE”.
  - Trả `idempotent=true` (tuỳ chọn) để quan sát; đồng thời log event `idempotent_hit`.
- Edge cases:
  - Client không gửi `request_id` → không có đảm bảo idempotent; doc cần nói rõ.
  - Client gửi `request_id` nhưng thay đổi field khác (amount) → quyết định policy: thường trả theo bản ghi đầu tiên và log cảnh báo `idempotency_mismatch`.

## 6) Phân loại lỗi và chính sách retry
- Nhóm lỗi không retry:
  - Lỗi logic/validation (Luhn, amount format), `unique_violation` (23505), `syntax_error`.
- Nhóm lỗi có thể retry:
  - Connection reset, timeout tạm thời, `PGRES_FATAL_ERROR` liên quan mạng.
- Backoff & jitter:
  - Exponential: 50ms → 100ms → 200ms; thêm jitter ±10–20ms để tránh đồng bộ.
  - Giới hạn `max_attempts` (2–3) và `max_elapsed_time` (ví dụ 300ms) để không kéo dài request.

## 7) Circuit breaker (thiết kế và dữ liệu)
- Mục tiêu: Fail‑fast khi downstream (DB) đang “đau”, tránh dồn thêm tải làm sập hệ thống.
- Trạng thái:
  - Closed → Open: khi lỗi liên tiếp > N hoặc tỷ lệ lỗi > X% trong cửa sổ T (cần số mẫu tối thiểu).
  - Open → Half‑open: sau `open_duration` (ví dụ 2s), cho 1–vài request thăm dò.
  - Half‑open → Closed/Open: theo kết quả thăm dò.
- Dữ liệu theo dõi:
  - `consecutive_failures`, `window_successes`, `window_failures`, `last_open_ts`.
  - Đồng bộ giữa threads: dùng atomic/lock nhẹ; cập nhật counters an toàn.
- Chính sách trả về:
  - Khi Open: không gọi DB, trả `DECLINED` `reason=db_unavailable`; tăng `declined_db_unavailable`.
  - Log sự kiện `breaker_open`, `breaker_half_open`, `breaker_close` với lý do/ngưỡng.

## 8) Logging có cấu trúc (vận hành/điều tra sự cố)
- Trường khuyến nghị: `ts, lvl, event, request_id, status, reason, latency_us, thread_id, queue_size, queue_cap`.
- Mức log:
  - INFO: mỗi request 1 dòng (sản xuất có thể sampling).
  - WARN: `server_busy`, `idempotency_mismatch`, `retry_attempt`.
  - ERROR: lỗi DB không hồi phục, parse JSON hỏng nghiêm trọng.
- Thực thi:
  - `clock_gettime(CLOCK_MONOTONIC)` đo latency; `CLOCK_REALTIME` cho timestamp log.
  - Bảo đảm 1 dòng/record (không xuống dòng trong JSON log).

## 9) Metrics (đếm và phân phối)
- Counters: `total_requests, approved, declined, server_busy, db_errors, breaker_opened, retry_attempts, declined_db_unavailable`.
- Latency: có thể log thô theo bucket ở server hoặc để client loadgen tính p50/p95/p99.
- Endpoint `/metrics`:
  - Trả snapshot JSON; đảm bảo thread‑safe (atomic, memory order phù hợp).
  - Không block đường nóng (hot path) quá lâu.

## 10) Bảo mật và tuân thủ tối thiểu
- PAN masking: chỉ lưu `pan_masked` dạng `######********####`; không lưu PAN đầy đủ.
- Dữ liệu nhạy cảm khác: amount/currency OK; cân nhắc mã hoá/kiểm soát truy cập ở DB.
- Vận hành log: tránh log PAN đầy đủ; chỉ log masked hoặc request_id.
- Network: có thể bổ sung TLS ở lớp ngoài (stunnel/nginx) hoặc tích hợp TLS trong tương lai.

## 11) PostgreSQL: thực hành an toàn
- Kết nối:
  - Per‑thread connection nhưng giới hạn tổng kết nối < `max_connections` của DB; cân nhắc pgbouncer nếu cần.
  - Kiểm `PQstatus(conn) == CONNECTION_OK` cho `/readyz`.
- Transaction & timeouts:
  - Dùng `PQexecParams` trong transaction; `SET LOCAL statement_timeout` cho mỗi txn để tránh treo dài.
- Giải phóng tài nguyên:
  - Luôn `PQclear(result)`; kiểm tra rò rỉ bằng valgrind/ASan ở đường lỗi sớm.

## 12) Testing & Chaos Engineering
- Unit/integration:
  - Parser newline: test “nửa gói”, gói dài quá, ký tự lạ.
  - Idempotency: gửi 2 lần cùng `request_id`, khác `amount` → xác minh policy.
  - Retry: mô phỏng lỗi tạm thời ở DB; thấy `retry_attempt` và thành công sau 1–2 lần.
- Load & soak:
  - Long‑running (30–60 phút) để bắt rò rỉ hoặc counters lệch.
- Chaos:
  - Dừng/tạm dừng Postgres ngắn; tăng latency mạng (gợi ý `tc netem`); theo dõi breaker mở/đóng và hệ thống hồi phục.

## 13) Vận hành và quan sát
- Triển khai:
  - Config qua env: `PORT, DB_URI, THREADS, QUEUE_CAP, IO_TIMEOUT_MS, RETRY_MAX, BREAKER_*`.
  - systemd: restart on‑failure, giới hạn tài nguyên (NOFILE), log‑forwarding.
- Giám sát:
  - Alert khi `breaker_opened` tăng nhanh, `server_busy` > ngưỡng, p95 tăng mạnh.
  - Log sampling ở INFO để tiết kiệm I/O khi RPS cao.

## 14) Nâng cấp tương lai
- Giao thức: length‑prefixed framing; HTTP/1.1; gRPC.
- Bảo mật: TLS bản địa; mTLS nội bộ; rate limiting theo IP/request_id.
- Tối ưu hiệu năng: epoll/kqueue (event loop), batch DB, prepared statements, zero‑copy.
- Dữ liệu/DB: currency, id giao dịch độc lập với `request_id`, index theo thời gian, partitioning, retention.

## 15) Checklist toàn diện (thực thi + kiểm chứng)

### A. Thiết lập và chạy cơ bản
- [ ] `make` build không lỗi; tạo `./build/loadgen` và server binary.
- [ ] Chạy server bằng `./scripts/run.sh` với `DB_URI, PORT` hợp lệ.
- [ ] `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'` trả `APPROVED`.
- [ ] Log stderr hiển thị timestamp; không có backtrace bất thường.

### B. Đúng chức năng cốt lõi
- [ ] Luhn sai hoặc amount sai trả `DECLINED` + `reason` rõ ràng.
- [ ] DB có `pan_masked` đúng định dạng `######********####` cho giao dịch APPROVED.
- [ ] Khi queue đầy, trả `server_busy` (không treo/crash process).

### C. Idempotency + Health/Readiness
- [ ] Gửi 2 lần cùng `request_id` → không tăng `COUNT(*)` trong `transactions`.
- [ ] Trả cùng trạng thái (và `idempotent=true` nếu bật) cho lần gửi trùng.
- [ ] `GET /healthz` phản hồi OK khi process sống; `GET /readyz` phản ánh trạng thái kết nối DB.
- [ ] Ghi hướng dẫn kiểm tra health/ready vào `README.md`.

### D. Keep‑alive + Framing
- [ ] Một kết nối gửi nhiều dòng JSON → nhận đủ nhiều dòng response, không rò rỉ.
- [ ] `./tests/keepalive.sh 9090` pass, không treo/kết nối rò rỉ.
- [ ] Timeout đọc/ghi hợp lý: kết nối nhàn rỗi bị đóng sau hạn định.
- [ ] Giới hạn kích thước request (max line length) và từ chối hợp lệ khi vượt.

### E. Logging có cấu trúc + Metrics
- [ ] Mỗi request có 1 dòng log JSON: `ts,lvl,event,request_id,status,latency_us`.
- [ ] Có counters: `total_requests, approved, declined, server_busy` tăng đúng dưới tải.
- [ ] `printf 'GET /metrics\r\n' | nc 127.0.0.1 9090` trả snapshot JSON hợp lệ.
- [ ] Sampling log INFO 1/N khi RPS cao (nếu bật), vẫn giữ WARN/ERROR đầy đủ.

### F. Hiệu năng và backpressure
- [ ] Chạy `THREADS=8 QUEUE_CAP=2048 ./build/loadgen 50 200 9090` ổn định, không crash.
- [ ] Thu thập `rps,p50,p95,p99,reject_rate` và ghi CSV (scripts/bench.sh nếu có).
- [ ] Điều chỉnh `THREADS,QUEUE_CAP` để đạt p95 mục tiêu; ghi nhận kết quả vào `reports/RESULTS.md`.

### G. Retry có backoff
- [ ] Chỉ retry lỗi tạm thời (timeout mạng, `PGRES_FATAL_ERROR`), không retry lỗi logic (`23505`).
- [ ] Backoff 50→100→200ms, có jitter ±10–20ms hoặc decorrelated jitter.
- [ ] Giới hạn `max_attempts<=3`, `max_elapsed_time<=300ms` cho mỗi thao tác DB.
- [ ] Log sự kiện `retry_attempt` với số lần và lý do; counter `retry_attempts` tăng chính xác.

### H. Circuit breaker
- [ ] Theo dõi lỗi trong cửa sổ thời gian (ví dụ 10s); có ngưỡng lỗi hoặc rate với min samples.
- [ ] Khi vượt ngưỡng, breaker `Open` trong `open_duration` (ví dụ 2s); fail‑fast trả `db_unavailable`.
- [ ] `Half‑open` cho 1 probe; thành công → `Close`, thất bại → `Open` lại.
- [ ] Log `breaker_open/half_open/close` và counter `breaker_opened`, `declined_db_unavailable` tăng đúng.

### I. I/O an toàn và deadline
- [ ] `read_loop()` xử lý `EINTR/EAGAIN`, partial read; tích lũy đến `\n` với giới hạn kích thước.
- [ ] `write_all()` xử lý `EINTR/EAGAIN`, partial write; có per‑write timeout.
- [ ] Có per‑request deadline tổng hợp (không vượt quá SLA) cho cả đọc + xử lý + ghi.

### J. PostgreSQL an toàn
- [ ] Per‑thread connection nhưng tổng kết nối < `max_connections`; cân nhắc pgbouncer khi cần.
- [ ] `/readyz` dựa trên `PQstatus(conn)==OK`.
- [ ] Transaction dùng `PQexecParams`; `SET LOCAL statement_timeout` khớp với deadline app.
- [ ] Giải phóng `PGresult` đúng chỗ; kiểm rò rỉ bằng Valgrind/ASan (kể cả nhánh lỗi sớm).

### K. Bảo mật và tuân thủ
- [ ] Không log PAN đầy đủ; chỉ log `pan_masked` hoặc `request_id`.
- [ ] Không lưu PAN đầy đủ trong DB; chỉ `pan_masked` và metadata cần thiết.
- [ ] Cân nhắc TLS ở lớp ngoài (stunnel/nginx) hoặc kế hoạch TLS bản địa.

### L. Testing & Chaos
- [ ] Parser newline: test “nửa gói”, gói quá dài, ký tự lạ.
- [ ] Idempotency: gửi trùng `request_id`; test mismatch (same id khác amount) → policy rõ ràng và log cảnh báo.
- [ ] Retry: mô phỏng DB chập chờn, thấy `retry_attempt` và thành công.
- [ ] Chaos: dừng Postgres ngắn; breaker mở; sau phục hồi, hệ thống xử lý lại bình thường.

### M. Vận hành & giám sát
- [ ] Biến môi trường cấu hình: `PORT, DB_URI, THREADS, QUEUE_CAP, IO_TIMEOUT_MS, RETRY_MAX, BREAKER_*` hoạt động.
- [ ] systemd unit: restart on‑failure, `LimitNOFILE`, chuyển log stderr sang tập trung.
- [ ] Cảnh báo khi `server_busy` hoặc `breaker_opened` tăng nhanh; theo dõi p95/p99.

---

Tài liệu này nhằm giúp bạn có câu chuyện đầy đủ: “thiết kế vì sao”, “đo thế nào”, “xử lý khi hỏng ra sao”, và “nâng cấp gì tiếp theo”. Khi phỏng vấn, hãy dẫn chứng bằng số đo (RPS/p95/p99, reject_rate), log/matrics, và mô tả các quyết định (trade‑offs) có lý do.

## 16) Case khó mức chuyên gia: Multi‑region, HA, RL, Zero‑downtime

Mục tiêu: nâng cấp mini‑Visa thành dịch vụ có khả năng chịu lỗi cao, mở rộng đa vùng (multi‑region), giới hạn tốc độ theo tenant, và triển khai không downtime; vẫn giữ đơn giản ở mức có thể để tự triển khai.

### 16.1 Bối cảnh và tiêu chí SLO
- SLO: `p99_latency ≤ 120ms`, `availability ≥ 99.95%`, `error_rate ≤ 0.1%` ở tải 10k RPS.
- Hai vùng độc lập (A/B), đường truyền chéo 8–20ms. Khi A hỏng: RTO ≤ 60s, RPO ≤ 1s.
- Bảo toàn idempotency xuyên vùng, không double‑charge.

### 16.2 Yêu cầu giao thức và dữ liệu
- Yêu cầu payload thêm: `tenant_id`, `idempotency_key`, `ts`, `schema_version`.
- Xác thực: HMAC (ví dụ `X‑Sig`) trên payload chuẩn hóa để chống sửa dữ liệu giữa đường (TLS vẫn khuyến nghị).
- Idempotency table: `UNIQUE(tenant_id, idempotency_key)`; TTL dọn rác sau N ngày.
- Rate limiting theo tenant: token‑bucket/sliding‑window, có burst và quota ngày.

### 16.3 Kiến trúc đa vùng
- Triển khai active‑passive (bắt đầu): PG primary ở A, replica ở B (async). App chạy cả A/B; B chỉ nhận readiness false với DB read‑only; khi failover, B promote.
- Nâng cấp tùy chọn active‑active: hash `idempotency_key` → region “home”; request lệch vùng thì proxy sang home hoặc ghi local với outbox để reconcile.
- Outbox pattern: giao dịch DB và sự kiện `approved/declined` được commit atomically; background dispatcher bảo đảm at‑least‑once tới hàng đợi (kafka/nats) – mô phỏng bằng file/queue nội bộ nếu thiếu MQ.

### 16.4 Backpressure và bảo vệ đột biến
- Load‑shedding: khi queue > α×cap hoặc p95 vượt ngưỡng, trả `server_busy` có `retry_after_ms` (ngẫu nhiên trong 50–150ms để chống herd).
- Rate limiting nhiều tầng: per‑tenant, per‑IP (thô), và global circuit‑breaker theo error‑budget burn.

### 16.5 Circuit breaker v2
- Sử dụng cửa sổ trượt theo thời gian (ví dụ 10s) với số mẫu tối thiểu; mở khi `error_rate > X%` hoặc `consecutive_failures > N`.
- Half‑open giới hạn đồng thời (ví dụ 5 request thử nghiệm); đóng khi tỷ lệ thành công > ngưỡng trong cửa sổ nhỏ.
- Ghi `breaker_state`, `open_reason`, `open_until` vào metrics và log có cấu trúc.

### 16.6 Migrations không downtime
- Nguyên tắc Backward/Forward compatible: thêm cột → backfill → chạy dual‑read/dual‑write (nếu cần) → cắt cũ → xóa cột cũ.
- Online index creation; chạy shadow reads để so sánh kết quả giữa lược đồ cũ/mới (sampling).
- Triển khai blue/green hoặc canary theo phần trăm traffic; hỗ trợ `GET /version` và flag `schema_version` trong response để kiểm.

### 16.7 Bảo mật & tuân thủ mở rộng
- Giới hạn scope PCI: PAN chỉ đi qua process ngắn, zeroize buffer sau mask; không ghi đĩa không mã hóa.
- Secret/KMS: nạp khóa HMAC, DSN qua env hoặc file được mount; xoay khóa theo lịch; hỗ trợ key‑id trong header.
- TLS: offload qua reverse proxy hoặc tích hợp native; bật mTLS nội bộ giữa các service (tùy chọn nâng cao).

### 16.8 Quan sát nâng cao
- Tracing: header `traceparent` → ghi `trace_id, span_id` vào log và metrics; liên kết request ↔ truy vấn DB.
- RED/USE: `requests, errors, duration` + `utilization, saturation, errors` cho threadpool/queue.
- Log sampling động theo RPS; đảm bảo sự kiện hiếm (ERROR/WARN) luôn được ghi.

### 16.9 PostgreSQL nâng cao
- Partition theo thời gian (tháng/tuần) cho bảng giao dịch; index phù hợp hot‑path.
- Statement timeout per txn; prepared statements; retry an toàn theo mã lỗi phân loại.
- Logical replication để failover; script promote và reconfigure readonly/primary.

### 16.10 Kiểm thử và hỗn loạn cấp chuyên gia
- Tải nặng: ma trận `THREADS × QUEUE_CAP × RPS` đến 10k; thu `p50/p95/p99`, `reject_rate`, `breaker_events`.
- Fault injection: kill Postgres, tăng `tc netem` delay/loss 5–10%, throttle IO; kiểm chứng RTO/RPO.
- Consistency: gửi trùng `idempotency_key` qua 2 vùng cùng lúc; xác nhận đúng 1 bản ghi hiệu lực.
- Migrations: chạy online migration khi có tải; so sánh dual‑read không lệch.

### 16.11 Deliverables
- Mã nguồn: rate limiting per‑tenant, breaker v2, outbox đơn giản, endpoint `/version`, mở rộng `/metrics`.
- Script: `scripts/failover.sh`, `scripts/loadgen-matrix.sh`, `scripts/migrate-online.sh` (khung tối thiểu).
- Tài liệu: runbook sự cố (DB down, breaker open), hướng dẫn release canary/rollback, sơ đồ kiến trúc.
- Dashboard/alert: rule cho `server_busy`, `breaker_opened`, `error_budget_burn`, `replica_lag`.

### 16.12 Tiêu chí chấp nhận
- 10k RPS với `p99 ≤ 120ms` trong 10 phút, `error_rate ≤ 0.1%`, `reject_rate ≤ 3%` khi backpressure hoạt động.
- Failover A→B trong ≤ 60s; không double‑charge với `idempotency_key` trùng.
- Chạy 1 migration không downtime có theo dõi dual‑read/so sánh kết quả.

## 17) Lộ trình thực thi đề xuất (phân pha)
- Pha 1: RL per‑tenant + breaker v2 + metrics/tracing cơ bản; benchmark và điều chỉnh sizing.
- Pha 2: Online migration + blue/green + `/version`; bổ sung runbook và canary.
- Pha 3: Active‑passive multi‑region + failover script + outbox; kiểm thử hỗn loạn.
- Pha 4 (stretch): Active‑active hash‑routing idempotency; reconcile outbox hai chiều.

## 18) Nâng cấp mức VISA “thật” (domain, giao thức, nghiệp vụ)

Mục tiêu: từng bước biến mini‑Visa thành mô phỏng có độ phức tạp gần hệ thống thẻ thật (authorization/clearing/settlement/dispute), vẫn có thể tự triển khai và kiểm thử trên máy cá nhân bằng cơ chế mô phỏng an toàn (không chạm dữ liệu nhạy cảm thật).

### 18.1 Phân hệ và ranh giới dịch vụ
- Gateway & Translator: nhận TCP/HTTP, chuyển đổi JSON ↔ ISO 8583 (stub), quản lý phiên và framing.
- Authorization Service: quyết định APPROVED/DECLINED, quản lý idempotency, retry/breaker, rule rủi ro thời gian thực.
- Risk Engine: rule‑based + stub ML; velocity, blacklist, geo/IP, device fingerprint (giả lập).
- Tokenization & PAN Vault: sinh token thay PAN, lưu PAN mã hoá/masking (mô phỏng, không PAN thật).
- Ledger & Accounting: sổ cái bút toán kép; tài khoản merchant/issuer/fees; đảm bảo cân bằng.
- Clearing & Settlement: nhận file/batch clearing (giả lập), tính phí interchange/network, tạo bút toán settlement T+1.
- Reconciliation: so khớp giao dịch giữa auth, ledger, clearing; phát hiện lệch.
- Dispute/Chargeback: quản lý vòng đời tranh chấp, evidence, representment, arbitration (stub trạng thái và SLA).
- Key Management/HSM: quản lý khoá, DUKPT/ZMK/ZPK (chỉ mô phỏng interface, không triển khai crypto thật).
- Observability & Compliance: metrics, tracing, audit log, retention và redaction.

### 18.2 Giao thức ISO 8583 (mô phỏng tối thiểu)
- MTI phổ biến: 0100 (Authorization Request), 0110 (Response), 0200/0210 (Financial), 0420/0430 (Reversal), 0220 (Advice).
- Data Elements trọng yếu (mapping sang JSON):
  - DE2 PAN → `pan` (chỉ dùng để mask/token hoá; hạn chế lưu trữ).
  - DE3 Processing Code → `type` (AUTH/CAPTURE/REFUND/REVERSAL giả lập).
  - DE4 Amount → `amount` + `currency`.
  - DE7 Transmission Date/Time → `ts`.
  - DE11 STAN → `request_id` (idempotency key).
  - DE14 Expiry → `exp` (tuỳ chọn kiểm tra định dạng, không lưu thô).
  - DE22 POS Entry Mode, DE41 Terminal ID, DE42 Merchant ID → `device`, `merchant_id`.
  - DE39 Response Code → `status`/`reason`.
  - DE55 EMV data (ARQC/ARPC) → trường nhị phân, chỉ mô phỏng hợp lệ/hỏng.
- Translator: tạo lớp `iso8583_adapter` để (de)serialize cấu trúc sang JSON nội bộ. Bắt đầu từ subset trường; thêm dần theo nhu cầu test.

### 18.3 Authorization “thật” (dòng xử lý bên trong)
- Tiền xử lý: xác thực chữ ký/hmac header (stub), chống replay theo `ts` + cửa sổ thời gian, kiểm tra schema_version.
- Kiểm soát rủi ro: rule tuyên bố rõ ràng (velocity by PAN/merchant, geo mismatch, blacklist BIN/PAN token, limit theo MCC).
- Kiểm tra hạn mức/tín dụng: dùng Ledger account “available balance” (mô phỏng), lock soft cho AUTH; CAPTURE trừ cứng.
- Quyết định: trả mã `DE39` hợp lệ (00 APPROVED, 05 Do not honor, 14 Invalid card, 51 Insufficient funds...). Map sang JSON `status/reason`.
- Idempotency xuyên dịch vụ: `UNIQUE(tenant_id, idempotency_key)`; đọc lại kết quả cũ khi trùng; log `idempotent_hit`.
- Stand‑in Processing (STIP) giả lập: khi Risk/Ledger/DB lỗi hoặc breaker mở → áp policy đơn giản (ví dụ decline‑by‑default hoặc approve dưới ngưỡng nhỏ) và đánh dấu `stand_in=true` trong log/metrics.

### 18.4 Clearing & Settlement (dual‑message)
- Clearing file (giả lập CSV/JSON) nhập vào hằng ngày: chứa các giao dịch đã được presentment/capture.
- Tính phí: interchange, assessment, processing fee; cấu hình theo `card_type/MCC/region` (bảng cấu hình mô phỏng).
- FX: chuyển đổi tiền tệ dùng tỷ giá snapshot (stub) và ghi chênh lệch.
- Settlement T+1: tạo bút toán chuyển tiền net per merchant/issuer; sinh “vouchers” (stub) và log.
- Reconciliation: so khớp số tiền giữa AUTH ↔ CLEARING ↔ LEDGER; tạo báo cáo lệch và công cụ điều tra.

### 18.5 Ledger & Kế toán (bút toán kép)
- Mô hình tài khoản: Merchant Payables, Scheme Fees, Interchange Fees, Issuer Receivables, Chargebacks, Reserves.
- Bút toán:
  - AUTH: ghi giữ chỗ (authorization hold) ngoài bảng chính; không vào sổ cái cứng.
  - CAPTURE: Debit Cardholder/Issuer Receivable, Credit Merchant Payable; ghi phí.
  - REFUND/REVERSAL: bút toán đảo chiều; giữ nguyên idempotency.
- Bảo toàn bất biến: tổng Nợ = tổng Có; số dư không âm theo ràng buộc; kiểm thử property‑based.

### 18.6 Dispute/Chargeback Lifecycle (stub)
- Trạng thái: Open → Evidence Submitted → Representment → Pre‑Arbitration → Closed.
- Tác nhân: Cardholder, Merchant, Acquirer, Issuer, Network.
- SLA: mốc thời gian theo lý thuyết (giả lập); reminder jobs và escalation.
- Lý do (reason codes): hàng mẫu (fraud, not received, defective, duplicate).

### 18.7 Bảo mật & tuân thủ (PCI‑inspired, mô phỏng)
- Giảm phạm vi PCI: tokenization ngay tại biên, PAN vault tách biệt, không ghi PAN đầy đủ.
- Khoá & HSM: API mô phỏng cho DUKPT/ZMK/ZPK, ARQC/ARPC verification stub để không xử lý crypto thật.
- 3‑D Secure (stub): thêm trường `threeds_result` (Frictionless/Challenge/Failed) tác động quyết định rủi ro.
- PII/Privacy: data minimization, redaction trong log, TTL/retention theo loại dữ liệu.

### 18.8 Risk/Fraud nâng cao (mô phỏng có số liệu)
- Rules: velocity per PAN/merchant/IP, high‑risk MCC, BIN country ≠ IP country.
- Model stub: logistic regression giả lập hoặc score ngẫu nhiên có seed; threshold điều chỉnh qua config.
- Feedback loop: ghi outcome vào kho huấn luyện (stub); job đào tạo định kỳ (no‑op).

### 18.9 Quan sát & SLO ở quy mô lớn
- SLO: `availability ≥ 99.95%`, `p99 ≤ 120ms` ở 10k RPS cho auth; `clearing lag ≤ 5m` sau cutover.
- Metrics mới: `stand_in_rate`, `risk_decline_rate`, `ledger_invariants_violations`, `clearing_ingest_lag`.
- Tracing: span `gateway → auth → risk → ledger`; gán `trace_id` vào log ISO/JSON.

### 18.10 Quản trị dữ liệu
- Phân tầng dữ liệu: hot (auth), warm (30–90 ngày), cold (≥ 1 năm) với partitioning.
- Masking/Token ở ETL/report; quyền xem dữ liệu chi tiết theo vai trò.
- GDPR‑like: xoá dữ liệu theo yêu cầu (trên token/merchant scope, không PAN thật).

### 18.11 Kiến trúc dịch vụ đề xuất (mono → module → micro)
- Bước 1: trong 1 binary, tách module logic: `iso8583_adapter`, `auth`, `risk`, `ledger`, `clearing`.
- Bước 2: tách process `risk` và `clearing` chạy nền (batch/worker), giao tiếp qua hàng đợi nội bộ (file/DB‑outbox).
- Bước 3: cân nhắc dịch vụ hoá `gateway` và `auth` khi cần scale độc lập.

### 18.12 Lộ trình nâng cấp theo pha (khả thi tự triển khai)
- Pha A: ISO 8583 adapter (subset), map JSON↔ISO, thêm DE39 code; mở rộng metrics/tracing theo trace_id.
- Pha B: Risk rules + stand‑in; cấu hình threshold, thêm `stand_in=true`, test breaker‑open→STIP.
- Pha C: Ledger bút toán kép + invariant tests; AUTH hold → CAPTURE; REFUND/REVERSAL đường cơ bản.
- Pha D: Clearing batch giả lập + fee + FX stub; job settlement T+1; báo cáo reconciliation lệch.
- Pha E: Dispute workflow stub + dashboard SLA; hooks với ledger khi chargeback.
- Pha F (advanced): Tokenization/PAN vault mock + 3DS stub + KMS/HSM API giả lập.

### 18.13 Tiêu chí chấp nhận & demo
- ISO 8583 round‑trip: gửi 0100 → 0110 qua adapter; response code đúng mapping.
- Risk & STIP: khi DB/risk lỗi → stand‑in kick‑in; `stand_in_rate` > 0; log/tracing thể hiện tuyến.
- Ledger invariants: test property đảm bảo Nợ=Có sau CAPTURE/REFUND/REVERSAL; số dư merchant không âm.
- Clearing: ingest batch → sinh bút toán fee/FX; reconciliation không lệch hoặc báo cáo lệch chi tiết.
- Dispute: mở tranh chấp → đổi trạng thái đúng SLA; ledger nhận chargeback.
- Observability: dashboard p50/p95/p99, stand‑in, risk_decline, clearing_ingest_lag; alert hoạt động.

### 18.14 Ghi chú an toàn
- Không dùng PAN thật, không triển khai crypto sản xuất; mọi crypto/HSM chỉ ở mức interface mô phỏng.
- Tài liệu chỉ nhằm học tập; khi đưa vào sản xuất thật cần đội PCI, bảo mật, tuân thủ và quy trình chứng nhận riêng.
