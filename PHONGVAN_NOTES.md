# Ghi chú phỏng vấn: mini-visa (VN)

Mục tiêu: tóm tắt ngắn gọn kiến trúc, quyết định kỹ thuật, và câu trả lời mẫu để trình bày trong phỏng vấn khi nói về hệ thống thanh toán mô phỏng (high-load, multi-threaded).

## Kiến trúc tổng thể
- Acceptor + Thread Pool: một luồng accept kết nối, giao công việc vào pool để xử lý song song.
- Backpressure: hàng đợi bounded; nếu đầy, trả JSON `{\"status\":\"DECLINED\",\"reason\":\"server_busy\"}` rồi đóng.
- DB per-thread: mỗi worker có kết nối PostgreSQL riêng (TLS) qua `db_thread_get(...)` để giảm tranh chấp.
- Đường đi request: TCP → `net_server_run` → `handler_job` → validate (Luhn/amount) → mask PAN → `db_insert_transaction` → response JSON.

Điểm neo trong code
- `server/main.c`: khởi tạo cấu hình (ENV), DB bootstrap, thread pool, chạy server.
- `server/net.c`: vòng lặp accept + submit vào pool, backpressure.
- `server/handler.c`: xử lý 1 request, timeout I/O, parse tối giản, validate, ghi DB, trả phản hồi.
- `server/db.c`: kết nối PG, chèn bản ghi, per-thread connection (TLS) + mutex dự phòng.
- `server/threadpool.c`: pool đa luồng hàng đợi FIFO có giới hạn.
- `client/loadgen.c`: bắn tải đa luồng, đo RPS và p50/p95/p99.

## Câu hỏi thường gặp (và gợi ý trả lời)
- Hỏi: Vì sao cần thread pool thay vì tạo thread cho mỗi kết nối?
  - Đáp: Tạo thread theo kết nối gây overhead và không kiểm soát được song song. Pool cố định + hàng đợi bounded giúp backpressure và ổn định.

- Hỏi: Vì sao thiết kế backpressure thay vì nhận hết rồi xử lý?
  - Đáp: Khi downstream (DB) chậm, nếu nhận vô hạn sẽ cạn tài nguyên (FD/RAM). Bounded queue cho phép từ chối sớm và bảo vệ hệ thống.

- Hỏi: Chia sẻ 1 kết nối DB cho nhiều luồng có an toàn không?
  - Đáp: libpq không an toàn khi 1 PGconn dùng đồng thời bởi nhiều luồng. Ta dùng per-thread PGconn (TLS) qua `db_thread_get`. Mutex còn lại chỉ là dự phòng khi bị fallback.

- Hỏi: Làm sao đảm bảo tính đúng đắn dữ liệu (idempotency)?
  - Đáp: Bản tối thiểu chưa có `request_id` unique. Bước nâng cấp: thêm cột `request_id` unique + logic trả kết quả cũ khi lặp lại.

- Hỏi: Tại sao phải mask PAN? Có yêu cầu bảo mật nào?
  - Đáp: Không lưu PAN thô để tránh rủi ro; chỉ log/mask cần thiết (6 đầu + 4 cuối). Với PCI-DSS thực tế cần nhiều biện pháp hơn (mã hoá, tokenization, kiểm soát truy cập).

- Hỏi: Xử lý reliability thế nào?
  - Đáp: Timeout I/O socket (SO_RCVTIMEO/SO_SNDTIMEO), có thể thêm `statement_timeout` cho DB. `write_all` xử lý ghi từng phần và EINTR.

- Hỏi: Đo hiệu năng ra sao?
  - Đáp: Dùng `loadgen` để bắn tải đồng thời, in `RPS`, p50/p95/p99. So sánh khi thay đổi `THREADS`/`QUEUE_CAP`.

- Hỏi: Mở rộng để chịu tải lớn hơn?
  - Đáp: Dùng epoll/kqueue, tái sử dụng kết nối client (keep-alive), connection pool DB giới hạn kích thước, batching/async I/O, tách read/parse/DB/response thành pipeline.

- Hỏi: Log và quan sát gì quan trọng?
  - Đáp: Thêm `request_id`, timestamp, thời gian xử lý, mã lỗi. Theo dõi RPS, tỉ lệ lỗi, p95/p99. Tích hợp Prometheus/OpenTelemetry ở bản nâng cấp.

