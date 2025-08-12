# Dự án ôn luyện sát thực tế: mini‑visa (VN)

Tài liệu này biến bộ ghi chú trong PHONGVAN_FULL.md thành một lộ trình học tập “sát thực tế” với độ phức tạp mô phỏng vừa đủ để luyện phỏng vấn. Mỗi giai đoạn có mục tiêu, phạm vi, tiêu chí chấp nhận (Acceptance), cách kiểm thử, và mapping sang mã nguồn trong repo.

## Tổng quan hệ thống (điểm neo thực tế)
- Kiểu hệ thống: “Payment Authorization” tối giản (APPROVED/DECLINED) theo phong cách issuer.
- Kiến trúc: 1 acceptor TCP + thread pool (bounded queue) + PostgreSQL (per‑thread connection).
- An toàn/vận hành: timeouts I/O, backpressure, ghi log stderr có timestamp, script backup DB.
- Quan sát: client loadgen đa luồng đo RPS/p50/p95/p99; scripts stress/chaos.

## Lộ trình 4 giai đoạn (milestones)

## Trạng thái hiện tại (tóm tắt)
- M0: Cần tự xác nhận dưới tải bằng loadgen (chưa đánh dấu tự động trong repo).
- M1: ĐÃ HOÀN THÀNH — Idempotency theo `request_id` + Health/Readiness.
  - Tham chiếu: `server/handler.c` (parse `request_id`, health/ready), `server/db.c` (`db_insert_or_get_by_reqid`), `tests/idempotency.sh`.
- M2: ĐÃ HOÀN THÀNH — Newline framing + Keep‑alive; Structured JSON logging; Metrics counters + `/metrics`.
  - Tham chiếu: `server/handler.c` (read loop, framing, metrics), `server/log.[ch]` (JSON logs), `server/metrics.[ch]`, `tests/keepalive.sh`, README.
- M3: CHƯA LÀM — Retry/Backoff + Circuit‑breaker + Chaos script nâng cao.

### M0 — Nền tảng vững chắc (đã có, kiểm chứng lại)
- Mục tiêu: chạy E2E ổn định, giải thích luồng xử lý và backpressure.
- Phạm vi: `net.c` accept → `threadpool.c` → `handler.c` validate (Luhn/amount) → `db.c` insert → JSON response.
- Acceptance:
  - Gửi hợp lệ trả `{\"status\":\"APPROVED\"}`; sai Luhn/amount trả DECLINED + reason.
  - DB có bản ghi APPROVED với `pan_masked` dạng `######********####`.
  - Dưới tải, khi queue đầy trả `server_busy` thay vì treo/crash.
- Kiểm thử nhanh:
  - Build: `make`
  - Run: `DB_URI=... PORT=9090 ./scripts/run.sh 2>server.err & echo $! >server.pid`
  - Gửi: `./tests/send-json.sh 9090 '{\"pan\":\"4111111111111111\",\"amount\":\"10.00\"}'`
  - Load: `THREADS=8 QUEUE_CAP=2048 ./build/loadgen 50 200 9090`
  - Quan sát: `./scripts/tail-errs.sh server.err`, `DB_QUERIES.md`

### M1 — Idempotency thực chiến + Health/Readiness (core sản xuất)
- Lý do: thanh toán thực cần chống gửi lại (retry) do mạng; service cần endpoint health cho orchestration.
- Việc cần làm:
  1) Idempotency theo `request_id` (schema đã có UNIQUE):
     - Handler: parse `request_id` (nếu có). Logic: 
       - Nếu thiếu → xử lý bình thường (vẫn đúng spec tối thiểu).
       - Nếu có → BEGIN; thử INSERT với `request_id`;
         - Nếu xung đột unique → SELECT bản ghi cũ, trả response “idempotent” (trạng thái cũ).
         - Nếu ok → COMMIT và trả APPROVED.
     - DB: thêm hàm `db_insert_tx_with_reqid(...)` (hoặc mở rộng API) dùng `PQexecParams` trong transaction.
     - Response: có thể thêm `\"idempotent\":true` khi trùng (tuỳ chọn, để học logging/metrics).
  2) Healthz/Readyz:
     - Thêm cổng phụ/điều kiện đơn giản: `GET /healthz` (hoặc JSON đặc biệt trên TCP) trả OK khi process sống;
       `GET /readyz` kiểm tra `PQstatus(conn)==OK`.
