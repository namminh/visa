# Mini‑Visa: Giải thích bằng ngôn ngữ tự nhiên (VN)

Tài liệu này giúp bạn hiểu nhanh toàn bộ dự án mini‑visa theo cách gần gũi, dễ đọc, dựa trên nội dung gốc trong `PRACTICE_PROJECT.md`.

## 1) Dự án này làm gì?
- Đây là một “cổng thanh toán mini” để luyện phỏng vấn, mô phỏng hệ thống xác thực giao dịch. Bạn gửi yêu cầu thanh toán dạng JSON, hệ thống kiểm tra và trả về `APPROVED` (chấp nhận) hoặc `DECLINED` (từ chối).
- Mục tiêu học tập: làm quen với hệ thống backend có TCP server, đa luồng, kết nối DB, idempotency, logging có cấu trúc, metrics, và các kỹ thuật chịu lỗi (retry, circuit breaker).

## 2) Hệ thống hoạt động thế nào?
- Client kết nối TCP tới server, gửi JSON mỗi dòng (newline‑delimited). Ví dụ một yêu cầu:
  `{"pan":"4111111111111111","amount":"10.00","request_id":"abc123"}`
- Server đọc request, kiểm tra hợp lệ (Luhn của số thẻ, số tiền), gọi DB để lưu và quyết định, rồi trả JSON kết quả (APPROVED/DECLINED) theo từng dòng.

## 3) Kiến trúc tổng thể (đơn giản hoá)
- `acceptor TCP` nhận kết nối → đẩy công việc vào `bounded queue` → `thread pool` lấy việc ra xử lý.
- Mỗi worker có kết nối PostgreSQL riêng (per‑thread connection) để giảm lock chung.
- Cơ chế vận hành an toàn:
  - Timeout I/O: tránh treo khi client/DB chậm.
  - Backpressure: khi hàng đợi đầy, từ chối nhẹ nhàng (trả `server_busy`) thay vì treo/crash.
  - Logging stderr có timestamp; có scripts tiện theo dõi và sao lưu DB.
- Quan sát: có chương trình bắn tải (loadgen) để đo thông lượng (RPS) và độ trễ p50/p95/p99; có counters và `/metrics`.

## 4) Luồng xử lý chi tiết (từ kết nối đến phản hồi)
1) `net.c` chấp nhận kết nối và tạo socket.
2) Đẩy “công việc” vào hàng đợi (queue) có giới hạn.
3) `threadpool.c` có các worker chờ sẵn; mỗi worker lấy việc ra và gọi `handler.c`.
4) `handler.c`:
   - Parse JSON (pan, amount, request_id nếu có).
   - Kiểm tra: số thẻ qua Luhn, số tiền hợp lệ.
   - Gọi `db.c` để ghi/đọc DB theo logic idempotency.
   - Ghi log JSON một dòng (ts, level, request_id, status, latency_us).
   - Trả kết quả JSON về client, mỗi request/response kết thúc bằng newline (`\n`).

## 5) Các mốc học tập (Milestones)

### M0 — Nền tảng ổn định
- Mục tiêu: chạy end‑to‑end trơn tru, đúng dữ liệu, không sập khi có tải.
- Kết quả mong đợi:
  - Gửi hợp lệ: trả `{"status":"APPROVED"}`; sai Luhn/amount: `DECLINED` kèm lý do.
  - DB có bản ghi APPROVED với `pan_masked` dạng `######********####`.
  - Khi queue đầy: trả `server_busy` (backpressure) thay vì treo/crash.

### M1 — Idempotency + Health/Readiness
- Idempotency theo `request_id`: gửi lại cùng `request_id` không tạo bản ghi mới; trả trạng thái cũ.
- Health/Readiness: endpoint để kiểm tra tiến trình sống và tình trạng kết nối DB.
- Lợi ích: chống tác dụng phụ khi client retry; phục vụ orchestrator/monitoring.

### M2 — Keep‑alive, Framing, Structured Logging, Metrics
- Keep‑alive: một kết nối có thể gửi nhiều request; server đọc trong vòng lặp, đến khi client đóng hoặc timeout.
- Framing: mỗi request/response là một dòng JSON (newline‑delimited) → dễ ghép nhiều yêu cầu.
- Logging JSON 1 dòng/req: dễ parse, dễ đưa vào hệ thống log.
- Metrics: counters tối thiểu (total_requests, approved, declined, server_busy) và `/metrics` trả snapshot.

### M3 — Chịu lỗi nâng cao: Retry + Circuit Breaker + Chaos
- Retry có backoff: thử lại thao tác DB khi lỗi tạm thời (50ms → 100ms → 200ms). Không retry lỗi logic (như unique_violation của idempotency).
- Circuit breaker: nếu lỗi DB tăng cao, “mở cầu dao” một thời gian T để fail‑fast (trả `db_unavailable`) thay vì dồn thêm áp lực xuống DB; sau T giây chuyển half‑open để “thăm dò”.
- Chaos: kịch bản gây lỗi để kiểm tính ổn định (dừng/tạm dừng server, làm mạng chập chờn...).

## 6) Cách chạy và kiểm thử nhanh
- Build: `make`
- Chạy server:
  - `DB_URI=... PORT=9090 ./scripts/run.sh 2>server.err & echo $! >server.pid`
- Gửi yêu cầu mẫu:
  - `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'`
- Bắn tải:
  - `THREADS=8 QUEUE_CAP=2048 ./build/loadgen 50 200 9090`