- Hỏi: Test và Chaos?
  - Đáp: Stress tăng dần (`./build/loadgen`, `tests/stress.sh`), tạm dừng server (`tests/chaos.sh`), theo dõi DB/log. Dùng Valgrind để bắt rò rỉ bộ nhớ.

## Lệnh minh họa nhanh
- Cấu hình & chạy:
  - `export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"`
  - `export PORT=9090 THREADS=8 QUEUE_CAP=2048`
  - `make clean && make && ./scripts/run.sh 2>server.err & echo $! >server.pid`
- Gửi 1 request hợp lệ:
  - `./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'`
- Bắn tải + đo:
  - `./build/loadgen 50 200 9090` → in `RPS`, p50/p95/p99
- Xem DB:
  - `psql "$DB_URI" -At -F ' | ' -c "SELECT id, pan_masked, amount, status, to_char(created_at,'YYYY-MM-DD HH24:MI:SS') FROM transactions ORDER BY id DESC LIMIT 10;"`

## Điểm thỏa hiệp hiện tại (để thảo luận)
- Parser JSON tối giản (dễ vỡ) → thay bằng thư viện chuẩn ở bản thật.
- Mỗi request mở 1 TCP trong loadgen (đơn giản) → có thể tái sử dụng kết nối để đo đúng hơn.
- Chưa có idempotency, capture/refund → có đề xuất trong `VISA_FLOW.md`.
- Log chưa có request_id/định dạng JSON → bản nâng cấp.

## Gợi ý khai thác khi phỏng vấn
- Vẽ sơ đồ: Client → Server (accept) → Queue → Workers → DB → Response.
- Phân tích bottleneck: CPU worker, queue đầy, PG connection pool, chỉ số p95/p99.
- Mô tả cách scale: tăng THREADS, scale-out process (SO_REUSEPORT), tách DB ra pool, caching/batching.

## Mẫu trình bày 2–3 phút (script nói)
Chào anh/chị, em xin trình bày nhanh về mini-visa – một cổng thanh toán mô phỏng em xây để luyện high‑load và multi‑threading.

Về kiến trúc, em chọn mô hình acceptor + thread pool. Một luồng duy nhất lắng nghe và nhận kết nối; mỗi kết nối được đóng gói thành “job” và đẩy vào hàng đợi FIFO có giới hạn. Các worker trong pool lấy job và xử lý song song: đọc JSON, kiểm tra số thẻ theo Luhn, kiểm tra amount, che PAN, ghi giao dịch vào PostgreSQL rồi trả JSON APPROVED/DECLINED. Hàng đợi có giới hạn giúp em áp dụng backpressure: khi quá tải, server trả về "server_busy" thay vì nhận vô hạn và sập.

Về dữ liệu, libpq không an toàn khi nhiều luồng cùng dùng một PGconn, nên em dùng per‑thread DB connection: mỗi worker có một kết nối riêng, tạo qua TLS khi cần, giúp giảm tranh chấp. Em vẫn giữ mutex trong wrapper để an toàn nếu rơi vào trường hợp dùng chung.

Về độ tin cậy, em đặt timeout đọc/ghi socket để tránh treo, có hàm write_all xử lý EINTR và ghi từng phần. Đầu vào JSON hiện dùng parser tối giản cho demo; phiên bản thật em sẽ thay bằng thư viện chuẩn và bổ sung idempotency (request_id unique) cũng như các trạng thái giao dịch như capture/refund.

Đo đạc, em viết loadgen đa luồng. Mỗi request mở một kết nối, gửi payload tối giản, đo độ trễ từng request và in RPS, p50/p95/p99. Nhờ vậy có thể so sánh nhanh giữa cấu hình THREADS/QUEUE_CAP khác nhau và thấy được tác động của per‑thread DB connections.

Demo ngắn: đặt DB_URI, build, chạy server; dùng loadgen bắn "50 workers × 200 requests". Kỳ vọng RPS ổn định, khi hàng đợi đầy nhận "server_busy", và các giao dịch hợp lệ xuất hiện trong bảng transactions ở DB. Toàn bộ lệnh chạy và cách đọc kết quả em đã ghi trong tài liệu kèm repo.

Tổng kết, điểm nhấn của em là kiểm soát song song bằng thread pool + backpressure, giảm contention DB bằng per‑thread connections, và có công cụ đo đạc để đưa ra quyết định kỹ thuật dựa trên số liệu. Các hướng mở rộng tiếp theo là parser JSON chuẩn, idempotency, logging có request_id, và đo p99.9.