- Acceptance:
  - Gửi 2 lần cùng `request_id` → lần 1 APPROVED, lần 2 không tạo bản ghi mới, phản hồi ổn định.
  - Healthz/Readyz trả trạng thái phù hợp khi DB down/up (dễ test bằng dừng Postgres).
- Kiểm thử:
  - `./tests/send-json.sh 9090 '{\"pan\":\"4111111111111111\",\"amount\":\"10.00\",\"request_id\":\"abc123\"}'`
  - Gửi lại payload y hệt → không tăng `COUNT(*)` trong `transactions`.
  - Hoặc dùng script: `DB_URI=... ./tests/idempotency.sh 9090 <req_id_optional>`

### M2 — Keep‑alive + Framing + Structured Logging + Metrics (vận hành quan sát)
- Lý do: thực tế client tái sử dụng kết nối; log phải parse được; cần chỉ số để đánh giá.
- Việc cần làm:
  1) Protocol framing newline‑delimited hoặc length‑prefixed:
     - Chọn newline‑delimited JSON (mỗi request/response kết thúc bằng `\n`).
     - Handler đọc loop nhiều request trên 1 kết nối tới khi client đóng hoặc timeout.
  2) Keep‑alive:
     - Sử dụng socket hiện tại; thiết lập timeout ngắn cho mỗi vòng đọc; nếu quá hạn → đóng.
  3) Structured logging JSON:
     - Hàm `log_message_json(level, kv...)` in ra 1 dòng JSON gồm `ts, level, request_id, status, latency_us`.
  4) Metrics tối thiểu (xuất qua STDERR hoặc cổng riêng):
     - Counters: total_requests, approved, declined, server_busy.
     - Histograms thô: ghi latency buckets (p50/p95/p99 tính ở client đã có, server có thể log percentile snapshot mỗi 10s).
- Acceptance:
  - Một kết nối gửi 5 JSON liên tiếp → server trả đủ 5 response, không rò rỉ.
  - Log JSON một dòng/req; có `request_id` nếu request cung cấp.
  - Counters tăng đúng khi chạy `./build/loadgen ...`.
  - `/metrics` trả JSON snapshot counters.
  
- Kiểm thử nhanh:
  - Keep‑alive: `./tests/keepalive.sh 9090`
  - Thủ công: `printf '{"pan":"4111111111111111","amount":"1.00"}\n{"pan":"4111111111111111","amount":"2.00"}\n' | nc 127.0.0.1 9090`
  - Metrics: `printf 'GET /metrics\r\n' | nc 127.0.0.1 9090`
  - Quan sát log JSON: `./scripts/tail-errs.sh server.err`

### M3 — Resilience mô phỏng: Retry, Backoff, Circuit‑breaker, Chaos (thực chiến chịu lỗi)
- Lý do: mô phỏng sự cố downstream và kiểm soát suy thoái (graceful degradation).
- Việc cần làm:
  1) DB retry với backoff giới hạn:
     - Trên lỗi tạm thời (e.g. `PGRES_FATAL_ERROR` với network) → retry tối đa N lần, backoff (50,100,200ms).
     - Không retry lỗi logic (unique violation cho idempotency).
  2) Circuit‑breaker thô:
     - Đếm lỗi DB trong cửa sổ thời gian; khi vượt ngưỡng → “open” trong T giây, handler trả DECLINED lý do `db_unavailable` mà không gọi DB.
  3) Chaos & áp lực:
     - Dùng `tests/chaos.sh` dừng/tạm dừng server hoặc thêm gợi ý dùng `tc netem` (comment sẵn).
- Acceptance:
  - Khi Postgres tạm mất kết nối, server không sập; sau khi DB hồi phục, tiếp tục xử lý.
  - Khi lỗi vượt ngưỡng, breaker mở (log cảnh báo), sau T giây tự đóng.

## Bài tập cụ thể theo chủ đề (mapping phỏng vấn)
- C (ngôn ngữ & bộ nhớ):
  - Viết `write_all` timeout‑aware cho keep‑alive; thêm kiểm tra `EINTR/EAGAIN`.
  - Dùng Valgrind: chứng minh không rò rỉ trong path bình thường và khi lỗi sớm.
- Cấu trúc dữ liệu & giải thuật:
  - Thêm hàng đợi lock‑free (tuỳ chọn) và so sánh với mutex/cond hiện tại (ghi nhận p95/p99).
  - Phân tích độ phức tạp thread pool khi `QUEUE_CAP` thay đổi (RPS vs drop rate).
