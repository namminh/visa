# Hướng dẫn test nâng cao (Mini‑Visa)

Tài liệu này mô tả các kịch bản test theo PRACTICE_PROJECT_CHUYEN_SAU.md, bám sát hệ thống hiện tại: TCP JSON, idempotency, metrics, risk‑velocity, endpoints vận hành.

## 0) Chuẩn bị môi trường
- Thiết lập DB và build
  - `export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"`
  - `make clean && make`
- Chạy server (ví dụ cổng 9090)
  - `PORT=9090 THREADS=4 QUEUE_CAP=1024 ./scripts/run.sh`
  - Theo dõi stderr: `./scripts/tail-errs.sh server.err` (nếu chạy background)

## 1) Smoke: health, ready, version
- Health: `printf "GET /healthz\n" | nc 127.0.0.1 9090` → `OK`
- Ready (DB): `printf "GET /readyz\n" | nc 127.0.0.1 9090` → `OK` (khi DB kết nối được)
- Phiên bản: `printf "GET /version\n" | nc 127.0.0.1 9090` → `{"version":"…","schema":…}`

## 2) Chức năng cơ bản APPROVED/DECLINED
- Hợp lệ (Luhn + amount):
  - `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'`
  - Kỳ vọng: `{"status":"APPROVED"}`
- Sai Luhn:
  - `./tests/send-json.sh 9090 '{"pan":"4111111111111112","amount":"10.00"}'`
  - Kỳ vọng: `{"status":"DECLINED","reason":"luhn_failed"}`
- Sai amount (âm/quá lớn):
  - `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"-5"}'`
  - Kỳ vọng: `{"status":"DECLINED","reason":"amount_invalid"}`
- Kiểm tra DB (với giao dịch APPROVED):
  - `psql "$DB_URI" -c 'SELECT id, pan_masked, amount, status, request_id, created_at FROM transactions ORDER BY id DESC LIMIT 5;'`
  - Kỳ vọng: `pan_masked` dạng `######********####`, `status=APPROVED`.

## 3) Idempotency theo request_id
- Gửi 2 lần cùng `request_id`:
  - `RID=abc123; ./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00","request_id":"'"$RID"'"}'`
  - Gửi lại: `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00","request_id":"'"$RID"'"}'`
  - Lần 2 có thể trả `{"idempotent":true}`; DB không tăng `COUNT(*)` cho `request_id` đó.
- Xác minh DB:
  - `psql "$DB_URI" -c "SELECT request_id, count(*) FROM transactions WHERE request_id='${RID}' GROUP BY request_id;"`

## 4) Keep‑alive + framing dòng
- Một kết nối nhiều dòng JSON: `./tests/keepalive.sh 9090`
- Kỳ vọng: mỗi dòng request nhận 1 dòng response; không treo, không rò rỉ.

## 5) Metrics snapshot
- `printf "GET /metrics\n" | nc 127.0.0.1 9090`
- Kỳ vọng JSON có: `total, approved, declined, server_busy, risk_declined`
- So sánh trước/sau khi gửi request để thấy số đếm tăng hợp lý.

## 6) Risk: velocity limit theo PAN (env)
- Bật rule và giới hạn 1 yêu cầu/mỗi 60s cho cùng PAN:
  - `export RISK_ENABLED=1 RISK_VEL_LIMIT=1 RISK_VEL_WINDOW_SEC=60`
  - Khởi động lại server nếu cần.
- Gửi 2 yêu cầu liên tiếp cùng PAN hợp lệ:
  - Lần 1: `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'` → APPROVED
  - Lần 2: `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'` → DECLINED `risk_velocity`
- Kiểm tra metrics: `risk_declined` tăng và `declined` tăng tương ứng.

## 7) Server busy (backpressure)
- Giảm tài nguyên để dễ tái hiện:
  - `PORT=9090 THREADS=1 QUEUE_CAP=1 ./scripts/run.sh`
- Tạo nhiều kết nối đồng thời (vd: trong shell khác chạy vòng lặp 50 lần):
  - `for i in $(seq 1 50); do ./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"1"}' & done; wait`
- Kỳ vọng: một số request trả `{"reason":"server_busy"}`; `/metrics` cho thấy `server_busy` tăng.

## 8) Readiness theo DB
- Khi DB không sẵn sàng (ví dụ tắt Postgres tạm thời), `GET /readyz` trả `NOT_READY`.
- Sau khi DB phục hồi, `GET /readyz` trở lại `OK` và xử lý giao dịch bình thường.

## 9) Chaos/Soak (tuỳ chọn)
- Chaos nhẹ: `tests/chaos.sh` (nếu script yêu cầu, điều chỉnh cho phù hợp môi trường)
- Soak ngắn: chạy 5–10 phút với vòng lặp gửi request, quan sát log và rò rỉ (sử dụng `scripts/bench.sh` nếu đã cấu hình).

## 10) Tiêu chí chấp nhận nhanh
- Build/run ổn định; health/ready OK.
- Luhn/amount hoạt động; DB ghi `pan_masked` đúng.
- Idempotency không tạo bản ghi trùng; response có `idempotent=true` khi hit.
- Keep‑alive hoạt động; không rò rỉ/treo.
- `/metrics` có `risk_declined`; khi bật risk‑velocity thì số này tăng đúng.
- Có thể tái hiện `server_busy` khi queue nhỏ và tải đồng thời.

## 11) Ghi chú
- Các mô‑đun `ledger` và `clearing` hiện ở mức stub để mở rộng theo lộ trình 18.x; chưa có test E2E riêng.
- Khi cần đo p50/p95/p99 và ma trận cấu hình, xem `scripts/bench.sh` và tài liệu `PRACTICE_PROJECT_CHUYEN_SAU.md`.