- Giữ kết nối (keep‑alive) kiểm nhanh:
  - `./tests/keepalive.sh 9090`
  - Hoặc: `printf '{"pan":"4111111111111111","amount":"1.00"}\n{"pan":"4111111111111111","amount":"2.00"}\n' | nc 127.0.0.1 9090`
- Xem metrics:
  - `printf 'GET /metrics\r\n' | nc 127.0.0.1 9090`
- Quan sát log JSON:
  - `./scripts/tail-errs.sh server.err`

## 7) Idempotency (giải thích dễ hiểu)
- Vấn đề: Client có thể gửi lại cùng một giao dịch vì mạng lỗi. Nếu server “làm lại” sẽ tạo bản ghi trùng, gây nhầm lẫn.
- Cách giải: Mỗi request có `request_id` duy nhất. Server INSERT với `request_id`; nếu bị trùng (unique_violation), thay vì tạo thêm bản ghi, server lấy lại bản ghi cũ rồi trả đúng trạng thái cũ cho client.
- Lợi ích: Gửi lặp không làm thay đổi kết quả và dữ liệu; an toàn khi có retry.

## 8) Keep‑alive + Framing (vì sao quan trọng)
- Không phải mỗi yêu cầu phải mở kết nối mới; giữ kết nối giúp giảm chi phí TCP và tăng hiệu năng.
- Newline‑delimited JSON giúp server biết ranh giới yêu cầu: mỗi dòng là một request/response.
- Có timeout cho mỗi vòng đọc/ghi để tránh “treo” kết nối vô thời hạn.

## 9) Logging có cấu trúc + Metrics
- Mỗi request log một dòng JSON: `ts, lvl, event, request_id, status, latency_us` → dễ ingest vào ELK, Loki,…
- Counters cơ bản: tổng request, số APPROVED, DECLINED, SERVER_BUSY; có thể bổ sung histogram thô cho latency.
- `/metrics` trả ảnh chụp counters để công cụ thu thập số liệu đọc.

## 10) Retry + Circuit Breaker (chịu lỗi nâng cao)
- Retry: chỉ dùng khi lỗi tạm thời; có backoff tăng dần và jitter (ngẫu nhiên nhỏ) để tránh “dồn sóng”.
- Circuit breaker:
  - Closed: hoạt động bình thường.
  - Open: lỗi vượt ngưỡng → cắt gọi xuống DB trong T giây, trả `db_unavailable` nhanh.
  - Half‑open: hết T → cho vài request thăm dò; nếu OK thì đóng lại.
- Kết hợp: trước khi breaker mở, cho phép retry có kiểm soát; khi breaker mở, không retry nữa → fail‑fast.

## 11) Bài tập theo chủ đề (gợi ý phù hợp phỏng vấn)
- C/Memory: viết `write_all` có timeout, xử lý `EINTR/EAGAIN`; dùng Valgrind kiểm rò rỉ cả đường thành công và lỗi sớm.
- Concurrency/DSA: thử hàng đợi lock‑free và so sánh với mutex/cond hiện tại (đo p95/p99).
- High‑load: benchmark ma trận THREADS ∈ {2,4,8,16}, QUEUE_CAP ∈ {256,1024,4096}; ghi CSV: `threads,queue_cap,conns,reqs,rps,p50,p95,p99,reject_rate`.
- UNIX/TCP: giới hạn fd `ulimit -n`, ép accept fail; test “nửa gói” với `nc` để kiểm parser an toàn.
- Shell: viết `scripts/bench.sh` chạy ma trận cấu hình và gom kết quả.
- SQL: thực thi idempotency bằng UNIQUE + transaction; thêm index phù hợp; viết truy vấn tổng hợp theo ngày/giờ.

## 12) Definition of Done (DoD) theo milestone
- M1: Idempotency chạy đúng (test shell chứng minh); Health/Ready rõ ràng, có hướng dẫn trong README.
- M2: Keep‑alive + newline‑framing hoạt động (thử được bằng netcat); log 1 dòng/req; counters chính xác; `/metrics` trả được snapshot.
- M3: Có log nhánh retry/backoff; circuit‑breaker mở/đóng đúng kỳ vọng trong bài test chaos.

## 13) Cách kể câu chuyện trong phỏng vấn
- Nhấn mạnh: backpressure, timeout, idempotency, per‑thread DB, structured logging/metrics, kết quả đo p95/p99, và kế hoạch mở rộng (epoll/keep‑alive, TLS, monitoring).
- Nêu trade‑off: parser JSON tự viết vs. thư viện; 1 request/connection vs. keep‑alive; mutex bảo vệ DB vs. per‑thread connection.

## 14) Checklist nhanh
- [ ] Xác nhận M0 ổn định dưới tải vừa (loadgen 50×200).
- [x] M1: thêm `request_id` idempotency + test gửi trùng.
- [x] M1: thêm healthz/readyz đơn giản.
- [x] M2: newline framing + keep‑alive; log JSON một dòng/req; counters và `/metrics`.
- [ ] M3: retry/backoff có điều kiện + circuit‑breaker; viết kịch bản chaos.

## 15) Tham chiếu hữu ích trong repo
- `server/handler.c`: parse `request_id`, vòng lặp đọc nhiều request, cập nhật metrics.
- `server/db.c`/`server/db.h`: `db_insert_or_get_by_reqid(...)`, transaction, xử lý unique conflict.
- `server/log.[ch]`: helper in JSON 1 dòng.
- `server/metrics.[ch]`: counters và endpoint `/metrics`.
- `tests/idempotency.sh`, `tests/keepalive.sh`: kịch bản kiểm nhanh.
- `DB_QUERIES.md`, `HIGHLOAD_STEPS.md`, `HIGHLOAD_PROGRESS.md`, `README.md`.

