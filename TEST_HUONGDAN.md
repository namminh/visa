# Hướng dẫn test mini-visa theo kịch bản (từng bước học)

Tài liệu này giúp bạn học và kiểm thử theo kịch bản thực tế. Lưu ý: mã nguồn hiện là khung xương (starter) với nhiều phần còn “stub” (chưa triển khai). Một số bài lab có thể chạy ngay (cấu hình/DB/error), các bài còn lại mô tả cách test sau khi bạn hoàn thiện TODO trong mã.

## 0) Tổng quan mã nguồn (đã/thiếu)
- Mạng (`server/net.c`): chưa có socket, bind, listen/accept, phân phối job vào thread pool.
- Thread pool (`server/threadpool.c`): chưa có hàng đợi, mutex/condvar, worker threads.
- Xử lý request (`server/handler.c`): chưa đọc/parse JSON, validate, giao dịch DB, phản hồi client.
- DB (`server/db.c`): kết nối PostgreSQL có thật; `db_insert_transaction` còn stub.
- Cấu hình (`server/config.c`): đọc `DB_URI`; port và số thread đang hard-code.
- Log (`server/log.c`): stub (in stderr, chưa timestamp/định dạng/log file).
- Client/loadgen (`client/loadgen.c`): chưa tạo kết nối/ gửi request.

Kết luận: hiện chưa chạy được E2E thực tế. Hãy dùng các lab bên dưới để học dần và xác nhận từng phần sau khi bạn triển khai các TODO tương ứng.

## 1) Chuẩn bị chung
- Cài đặt: `build-essential`, `libpq-dev`, `postgresql`, `valgrind` (tuỳ chọn).
- DB mẫu:
  ```bash
  sudo -u postgres psql -c "CREATE DATABASE mini_visa;"
  sudo -u postgres psql -c "CREATE USER mini WITH PASSWORD 'mini';"
  sudo -u postgres psql -d mini_visa -f db/schema.sql
  sudo -u postgres psql -d mini_visa -f db/seed.sql   # tuỳ chọn
  ```
- Build: `make`
- Biến môi trường DB: `export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"`

## 2) Lab “Sanity” chạy được ngay (với mã hiện tại)
2.1 Thiếu `DB_URI`
- Mục tiêu: xác nhận xử lý cấu hình cơ bản.
- Thực hiện: unset DB_URI rồi chạy `./scripts/run.sh`.
  ```bash
  unset DB_URI
  ./scripts/run.sh
  ```
- Kỳ vọng: script báo yêu cầu `DB_URI` và thoát.

2.2 Sai thông tin DB (kết nối thất bại)
- Mục tiêu: kiểm tra lỗi kết nối DB.
- Thực hiện: đặt DB_URI sai (user/pass/sai DB), chạy `./scripts/run.sh`.
- Kỳ vọng: stderr hiển thị lỗi từ libpq (Database connection failed...), thoát code ≠ 0.

2.3 Backup DB
- Mục tiêu: xác nhận script backup hoạt động.
- Thực hiện:
  ```bash
  ./scripts/backup.sh
  ls -1 backups/
  ```
- Kỳ vọng: tạo file `.sql.gz`, giữ tối đa 7 bản mới nhất.

2.4 Quan sát log/error
- Mục tiêu: dùng `tail-errs.sh` lọc lỗi.
- Thực hiện: chuyển stderr của server ra file, ví dụ khi bạn đã có server chạy thật; hiện tại dùng khi các phần mạng đã hoàn tất.
  ```bash
  ./scripts/tail-errs.sh server.err
  ```

## 3) Lab Mạng cơ bản (sau khi triển khai net_server_run)
- Mục tiêu: server listen cổng 9090, accept, đóng kết nối (echo/minimal).
- Gợi ý triển khai: tạo socket, `bind(9090)`, `listen`, vòng lặp `accept`, mỗi client đẩy vào thread pool bằng `threadpool_submit`.
- Test nhanh bằng netcat (nc):
  ```bash
  PORT=9090 ./scripts/run.sh &   # chạy nền, ghi nhớ PID
  echo '{"ping":1}' | nc 127.0.0.1 9090
  ```
- Kỳ vọng: kết nối được, server log "handler_job stub called" (hiện handler chỉ đóng socket).
- Trường hợp lỗi: cổng bận (đã có tiến trình khác listen 9090) → server fail ngay khi bind.

## 4) Lab Thread pool (sau khi triển khai queue/worker)
- Mục tiêu: nhiều kết nối xử lý song song, có hàng đợi khi quá tải.
- Gợi ý triển khai: mảng worker threads, queue liên kết, mutex/condvar.
- Test tải:
  ```bash
  make client
  ./build/loadgen 50 200 9090   # sau khi loadgen được triển khai gửi request thật
  ```
- Kỳ vọng: không crash; throughput ổn định; khi quá tải không lock-up.
- Thu thập số liệu: log thời gian bắt đầu/kết thúc xử lý; sơ bộ đo RPS qua client.

