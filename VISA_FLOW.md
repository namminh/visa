# Mô phỏng luồng VISA (phiên bản tối thiểu, tiếng Việt)

Tài liệu này mô tả cách ánh xạ quy trình duyệt/khước từ giao dịch (authorization) theo phong cách VISA vào project, kèm hướng dẫn test và theo dõi kết quả cụ thể.

## Phạm vi (E2E tối thiểu)
- Tác vụ: duyệt (APPROVED) hoặc từ chối (DECLINED) giao dịch.
- Giao vận: TCP, mỗi kết nối gửi 1 JSON (newline không bắt buộc).
- Quy tắc quyết định (issuer giả lập):
  - `pan` phải đạt kiểm tra Luhn.
  - `amount` > 0 và <= 10000.00.
  - Sai quy tắc → DECLINED + lý do; hợp lệ → APPROVED.
- Lưu trữ: ghi `pan_masked` (che số giữa), `amount`, `status` vào bảng `transactions`.
- Phản hồi: JSON `{"status":"APPROVED"}` hoặc `{ "status":"DECLINED", "reason":"..." }`.

## Giao thức
Ví dụ request (một request trên một kết nối):
```json
{"pan":"4111111111111111","amount":"10.00","currency":"USD","merchant":"M1","request_id":"abc123"}
```
Ví dụ response:
```json
{"status":"APPROVED"}
```
Ghi chú:
- Hiện handler chỉ cần `pan` và `amount`. Các trường khác để dành cho mở rộng (idempotency, chống gian lận,...).
- Che số thẻ (masking): giữ 6 số đầu + 4 số cuối, phần giữa thay bằng `*`.

## Thành phần liên quan trong mã
- `server/net.c`: TCP server, lắng nghe và gọi handler (tạm xử lý nội tuyến, chưa dùng thread pool).
- `server/handler.c`: đọc JSON, validate (Luhn/amount), che PAN, ghi DB, trả JSON kết quả.
- `server/db.c`: kết nối PostgreSQL, chèn vào `transactions`.
- `client/loadgen.c`: còn stub; dùng `tests/send-json.sh` để test thủ công.

## Cách chạy nhanh (demo E2E)
1) Chuẩn bị DB + build
```bash
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
make clean && make
PORT=9090 ./scripts/run.sh
```
2) Gửi request hợp lệ/không hợp lệ
```bash
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'   # APPROVED
./tests/send-json.sh 9090 '{"pan":"4111111111111112","amount":"10.00"}'   # DECLINED (luhn)
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"-5"}'      # DECLINED (amount)
```
3) Xem kết quả trong DB
```bash
psql "$DB_URI" -c 'SELECT id, pan_masked, amount, status, created_at FROM transactions ORDER BY id DESC LIMIT 5;'
```
4) Theo dõi log/lỗi (tuỳ chọn)
- Chạy server foreground để xem stderr trực tiếp.
- Hoặc chuyển stderr ra file và tail lỗi:
```bash
PORT=9090 ./scripts/run.sh 2>server.err & echo $! >server.pid
./scripts/tail-errs.sh server.err
```
5) Tắt server
```bash
kill $(cat server.pid)
```

## Hướng dẫn test chi tiết theo bước
- Kiểm tra Luhn:
  - Gửi PAN hợp lệ (4111111111111111) → `APPROVED`.
  - Gửi PAN sai (thay số cuối cùng) → `DECLINED` với `reason:luhn_failed`.
- Kiểm tra amount:
  - `amount` âm/0 hoặc quá lớn >10000 → `DECLINED` với `reason:amount_invalid`.
- Kiểm tra ghi DB:
  - Sau mỗi giao dịch `APPROVED`, chạy truy vấn ở mục 3) để thấy `pan_masked`/`amount`/`status`.
- Thử “áp lực nhẹ” (nhiều request tuần tự):
  - Gửi 10–20 lần lặp lệnh `send-json.sh` và quan sát DB tăng bản ghi.
- Thử client đóng sớm:
  - Dùng `nc 127.0.0.1 9090` rồi đóng ngay; server không được crash.

## Kỳ vọng đầu ra khi test
- Terminal khi gửi hợp lệ: không in lỗi; server trả `{"status":"APPROVED"}`.
- Terminal khi gửi không hợp lệ: server trả `{"status":"DECLINED","reason":"..."}`.
- DB: có bản ghi mới với `pan_masked` dạng `######********####`, `status` = `APPROVED`.
- Log stderr server: dòng như `db_connect ok`, lỗi rõ ràng nếu có sự cố bind/accept/DB.

## Áp dụng vào bài học (gợi ý lộ trình)
1) Nền tảng mạng: hiểu vòng đời socket (bind/listen/accept), kiểm thử với `nc`/script.
2) Business rule cơ bản: Luhn + amount → cách “quyết định” (APPROVED/DECLINED) và phản hồi JSON.
3) Che số nhạy cảm: thực hành masking và lưu trữ thông tin tối thiểu.
4) Tương tác DB: chèn bản ghi an toàn (dùng `PQexecParams`).
5) Quan sát: dựng quy trình tái tạo sự cố (DB sai, port bận), đọc log nhanh với `tail-errs.sh`.
6) Kiến thức mở rộng (sau này): idempotency, state machine auth/capture/refund, backpressure/thread pool, timeout/retry, logging có cấu trúc, load test.

## Bài tập nâng cao (tuỳ chọn)
- Idempotency: thêm `request_id` unique trong schema, logic handler trả kết quả cũ nếu gửi trùng.
- Trạng thái: thêm cột `type` (AUTH/CAPTURE/REFUND/REVERSAL) và luồng chuyển trạng thái.
- Rule rủi ro: limit theo PAN/merchant (ví dụ tối đa N giao dịch/phút), blacklist.
- Tin cậy: timeout socket, retry DB với backoff, hàng đợi + thread pool.
- Quan sát: log JSON có `request_id`, đo latency cơ bản (timestamp trước/sau xử lý).
- Loadgen: hiện thực hoá `client/loadgen.c` để tạo N kết nối đồng thời và thống kê RPS.
