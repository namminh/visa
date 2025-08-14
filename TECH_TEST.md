# Mini‑Visa — Đề Bài Tech Test (VN)

Tài liệu này định nghĩa bài tập kỹ thuật (tech test) dựa trên repo mini‑visa. Mục tiêu: đánh giá khả năng thiết kế, hiện thực, vận hành, và giải thích trade‑offs như khi làm hệ thống thực tế hiệu năng cao.

## 1) Mục tiêu & phạm vi
- Xây một dịch vụ TCP newline‑delimited JSON (đã có khung trong repo) xử lý giao dịch đơn giản: validate Luhn, amount; ghi DB; phản hồi `APPROVED/DECLINED`.
- Nâng cấp theo các chủ đề cốt lõi (bên dưới) để chứng minh hiểu biết về: backpressure, idempotency, retry, circuit breaker, risk velocity, metrics/logging, readiness.
- Thời lượng gợi ý: 4–8 giờ (chọn 1 track phù hợp năng lực/thời gian).

## 2) Chuẩn bị môi trường (tham khảo)
- PostgreSQL chạy cục bộ; DB/schema có sẵn:
  - `export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"`
  - `psql "$DB_URI" -f db/schema.sql`
- Build & chạy:
  - `make clean && make`
  - `PORT=9090 THREADS=4 QUEUE_CAP=1024 ./scripts/run.sh`
- Kiểm nhanh:
  - `printf "GET /healthz\n" | nc 127.0.0.1 9090` → `OK`
  - `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'`

## 3) Nhiệm vụ theo track
Chọn 1 trong 3 track. Hoàn thành càng nhiều mục bắt buộc càng tốt; có thể làm thêm mục “Bonus”.

### Track A (2–3h): Cơ bản vững
- Framing newline + partial read/write + timeout: xác nhận keep‑alive hoạt động (`tests/keepalive.sh`).
- Validation: Luhn/amount chính xác; mask PAN; chỉ `APPROVED` mới ghi DB.
- Idempotency: `UNIQUE(request_id)` + `INSERT ... ON CONFLICT DO NOTHING` + đọc lại status; trả `idempotent=true` khi trùng.
- `/healthz`, `/readyz`, `/metrics`, `/version`: phản hồi chính xác theo trạng thái.

### Track B (4–6h): Độ tin cậy & vận hành
- Retry có kiểm soát cho lỗi DB tạm thời: exponential backoff (50→100→200ms) + jitter ±20ms; `max_attempts ≤ 3` và `max_elapsed ≤ 300ms`.
- Circuit breaker v1 quanh đường DB:
  - Closed→Open khi lỗi liên tiếp > N (ví dụ 5) trong cửa sổ ngắn; Open trong 2s; Half‑open thử 1 yêu cầu.
  - Khi Open: không gọi DB, trả `DECLINED {reason: db_unavailable}`.
  - Ghi `breaker_opened`, `consecutive_failures` vào metrics/log.
- Risk velocity theo PAN (env): `RISK_ENABLED, RISK_VEL_LIMIT, RISK_VEL_WINDOW_SEC` (đã có khung) — chứng minh bằng test và metrics `risk_declined`.
- Bổ sung snapshot `/metrics`: `total, approved, declined, server_busy, retry_attempts, breaker_opened, risk_declined`.

### Track C (6–8h): Quan sát & áp lực tải
- Backpressure: chứng minh `server_busy` khi `QUEUE_CAP` nhỏ; đo p50/p95/p99 với `scripts/bench.sh` và viết kết luận 3–5 dòng.
- Logging có cấu trúc một dòng/log: tối thiểu `ts,lvl,event,request_id,status,reason,latency_us,thread_id,queue_size,queue_cap`.
- Chaos nhẹ: tắt Postgres 10–30s → breaker mở; bật lại → Half‑open → Closed; hệ thống phục hồi (log/metrics thể hiện rõ).
- Viết 1–2 test script tự động cho idempotency/keep‑alive/breaker (bash hoặc C test nhỏ) và thêm vào `tests/run_all.sh`.

## 4) Yêu cầu đầu ra (Deliverables)
- Mã nguồn: thay đổi nhỏ gọn, nhất quán phong cách repo. Không thêm framework nặng.
- Hướng dẫn chạy: `TECH_TEST_REPORT.md` (ngắn gọn 1–2 trang) mô tả:
  - Cách build/run; biến môi trường dùng; cách tái hiện kịch bản test.
  - Thiết kế ngắn: vì sao chọn ngưỡng breaker/retry; trade‑offs; hạn chế còn lại.
  - Số đo chính (nếu làm Track C): `rps,p50,p95,p99,reject_rate` cho 2–3 cấu hình.