## 5) Lab Giao thức/Handler & Validate (sau khi triển khai handler)
- Mục tiêu: parse JSON, validate field, mask PAN, phản hồi JSON.
- Định nghĩa request (gợi ý): `{"pan":"4111111111111111","amount":"10.00","currency":"USD","merchant":"M1"}`
- Kiểm thử:
  - Hợp lệ: PAN 16 số, amount > 0, currency 3 ký tự.
  - Không hợp lệ: thiếu field, JSON hỏng, amount âm/không parse được, PAN sai định dạng.
- Cách test: sau khi loadgen hỗ trợ sinh request JSON, hoặc dùng `nc`/`curl --data-binary` tùy giao thức bạn chọn.
- Kỳ vọng: response JSON có `status: ok|error`, thông báo lỗi rõ ràng, không rò rỉ kết nối.
  
  Hoặc dùng script tiện lợi (sau khi server lắng cổng):
  ```bash
  ./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00","currency":"USD","merchant":"M1"}'
  ./tests/send-json.sh 9090 '{"pan":"bad","amount":"-5","currency":"U","merchant":"M1"}'   # ví dụ không hợp lệ
  ```

## 6) Lab DB Transaction & Idempotency (sau khi db_insert_transaction hoàn thiện)
- Mục tiêu: INSERT an toàn trong transaction, xử lý lỗi tạm thời, rollback khi cần.
- Bài test:
  - Thành công: record được chèn; response `ok`.
  - Lỗi tạm thời (giả lập): làm `db_insert_transaction` trả lỗi một lần rồi thành công → client retry (nếu bạn thiết kế retry).
  - Idempotency: gửi cùng `request_id` 2 lần → chỉ ghi 1 lần (ràng buộc unique) và trả về `ok`/`duplicate` phù hợp.
- Kỳ vọng: không double-charge; transaction commit/rollback đúng; lỗi được log.

## 7) Lab Timeout/Retry/Backpressure
- Mục tiêu: không treo vô thời hạn, retry có giới hạn, không “bùng nổ” khi DB chậm.
- Cách tạo timeout: trong handler tạm `sleep()` hoặc trì hoãn DB; giới hạn thời gian đọc/ghi socket (`setsockopt`/`poll`/`select`).
- Kỳ vọng: quá hạn trả lỗi; retry theo backoff; khi hàng đợi đầy thì từ chối sớm hoặc chặn có kiểm soát.

## 8) Lab Chaos & Độ bền
- `tests/chaos.sh` (sau khi server chạy thật):
  ```bash
  SERVER_PID=<pid_server> ./tests/chaos.sh
  ```
  - Kỳ vọng: server tạm dừng 5s, resume và tiếp tục phục vụ; client có thể thấy timeout/ retry.
- Độ trễ mạng: dùng `tc` (cần quyền root) như gợi ý trong file `chaos.sh` để thêm delay/loss; đo ảnh hưởng đến thông lượng/thời gian phản hồi.

## 9) Lab Quan sát & Chất lượng
- Logging: thêm timestamp, mức độ (INFO/WARN/ERROR), request_id; test `scripts/tail-errs.sh` lọc lỗi.
- Rò rỉ bộ nhớ: chạy `valgrind`:
  ```bash
  valgrind --leak-check=full ./build/server
  ```
- Điều kiện tranh chấp: dùng `valgrind --tool=helgrind` (nếu phù hợp) để phát hiện deadlock/race.
- Tải tăng dần: `./tests/stress.sh` với `CONNS/REQS` tăng; quan sát khi tiệm cận giới hạn.

## 10) Ma trận kịch bản kiểm thử (tóm tắt)
- Cấu hình: thiếu/sai `DB_URI` → fail sớm, log rõ ràng.
- Mạng: cổng bận, timeout đọc/ghi, client đóng đột ngột.
- Giao thức: JSON hợp lệ/không hợp lệ/mất field/kiểu sai.
- DB: thành công/lỗi tạm thời/lỗi vĩnh viễn/vi phạm unique → retry/rollback.
- Tải: đồng thời cao, hàng đợi đầy → backpressure hoặc từ chối sớm.
- Chaos: tạm dừng tiến trình, thêm độ trễ/loss mạng.
- Độ bền: khởi động/tắt an toàn, cleanup tài nguyên.
- Chất lượng: không rò rỉ bộ nhớ, không deadlock, log/metrics đầy đủ.

## 11) Lộ trình học-đi-kèm-test (đề xuất)
1) Hoàn thiện net_server_run → test với nc (Lab 3).
2) Hoàn thiện thread pool → test tải nhẹ (Lab 4).
3) Xác định giao thức/handler → test hợp lệ/không hợp lệ (Lab 5).
4) Hoàn thiện DB insert/transaction/idempotency → test DB (Lab 6).
5) Thêm timeout/retry/backpressure → test chịu lỗi (Lab 7).
6) Bổ sung logging/metrics → test quan sát (Lab 9).
7) Chạy stress/chaos → đánh giá độ bền (Lab 8).

Gợi ý: cập nhật `README.md`/`HUONGDAN.md` sau mỗi mốc để đồng bộ cách chạy và cách test.