- High‑load & Multi‑threaded:
  - So sánh config: THREADS ∈ {2,4,8,16}, QUEUE_CAP ∈ {256,1024,4096}; ghi CSV (RPS, p50, p95, p99).
- UNIX cơ bản:
  - Giới hạn FD `ulimit -n`; tạo case accept fail; đảm bảo log rõ, process không chết.
- TCP/IP networking:
  - Triển khai newline framing; test “nửa gói” (gửi từng phần qua `nc`), đảm bảo parser an toàn.
- Shell scripting:
  - Viết `scripts/bench.sh` chạy ma trận cấu hình và xuất CSV.
- SQL/PostgreSQL:
  - Thực thi idempotency; thêm index phù hợp; viết truy vấn tổng hợp ngày/giờ.

## Hướng dẫn triển khai chi tiết (gợi ý code path)
- Handler (`server/handler.c`):
  - Parse thêm `request_id` (sử dụng `parse_field`).
  - Bọc DB insert trong transaction cho idempotency: INSERT → nếu `23505` (unique_violation) → SELECT trạng thái cũ.
  - Chuẩn bị hook để tính `latency_us` cho mỗi request; gọi `log_message` JSON.
  - ĐÃ CHUYỂN sang “nhiều request/connection” bằng newline‑delimited framing; vòng lặp đọc đến khi timeout/đóng.
- DB (`server/db.c`/`db.h`):
  - Thêm hàm `db_insert_or_get_by_reqid(...)` trả mã lỗi riêng cho unique conflict.
  - Tuỳ chọn: `PQexec` set `statement_timeout` (qua `SET LOCAL`) trong transaction.
- Net (`server/net.c`):
  - Giữ nguyên acceptor + submit; có thể thêm báo cáo “queue_size/cap” vào log khi busy.
- Log (`server/log.c`):
  - ĐÃ THÊM helper in JSON 1 dòng: `{\"ts\":\"...\",\"lvl\":\"INFO\",\"event\":\"tx\",\"request_id\":\"...\",\"status\":\"APPROVED\",\"latency_us\":1234}`.
  - ĐÃ THÊM counters tối thiểu trong `server/metrics.[ch]` và endpoint `GET /metrics` trả snapshot.

## Tiêu chí hoàn thành (Definition of Done) theo milestone
- M1 DoD:
  - Idempotency chạy đúng; thêm/tái gửi cùng `request_id` không tạo bản ghi mới; có test shell minh chứng.
  - Health/Readiness có lệnh kiểm tra rõ ràng; ghi vào README.
- M2 DoD:
  - Keep‑alive+framing hoạt động với client tùy chỉnh (netcat nhiều dòng); log có cấu trúc; counters hiển thị rõ.
- M3 DoD:
  - Retry/backoff có branch cover trong log; circuit‑breaker mở/đóng như kỳ vọng dưới chaos test.

## Gợi ý đo đạc và báo cáo
- Dùng `./build/loadgen C R P` ghi kết quả ra stdout; gom lại thành CSV:
  - Cột: `threads,queue_cap,conns,reqs,rps,p50,p95,p99,reject_rate`.
- Viết `reports/RESULTS.md` tóm tắt bảng so sánh cấu hình và phân tích nút cổ chai.

## Lưu ý phỏng vấn (cách kể câu chuyện)
- Nhấn mạnh: backpressure, timeout, idempotency, per‑thread DB, structured logging/metrics, kết quả đo được (p95/p99), và kế hoạch mở rộng (epoll/keep‑alive, TLS, monitoring).
- Chia sẻ trade‑off: parser JSON tối giản vs. thư viện; 1 request/connection vs. keep‑alive; mutex DB vs. per‑thread conn.

---

Checklist bắt đầu nhanh cho bạn:
- [ ] Xác nhận M0 ổn định dưới tải vừa (loadgen 50x200).
- [x] M1: thêm `request_id` idempotency + test gửi trùng.
- [x] M1: thêm healthz/readyz đơn giản.
- [x] M2: áp dụng newline framing + keep‑alive; log JSON một dòng/req; thêm counters và `/metrics`.
- [ ] M3: retry/backoff có điều kiện + circuit‑breaker; viết chaos kịch bản.

Tham chiếu: PHONGVAN_FULL.md, HIGHLOAD_STEPS.md, HIGHLOAD_PROGRESS.md, DB_*.md, README.md.
