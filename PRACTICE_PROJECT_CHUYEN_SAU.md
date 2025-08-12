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
