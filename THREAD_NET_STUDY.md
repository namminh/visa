# Ôn Luyện Chuyên Sâu: Threads & Networking (Mini‑Visa)

Tài liệu thực hành theo repo hiện có, tập trung làm – đo – giải thích. Dùng các file trong `server/` và scripts trong `tests/`, `scripts/` để luyện.

## 0) Bản đồ mã nguồn nhanh
- Thread pool: `server/threadpool.c` — hàng đợi có giới hạn, `threadpool_submit`, `worker_main`.
- Accept loop: `server/net.c` — `socket/bind/listen/accept`, backpressure khi queue đầy.
- Handler: `server/handler.c` — timeout, newline framing, partial read/write, health/ready/metrics.
- DB TLS (thread‑local): `server/db.c` — `db_thread_get` cấp 1 connection/worker.
- Risk velocity: `server/risk.c` — limiter theo PAN (env: `RISK_*`).

## 1) Warm‑up: đọc + xác nhận hành vi
- Đọc mã, ghi ra 5 điều bạn hiểu về: backpressure, partial read, timeouts, TLS DB, keep‑alive.
- Build & chạy: `make && PORT=9090 THREADS=4 QUEUE_CAP=1024 ./scripts/run.sh`
- Smoke:
  - `printf "GET /healthz\n" | nc 127.0.0.1 9090` → `OK`
  - `printf "GET /metrics\n" | nc 127.0.0.1 9090`
  - `./tests/keepalive.sh 9090` → 1 kết nối nhiều dòng, mỗi dòng 1 phản hồi.

## 2) Lab Threads: backpressure & sizing
- Mục tiêu: thấy rõ tác động của `THREADS` và `QUEUE_CAP`.
- Thiết lập nghèo tài nguyên: `PORT=9090 THREADS=1 QUEUE_CAP=1 ./scripts/run.sh`
- Nạp đồng thời: `for i in $(seq 1 50); do ./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"1"}' & done; wait`
- Kỳ vọng: một số request trả `{reason:"server_busy"}`; `/metrics` tăng `server_busy`.
- Thử ma trận nhanh:
  - `THREADS=2,4,8` × `QUEUE_CAP=1,32,1024`. Sau mỗi lần, xem `/metrics` và log.
- Câu hỏi tự luận (ghi 3–5 dòng):
  - Khi tăng `THREADS`, `server_busy` thay đổi ra sao? Có điểm bão hoà?
  - `QUEUE_CAP` nhỏ vs lớn ảnh hưởng p95 (thông qua cảm nhận độ trễ/độ trượt) thế nào?

## 3) Lab Threads: an toàn tài nguyên & tắt máy êm
- Mục tiêu: hiểu chu trình sống worker và dọn tài nguyên.
- Đọc `threadpool_destroy`: vì sao phát `broadcast` rồi `join`? Rủi ro rò rỉ job còn lại?
- Bài tập (tự code nếu muốn): thêm xử lý tín hiệu SIGTERM để
  - đặt cờ dừng accept, đóng `listen_fd`, `threadpool_destroy`, `db_disconnect`.
  - In một dòng log “graceful_shutdown”.
- Câu hỏi: vì sao cần dọn theo thứ tự net → pool → DB?

## 4) Lab Net: partial read/write, newline framing
- Mục tiêu: quan sát xử lý “nửa gói”.
- Gửi từng phần (bash /dev/tcp):
  - `exec 3<>/dev/tcp/127.0.0.1/9090`
  - `printf '{"pan":"4111111111111111","amount":"10' >&3; sleep 1; printf '.00"}' >&3; sleep 1; printf '\n' >&3`
  - Bạn sẽ nhận phản hồi sau khi gửi `\n` (framing theo dòng).
- Thử gửi dòng quá dài (>8KB) → handler sẽ reset buffer; thảo luận DoS control.
- Đọc `write_all` trong handler: cách xử lý `EINTR/EAGAIN`; vì sao cần vòng lặp?

