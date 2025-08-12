# Hướng dẫn siêu dễ: Server, Client, Script (cho học sinh lớp 5)

Xin chào! Đây là dự án “mini-visa”. Hãy tưởng tượng:
- Server giống như cô thu ngân ở cửa hàng: nhận yêu cầu thanh toán và trả lời “Đồng ý” (APPROVED) hoặc “Từ chối” (DECLINED).
- Client là “robot khách hàng”: gửi yêu cầu thanh toán để thử server.
- Scripts là những nút bấm nhanh giúp chạy, kiểm tra, và sao lưu dữ liệu.

Quan trọng: Đây chỉ là mô phỏng để học. Không dùng số thẻ thật nhé!

## Server (Cô thu ngân)
- Nghe ở một “cổng” (ví dụ: 9090).
- Nhận thông tin thanh toán dạng JSON (một chuỗi văn bản có cặp "tên":"giá trị").
- Kiểm tra sơ bộ:
  - Số thẻ phải “đúng quy tắc” (Luật Luhn – bạn tạm hiểu là phép kiểm tra số hợp lệ).
  - Số tiền phải lớn hơn 0 và không quá 10000.
- Nếu hợp lệ: Server trả về `{\"status\":\"APPROVED\"}` và ghi vào cơ sở dữ liệu (DB).
- Nếu không hợp lệ: Trả về `{\"status\":\"DECLINED\", \"reason\":\"...\"}` (đưa lý do).

Cách bật server (nhờ người lớn giúp gõ lệnh):
```bash
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
make clean && make
PORT=9090 ./scripts/run.sh
```
Bạn sẽ thấy dòng giống như: “Server listening on port 9090”.

## Client (Robot khách hàng)
- Gửi yêu cầu thanh toán đến server để thử nghiệm.
- Trong dự án đã có một nút nhanh gửi 1 yêu cầu:
```bash
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'
```
- Nếu hợp lệ, server trả về “APPROVED”. Nếu sai (ví dụ amount âm), server trả “DECLINED”.

Ngoài ra còn có “robot gửi nhiều” (stress):
```bash
CONNS=50 REQS=200 PORT=9090 ./tests/stress.sh
```
(Phần client nâng cao này để người lớn hoặc bạn lớn hơn khám phá thêm.)

## Scripts (Các nút bấm nhanh)
- `scripts/run.sh`: Bật server (nhớ đặt `DB_URI` trước).
- `tests/send-json.sh`: Gửi 1 yêu cầu JSON để thử server.
- `tests/stress.sh`: Gửi rất nhiều yêu cầu để xem server chịu tải thế nào.
- `tests/chaos.sh`: Tạm dừng server 5 giây rồi chạy tiếp (giống thử thách bất ngờ).
- `scripts/backup.sh`: Sao lưu dữ liệu DB (giữ 7 bản gần nhất).
- `scripts/tail-errs.sh`: Xem nhanh các dòng “lỗi” trong log.

## Xem kết quả ở đâu?
- Kết quả trả lời ngay trên màn hình khi bạn gửi yêu cầu (APPROVED hoặc DECLINED).
- Các giao dịch “APPROVED” được lưu trong DB.
- Nhờ người lớn chạy lệnh dưới để xem 5 giao dịch mới nhất:
```bash
psql "$DB_URI" -At -F ' | ' -c "SELECT id, pan_masked, amount, status, to_char(created_at,'YYYY-MM-DD HH24:MI:SS') FROM transactions ORDER BY id DESC LIMIT 5;"
```

## Trò chơi nhỏ: thử và học
1) Gửi yêu cầu hợp lệ (số thẻ mẫu 4111111111111111, số tiền 10.00) → thấy “APPROVED”.
2) Đổi số thẻ sai 1 chữ số → thấy “DECLINED” (lý do: luhn_failed).
3) Đổi amount thành -5 → thấy “DECLINED” (lý do: amount_invalid).
4) Nhờ người lớn xem DB để thấy các bản ghi “APPROVED” đã được lưu (số thẻ đã được che bớt).

## Từ điển mini
- PAN: Số thẻ (Primary Account Number).
- Mask (che): Giữ 6 số đầu và 4 số cuối, phần giữa thay bằng dấu `*` để bảo vệ thông tin.
- APPROVED: Đồng ý thanh toán.
- DECLINED: Từ chối thanh toán.
- DB (Cơ sở dữ liệu): Nơi lưu thông tin giao dịch.

Bạn đã sẵn sàng làm “kỹ sư nhí” rồi đó! Nếu muốn đi xa hơn, hãy xem tài liệu `VISA_FLOW.md` để biết cách hệ thống lớn (như VISA) hoạt động đơn giản hóa trong dự án này.

## Bóc tách theo dòng lệnh: Server xử lý ra sao?
Mục tiêu: thấy từng bước từ lúc bật server → gửi yêu cầu → kiểm tra → ghi DB → trả lời.

1) Bật server và theo dõi lỗi
```bash
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
make && PORT=9090 ./scripts/run.sh 2>server.err & echo $! >server.pid
./scripts/tail-errs.sh server.err   # xem nhanh các dòng lỗi/cảnh báo
```
Bạn sẽ thấy: “Server listening on port 9090”.

2) Gửi 1 yêu cầu từ client
```bash
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'
```
Nếu hợp lệ, client sẽ nhận `{ "status":"APPROVED" }`.

3) Bên trong server (đọc hiểu nhẹ nhàng)
- Nhận kết nối: file `server/net.c`, hàm `net_server_run(...)` dùng `accept(...)` để lấy một “điện thoại” nói chuyện với khách.
- Giao cho “nhân viên xử lý”: `handler_job(...)` trong `server/handler.c`.
- Đọc dữ liệu: `read(fd, ...)` nhận chuỗi JSON.
- Lấy 2 thông tin quan trọng: `pan` (số thẻ) và `amount` (số tiền).
- Kiểm tra:
  - `luhn_check(...)`: phép kiểm tra số thẻ hợp lệ.
  - `amount` phải > 0 và không quá 10000.
- Che số thẻ: `mask_pan(...)` để bảo mật (giữ 6 số đầu, 4 số cuối).
- Ghi DB: `db_insert_transaction(...)` trong `server/db.c` chạy câu lệnh SQL INSERT (nếu APPROVED).
- Trả lời: dùng `write_all(...)` gửi JSON APPROVED/DECLINED cho client.
- Đóng kết nối: `close(fd)`.

4) Xem dữ liệu trong DB (chỉ các giao dịch APPROVED mới được ghi)
```bash
psql "$DB_URI" -At -F ' | ' -c "SELECT id, pan_masked, amount, status, to_char(created_at,'YYYY-MM-DD HH24:MI:SS') FROM transactions ORDER BY id DESC LIMIT 5;"
```

5) Thử thay đổi để thấy nhánh xử lý lỗi
```bash
# Sai Luhn → DECLINED (luhn_failed)
./tests/send-json.sh 9090 '{"pan":"4111111111111112","amount":"10.00"}'

# Số tiền âm → DECLINED (amount_invalid)
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"-5"}'
```
Quan sát client nhận gì và server log có thông báo gì.

6) Tắt server khi xong việc
```bash
kill "$(cat server.pid)"; rm -f server.pid
```

7) Xem nhanh đoạn mã (tuỳ chọn, cho bạn tò mò)
```bash
# Tên hàm và nơi xử lý
grep -n "net_server_run" server/net.c
grep -n "handler_job" server/handler.c
grep -n "db_insert_transaction" server/db.c
```
Bạn có thể mở các file đó để xem các bước giống như đã mô tả ở trên.
