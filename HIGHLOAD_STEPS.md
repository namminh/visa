# Hướng dẫn từng bước: Ứng dụng đa luồng và kiểm thử tải

Tài liệu này liệt kê tuần tự các bước thực hiện để biến mini-visa thành dịch vụ chịu tải tốt, kèm lệnh chạy và cách quan sát.

## Bước 1: E2E cơ bản (đã có)
- Mục tiêu: server nhận JSON, kiểm tra Luhn/amount, ghi DB, trả JSON.
- Chạy:
```bash
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
make clean && make
PORT=9090 ./scripts/run.sh
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'
```
- Quan sát DB: xem 5 bản ghi mới nhất (APPROVED)
```bash
psql "$DB_URI" -At -F ' | ' -c "SELECT id, pan_masked, amount, status, to_char(created_at,'YYYY-MM-DD HH24:MI:SS') FROM transactions ORDER BY id DESC LIMIT 5;"
```

## Bước 2: Thread pool + Backpressure (đã có)
- Mục tiêu: nhiều worker xử lý song song; khi hàng đợi đầy → từ chối sớm.
- Cấu hình:
```bash
export THREADS=8 QUEUE_CAP=2048
```
- Chạy server + theo dõi lỗi:
```bash
PORT=9090 ./scripts/run.sh 2>server.err & echo $! >server.pid
./scripts/tail-errs.sh server.err
```
- Kiểm thử tải (script đơn giản):
```bash
CONNS=50 REQS=200 PORT=9090 ./tests/stress.sh
```
- Kỳ vọng: server không crash; khi đầy tải có trả `server_busy` rồi đóng.

## Bước 3: Kết nối DB theo luồng (đã có)
- Mục tiêu: giảm tranh chấp khi ghi DB; mỗi worker dùng kết nối riêng.
- Đã cài đặt qua `db_thread_get(...)` trong `server/db.c` và handler dùng nó.
- Kiểm thử lại với tải ở bước 2; kỳ vọng throughput ổn định hơn.

## Bước 4: Loadgen đo chỉ số (đã có)
- Mục tiêu: đo tổng quan hiệu năng.
- Dùng loadgen mới (đa luồng + đo latency per request):
```bash
./build/loadgen 50 200 9090
# Output mẫu: sent_ok=10000, sent_err=0, wall=1.234s, RPS=8105.45, p50=800us, p95=2.1ms, p99=3.4ms
```
- Diễn giải:
  - RPS = số yêu cầu thành công / tổng thời gian (giây).
  - p50/p95/p99 = độ trễ phần trăm vị (micro giây) của một yêu cầu (kết nối+gửi+nhận).

## Bước 5: Timeouts & Độ bền (đã có mức cơ bản)
- Server: đặt timeout đọc/ghi 5s (`SO_RCVTIMEO/SO_SNDTIMEO`) để tránh treo.
- Backpressure: khi hàng đợi đầy → trả JSON `server_busy`.
- Kế tiếp: thêm `statement_timeout` cho DB (tùy ý trong `db_connect`).

## Bước 6: Theo dõi & gỡ rối
- Log: `./scripts/tail-errs.sh server.err` để xem lỗi/bất thường.
- DB: `DB_QUERIES.md` có sẵn lệnh xem dữ liệu, thống kê.
- Bộ nhớ/FD: dùng `htop`, `ulimit -n` nếu cần mở rộng.

## Bước 7: Mở rộng (tùy chọn)
- Logging có `request_id`, đo thời gian xử lý server.
- Parser JSON chắc chắn (thay vì parser tự chế).
- Cấu hình qua CLI (getopt) ngoài biến môi trường.
- Thêm quy trình AUTH/CAPTURE/REFUND và idempotency (xem `VISA_FLOW.md`).

Gợi ý: cập nhật `HIGHLOAD_PROGRESS.md` sau mỗi thay đổi để lưu lại quyết định kỹ thuật và kết quả đo.
