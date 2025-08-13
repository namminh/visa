# Hướng dẫn chuyên sâu (VN): Triển khai case khó mini‑Visa

Tài liệu này là hướng dẫn thực hành để triển khai, kiểm thử và vận hành nâng cấp “case khó mức chuyên gia” đã mô tả trong PRACTICE_PROJECT_CHUYEN_SAU.md. Mục tiêu: chạy ổn định, đo đạc được, và có lộ trình nâng cấp không downtime.

## 1) Mục tiêu & phạm vi
- Mục tiêu ngắn hạn (Pha 1): vận hành ổn định, có health/ready, metrics; kiểm thử idempotency, backpressure; benchmark p95/p99; kịch bản chaos cơ bản.
- Mục tiêu trung hạn (Pha 2): migration không downtime (thêm tenant/idempotency_key), blue/green/canary, `/version`.
- Mục tiêu dài hạn (Pha 3+): multi‑region (active‑passive rồi active‑active), outbox.

## 2) Yêu cầu môi trường
- OS: Ubuntu/Debian hoặc tương đương
- Gói: `build-essential`, `libpq-dev`, `postgresql`, `netcat`, `jq` (tuỳ chọn để parse JSON)

Ví dụ cài đặt nhanh:
```bash
sudo apt update && sudo apt install -y build-essential libpq-dev postgresql netcat jq
```

## 3) Chuẩn bị CSDL (schema hiện tại)
Schema mặc định đã có bảng `transactions` với `request_id UNIQUE`. Tạo DB và load schema:
```bash
sudo -u postgres psql -c "CREATE DATABASE mini_visa;"
sudo -u postgres psql -c "CREATE USER mini WITH PASSWORD 'mini';"
sudo -u postgres psql -d mini_visa -f db/schema.sql
```

Kết nối dùng biến `DB_URI`, ví dụ:
```
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
```

## 4) Build & chạy
Build binaries:
```bash
make
```

Chạy server (khuyến nghị qua script):
```bash
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
export PORT=9090
export THREADS=4           # điều chỉnh theo CPU
export QUEUE_CAP=1024      # điều chỉnh backpressure
./scripts/run.sh
```

Xác thực server nhận lệnh:
```bash
printf "GET /healthz\n" | nc 127.0.0.1 "$PORT"
printf "GET /readyz\n"  | nc 127.0.0.1 "$PORT"
printf "GET /metrics\n" | nc 127.0.0.1 "$PORT"
```

## 5) Kiểm thử chức năng nhanh
- Gửi 1 giao dịch mẫu:
```bash
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00","currency":"USD","merchant":"M1"}'
```
- Kiểm thử idempotency (không tạo bản ghi trùng khi gửi lại cùng `request_id`):
```bash
DB_URI="$DB_URI" ./tests/idempotency.sh 9090 idem_demo_$(date +%s)
```
- Giữ kết nối (keep‑alive) và gửi nhiều request trên 1 socket (thử framing):
```bash
./tests/keepalive.sh 9090
```

## 6) Benchmark nhanh & backpressure
- Bắn tải: 100 kết nối, mỗi kết nối 1000 request:
```bash
./build/loadgen 100 1000 "$PORT"
```
- Theo dõi metrics nhanh (đơn giản):
```bash
printf "GET /metrics\n" | nc 127.0.0.1 "$PORT"
```
- Thử backpressure: giảm `QUEUE_CAP` (ví dụ 64) và tăng tải để quan sát `server_busy` tăng trong metrics/log.

Mẹo sizing:
- I/O‑bound (nặng DB): `THREADS` ≈ số core (±) và ưu tiên latency p95/p99.
- CPU‑bound: `THREADS` ≈ số core; tránh quá cao khiến context switch nhiều.

## 7) Chaos cơ bản (an toàn)
- Tạm dừng/tiếp tục process server (mô phỏng treo ngắn):
```bash
./tests/chaos.sh
```
- Giảm/khôi phục Postgres (nếu bạn có quyền root – thận trọng):
```bash
sudo systemctl stop postgresql   # dừng ngắn để quan sát /readyz và lỗi DB
sleep 5
sudo systemctl start postgresql
```
Quan sát: `/readyz` chuyển NOT_READY khi DB down; request có thể bị DECLINED hoặc lỗi tạm thời tuỳ đường xử lý.

## 8) Migration không downtime (thêm tenant + idempotency_key)
Mục tiêu: chuyển từ `UNIQUE(request_id)` sang `UNIQUE(tenant_id, idempotency_key)` có TTL dọn rác. Cách làm tối thiểu, tương thích tiến‑lùi:

1) Thêm cột mới (nullable) và index song song (CONCURRENTLY) – không chặn ghi:
```sql
ALTER TABLE transactions ADD COLUMN tenant_id TEXT;
ALTER TABLE transactions ADD COLUMN idempotency_key TEXT;
-- Index tạm (không unique) để hỗ trợ truy vấn, tạo song song nếu DB cho phép:
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_tx_tenant ON transactions(tenant_id);
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_tx_idem   ON transactions(idempotency_key);
```

2) Backfill an toàn (ví dụ gán `tenant_id = 'default'` cho dữ liệu cũ):
```sql
UPDATE transactions SET tenant_id = 'default' WHERE tenant_id IS NULL;
UPDATE transactions SET idempotency_key = request_id WHERE idempotency_key IS NULL;
```

3) Thêm ràng buộc unique mới (chỉ khi dữ liệu đã sạch xung đột):
```sql
ALTER TABLE transactions
  ADD CONSTRAINT uq_tenant_idem UNIQUE (tenant_id, idempotency_key);
```

4) Giai đoạn chuyển tiếp trong ứng dụng:
- Server ghi cả `tenant_id` và `idempotency_key` (dual‑write nếu cần), vẫn chấp nhận `request_id` cũ.
- Đọc ưu tiên cặp `(tenant_id, idempotency_key)`; fallback `request_id` nếu thiếu (đảm bảo backward‑compat).

5) Thu dọn (khi chắc chắn client đã chuyển hết):
```sql
-- Tuỳ chính sách, có thể bỏ UNIQUE cũ (không bắt buộc nếu vẫn cần tương thích)
-- ALTER TABLE transactions DROP CONSTRAINT transactions_request_id_key;
```

Ghi chú: Nếu khối lượng dữ liệu lớn, dùng batch nhỏ và theo dõi lock; luôn thử trước ở môi trường staging.

## 9) Blue/Green + kiểm chứng phiên bản
- Thêm endpoint phiên bản (gợi ý): `GET /version` trả `version, schema_version, build_time` (cập nhật trong mã nguồn).
- Quy trình triển khai:
  1) Deploy phiên bản mới song song (green), chỉ route 1–5% traffic.
  2) Giám sát p95/p99, error_rate, `server_busy`, log ERROR/WARN.
  3) Tăng dần traffic; rollback ngay nếu có dấu hiệu xấu.

## 10) Multi‑region (mô phỏng tối thiểu)
- Active‑passive: chạy 2 instance server (A/B) trỏ cùng DB; B tạm coi là “standby”. Mô phỏng failover: tắt A, traffic chuyển sang B.
- Theo dõi: thời gian khôi phục (RTO), tỉ lệ lỗi khi chuyển tải. Với DB đơn vùng bạn chỉ kiểm chứng app‑level; để đủ RPO/RTO cần replica.

## 11) Quan sát & vận hành
- Log có cấu trúc (1 dòng/request): `ts,lvl,event,request_id,status,reason,latency_us,thread_id`…
- Metrics (`GET /metrics`): tối thiểu có `total, approved, declined, server_busy`. Mở rộng dần theo nhu cầu.
- Cảnh báo: p95 tăng đột ngột, `server_busy` tăng nhanh, `/readyz` fail liên tiếp.

## 12) Checklist xác nhận
- Build thành công và server chạy với `DB_URI, PORT, THREADS, QUEUE_CAP`.
- `GET /healthz` trả OK; `GET /readyz` OK khi DB sẵn sàng; `GET /metrics` có số liệu.
- Idempotency: gửi trùng `request_id` không tăng `COUNT(*)` trong `transactions`.
- Backpressure: khi `QUEUE_CAP` nhỏ và tải cao, có `server_busy` (không crash/hang).
- Migration: áp dụng tuần tự mục 8 trên staging không downtime; dữ liệu nhất quán.

## 13) Phụ lục lệnh nhanh
```bash
make clean && make                 # build sạch
PORT=9090 ./scripts/run.sh         # chạy server
./build/loadgen 50 500 9090        # tải nhẹ
./tests/idempotency.sh 9090 idemX  # test idempotency
printf "GET /metrics\n" | nc 127.0.0.1 9090 | jq -r .  # xem metrics (nếu là JSON)
```

—

Gợi ý: Sau khi nắm vững hướng dẫn này, hãy quay lại PRACTICE_PROJECT_CHUYEN_SAU.md, mục 16 và 17, để lập kế hoạch chi tiết từng pha (rate limiting theo tenant, breaker v2, migration online, multi‑region).