- Log/metrics minh chứng:
  - Ảnh chụp hoặc trích `GET /metrics` trước/sau; trích 3–5 dòng log tiêu biểu.
- Test scripts (nếu có): thêm vào `tests/` + cập nhật `tests/run_all.sh`.

## 5) Tiêu chí chấm điểm (100 điểm)
- Đúng chức năng (25đ): Luhn/amount, keep‑alive, DB ghi `APPROVED`, idempotency hoạt động, health/ready/metrics/version đúng.
- Độ tin cậy (30đ): Retry giới hạn, breaker v1 hoạt động (Open/Half‑open/Closed), backpressure trả `server_busy` đúng lúc.
- Vận hành/quan sát (20đ): Logging một dòng có trường cốt lõi; `/metrics` đầy đủ counters; minh chứng bằng số đo hoặc ảnh chụp.
- Chất lượng mã (15đ): Đơn giản, rõ ràng, xử lý lỗi đầy đủ, không rò rỉ tài nguyên; nhất quán với codebase C hiện tại.
- Tài liệu & test (10đ): `TECH_TEST_REPORT.md` ngắn gọn, tái hiện được; test scripts chạy được.

Bonus (+10đ tối đa): benchmark ma trận nhỏ và phân tích; chaos test tự động; cải tiến `metrics` theo bucket hoặc thêm `/metrics` JSON chi tiết an toàn.

## 6) Tiêu chí chấp nhận (Acceptance)
- `make` thành công; server chạy bằng `./scripts/run.sh` với `DB_URI` hợp lệ.
- `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'` → `APPROVED`; DB có `pan_masked` dạng `######********####`.
- Gửi trùng `request_id`: không tăng `COUNT(*)` và (tùy chọn) `idempotent=true` trong response.
- `/readyz` phản ánh trạng thái DB; `/metrics` có `total,approved,declined,server_busy` và counters bạn bổ sung.
- Khi queue đầy (ví dụ `THREADS=1 QUEUE_CAP=1` + 50 yêu cầu đồng thời): xuất hiện một phần `server_busy` hợp lý.
- Nếu làm breaker: khi tắt DB ngắn, `/readyz` không OK; giao dịch trả `db_unavailable`; sau bật DB, xử lý bình thường.

## 7) Gợi ý kiểm thử (tham khảo nhanh)
- Health/Ready/Version: `printf "GET /healthz\n" | nc 127.0.0.1 9090`, tương tự `/readyz`, `/version`.
- Keep‑alive: `./tests/keepalive.sh 9090`.
- Idempotency: `tests/idempotency.sh` hoặc lặp `request_id` cố định 2 lần; xác minh DB.
- Metrics: `printf "GET /metrics\n" | nc 127.0.0.1 9090` (chụp trước/sau khi gửi yêu cầu).
- Backpressure: giảm `THREADS/QUEUE_CAP` và bắn đồng thời (`for i in $(seq 1 50) ...`).
- Breaker: tắt Postgres tạm thời → quan sát log/metrics và response.

## 8) Giới hạn & lưu ý
- Không yêu cầu triển khai TLS/HTTP; chỉ TCP + JSON newline.
- Không cần JSON lib đầy đủ; tiny parser trong repo là đủ cho test.
- Giữ thay đổi gọn, tập trung vào chủ đề đánh giá; tránh “vàng mã” (over‑engineering).

## 9) Cách nộp bài
- Tạo nhánh hoặc fork, đẩy thay đổi; đính kèm `TECH_TEST_REPORT.md`.
- Nêu rõ cách chạy nhanh (lệnh copy‑paste) và cách tái hiện các minh chứng.
- Tuỳ chọn: đính kèm tệp log/ảnh chụp metrics (nếu khó tái hiện RPS cao trên máy chấm).

## 10) Rubric tham chiếu nhanh cho người chấm
- Smoke: build/run/health/ready OK.
- Core: APPROVED/DECLINED đúng, DB ghi/mask chuẩn, idempotency không nhân bản ghi.
- Ops: `/metrics` có counters yêu cầu; log structured; backpressure hiển thị `server_busy`.
- Resilience: retry có giới hạn; breaker hoạt động đúng trạng thái.
- Docs/Tests: có REPORT ngắn, có scripts kiểm thử hoặc hướng dẫn rõ ràng; số đo/ảnh chụp đủ chứng minh.

Tham khảo chi tiết: `PRACTICE_PROJECT_CHUYEN_SAU.md`, `TEST_HUONGDAN_CHUYEN_SAU.md`, `DB_PERMISSIONS.md`, `DB_QUERIES.md`.