## 5) Lab Net: timeouts, Nagle, backlog
- Timeouts: `handler.c` đặt `SO_RCVTIMEO/SO_SNDTIMEO` = 5s. Thử không gửi `\n` trong 5s để thấy kết nối bị đóng.
- Nagle vs latency:
  - Bài tập (tuỳ chọn code): sau `accept`, bật `TCP_NODELAY` cho `fd` để giảm trễ gói nhỏ.
  - Thảo luận: khi nào bật/tắt giúp ích? Tác động đến băng thông và CPU?
- Backlog: trong `listen(listen_fd, 128)`—ý nghĩa backlog? Tác dụng khi burst kết nối?

## 6) Lab Net nâng cao: REUSEPORT, non‑blocking
- REUSEPORT (nâng cao): chạy nhiều acceptors (nhiều process hoặc nhiều threads có `SO_REUSEPORT`) để phân phối tải kernel‑level.
  - Bài tập (tuỳ chọn code): thêm `setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, ...)`, chạy 2 tiến trình cùng port, so sánh tải.
- Non‑blocking: đặt `O_NONBLOCK` cho `listen_fd` hoặc per‑client `fd`.
  - Bài tập (tuỳ chọn code): chuyển handler sang non‑blocking hoàn toàn, kiểm `EAGAIN` + event loop (epoll) — chỉ làm nếu quen tay.

## 7) Lab TLS DB (thread‑local)
- Đọc `db_thread_get`: lần đầu trong 1 thread sẽ tạo connection mới từ URI bootstrap.
- Câu hỏi:
  - Trade‑off per‑thread connection vs pool chung? `max_connections` PG ảnh hưởng sizing thế nào?
  - Vì sao vẫn có mutex trong `DBConnection` dù mỗi worker có connection riêng?

## 8) Gỡ rối & quan sát khuyến nghị
- Đếm luồng: `ps -L -p <PID> | wc -l` (hoặc `top -H`) để xem số worker thực tế.
- Kết nối TCP: `ss -ant | grep :9090 | wc -l` để thấy concurrent connections.
- Log một dòng: kiểm tra stderr/log file, đảm bảo không có xuống dòng trong JSON log.
- Memory/thread bug (tùy môi trường): valgrind/helgrind, ASan/TSan cho path đổi nhiều.

## 9) Bộ câu hỏi luyện phỏng vấn
- Thread pool: thiết kế queue bounded; tại sao không unbounded? Ảnh hưởng p95/p99 theo Little’s Law?
- Wake‑up thundering herd: làm sao tránh khi nhiều worker chờ `cv`?
- Deadlock/livelock: ví dụ trong pool/DB nếu lồng khoá sai thứ tự? Cách phòng?
- TCP basics: SYN backlog, TIME_WAIT, CLOSE_WAIT—tạo ra khi nào? Ảnh hưởng server?
- Nagle vs TCP_NODELAY: trade‑off latency vs throughput.
- EINTR/EAGAIN: khác gì? Cách viết read/write an toàn?
- Partial read/write: vì sao luôn cần vòng lặp? Minh hoạ trên handler hiện có.
- Keep‑alive & framing: vì sao dùng newline‑delimited? Rủi ro và cách giới hạn DoS?

## 10) Bài tập mở rộng (tự chọn viết mã)
- Thêm metric `queue_size` hiện tại và `queue_cap` vào `/metrics`.
- Đặt tên luồng: `pthread_setname_np` theo định dạng `worker-%02d` để debug dễ hơn.
- Graceful shutdown: bắt SIGTERM, dừng accept, đợi drain queue, rồi thoát sạch.
- Thêm `TCP_NODELAY` + đo khác biệt latency cho gói nhỏ.

## 11) Kết cấu báo cáo ngắn (1 trang)
- Mục tiêu thí nghiệm.
- Thay đổi cấu hình/mã (nếu có).
- Số đo: trước/sau (`/metrics`, quan sát `server_busy`, cảm nhận độ trễ).
- Kết luận & trade‑offs: cấu hình ưa thích cho môi trường máy bạn.

Tham khảo thêm: `PRACTICE_PROJECT_CHUYEN_SAU.md` (mục 3–5, 7–9), `TEST_HUONGDAN_CHUYEN_SAU.md` (kịch bản test), `server/*.c` (điểm tựa đọc mã).

