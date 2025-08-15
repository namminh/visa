# Lộ Trình Chuyên Gia: Threads & Networking (Mini‑Visa)

Tài liệu thực chiến giúp bạn làm chủ thread pool, socket I/O, backpressure, framing/timeout và DB per‑thread trên chính repo này. Mỗi phần đều có: mục tiêu, bước làm cụ thể, lệnh đo đạc, tiêu chí đánh giá, và gợi ý tối ưu.

---

## 1) Mục Tiêu
- Nắm vững bounded queue và backpressure; hiểu tác động đến p95/p99 theo Little’s Law.
- Viết/đọc I/O an toàn: partial read/write, EINTR/EAGAIN, timeout, framing.
- Vận hành/tối ưu TCP: backlog, TCP_NODELAY, SO_REUSEPORT (nâng cao).
- Thiết kế DB per‑thread hợp lý; kiểm tra idempotency và chỉ số DB.
- Đo → phân tích → tối ưu → giải thích trade‑off bằng số liệu thật.

---

## 2) Bản Đồ Năng Lực
- Threads: bounded queue, wake‑up (condvar), graceful shutdown, tránh thundering herd, cache/false sharing.
- Networking: partial I/O loops, newline framing, timeouts, backlog, TCP_NODELAY, SO_REUSEPORT.
- DB/Concurrency: thread‑local connection, sizing `max_connections`, idempotency, indexes.
- Kỷ luật đo lường: RPS/p95, metrics server, đếm threads/sockets, profiling cơ bản.

---

## 3) Checklist Đọc Mã (trước khi làm lab)
- Liên kết nhanh (anchors) trong `server/threadpool.c` để tra cứu trực tiếp:
  - `[ANCHOR:TP_QUEUE_STRUCT]`: cấu trúc hàng đợi bounded và trường `cap/size`.
  - `[ANCHOR:TP_WORKER_WAIT]`: worker chờ `cv` khi chưa có việc.
  - `[ANCHOR:TP_WORKER_EXIT]`: điều kiện thoát êm khi shutdown.
  - `[ANCHOR:TP_EXECUTE_OUTSIDE_LOCK]`: chạy job ngoài vùng khóa để giảm contention.
  - `[ANCHOR:TP_CREATE_SPAWN]`: vòng tạo `pthread_create` các worker.
  - `[ANCHOR:TP_SUBMIT_BACKPRESSURE]`: điểm áp dụng backpressure (từ chối khi queue đầy).
  - `[ANCHOR:TP_DESTROY_BROADCAST_JOIN]`: broadcast + join trong quá trình huỷ.

- Liên kết nhanh (anchors) trong `server/net.c`:
  - `[ANCHOR:NET_OVERVIEW]`: sơ đồ tổng quan accept → submit → busy/handle.
  - `[ANCHOR:NET_SOCKET_SETUP]`: tạo socket, `SO_REUSEADDR` (gợi ý `SO_REUSEPORT`).
  - `[ANCHOR:NET_ACCEPT_LOOP]`: vòng accept xử lý từng kết nối.
  - `[ANCHOR:NET_TCP_NODELAY_HINT]`: gợi ý bật `TCP_NODELAY` để giảm latency.
  - `[ANCHOR:NET_FAST_FAIL_BUSY]`: fast-fail gửi `server_busy` khi queue đầy.
  - `[ANCHOR:NET_CLOSE_LISTENER]`: đóng `listen_fd` khi kết thúc.

- Liên kết nhanh (anchors) trong `server/handler.c`:
  - `[ANCHOR:HANDLER_OVERVIEW]`: tổng quan handler (đọc → validate → ghi → đóng).
  - `[ANCHOR:HANDLER_WRITE_ALL]`: vòng lặp ghi an toàn (partial write, EINTR/EAGAIN).
  - `[ANCHOR:HANDLER_TIMEOUTS]`: thiết lập timeout đọc/ghi.
  - `[ANCHOR:HANDLER_BUFFER]`: bộ đệm 8192, giới hạn dòng chống DoS.
  - `[ANCHOR:HANDLER_READ_LOOP]`: vòng đọc và tích luỹ dữ liệu.
  - `[ANCHOR:HANDLER_FRAMING]`: cắt theo newline (framing theo dòng).
  - `[ANCHOR:HANDLER_HEALTH_READY_METRICS]`: xử lý GET /healthz, /readyz, /metrics.
  - `[ANCHOR:HANDLER_PARSE_VALIDATE]`: parse JSON tối thiểu và validate.
  - `[ANCHOR:HANDLER_LUHN]`: kiểm tra Luhn cho PAN.
  - `[ANCHOR:HANDLER_AMOUNT]`: kiểm tra amount hợp lệ.
  - `[ANCHOR:HANDLER_RISK]`: khung chấm điểm rủi ro.
  - `[ANCHOR:HANDLER_DB_IDEMP]`: insert/tra cứu theo request_id (idempotent).
  - `[ANCHOR:HANDLER_LATENCY_LOG]`: tính latency và log JSON.
  - `[ANCHOR:HANDLER_PARTIAL_REMAINDER]`: xử lý dữ liệu thừa cuối buffer.
  - `[ANCHOR:HANDLER_CLOSE]`: đóng kết nối và giải phóng context.

- `server/threadpool.c`:
  - Queue bounded ở đâu? Khi đầy, hành vi submit và báo `server_busy` thế nào?
  - Vì sao `destroy` dùng broadcast rồi join? Có nguy cơ thundering herd?
- `server/net.c`:
  - Accept loop xử lý khi queue đầy ra sao? Backlog `listen(fd, 128)` cứu burst kết nối mức nào?
  - Vị trí hợp lý để thêm `SO_REUSEPORT` nếu chạy multi‑process?
- `server/handler.c`:
  - Vòng lặp chống short read/write, xử lý EINTR/EAGAIN ở đâu?
  - Framing theo newline kiểm soát dòng quá dài (>8KB) như thế nào?
  - Timeout nhận/gửi 5s tác động gì lên keep‑alive và tài nguyên?
- `server/db.c`:
  - Thread‑local connection tạo/lifespan? Vì sao vẫn cần mutex trong `DBConnection`?
  - Idempotency với `request_id` unique: khi retry sẽ trả về gì?

---

## 4) Phương Pháp Đo Lường Chuẩn
- Xây dựng: `make clean && make`
- Start server: `DB_URI='postgresql://mini:mini@127.0.0.1:5432/mini_visa' PORT=9090 ./scripts/run.sh 2>server.err & echo $! > server.pid`
- RPS/latency: `./build/loadgen 50 200 9090` (trích `RPS=... p95=...`)
- Metrics: `printf 'GET /metrics\r\n' | nc 127.0.0.1 9090`
- Health/Ready: `printf 'GET /healthz\r\n' | nc ...` | `printf 'GET /readyz\r\n' | nc ...`
- Threads thực tế: `top -H -p $(cat server.pid)` hoặc `ps -L -p $(cat server.pid) | wc -l`
- Sockets đang mở: `ss -ant | grep :9090 | wc -l`
- Log tail: `tail -n 100 server.err`
- Tắt server: `kill $(cat server.pid) && rm -f server.pid`

Gợi ý: dùng `tests/run_all.sh` để gom kịch bản + log artifact nhanh trong CI.

Mẹo tra cứu nhanh anchors trong mã:
```
grep -n "\[ANCHOR:TP_" server/threadpool.c
grep -n "\[ANCHOR:NET_" server/net.c
grep -n "\[ANCHOR:HANDLER_" server/handler.c
```

---

## 3.1) Bài Tập Đọc Mã Theo Anchor (Hands‑on)

Mục tiêu: đi qua từng anchor, hiểu ý đồ thiết kế và tự xác nhận bằng thử nghiệm ngắn.

- ThreadPool (server/threadpool.c):
  - TP_QUEUE_STRUCT: đọc cap/size; giải thích vì sao bounded (3–4 câu).
  - TP_SUBMIT_BACKPRESSURE: sửa tạm `if (pool->size >= pool->cap)` → `>= 1` (để dễ tái hiện), build, bắn 50 requests đồng thời, xác nhận server trả `server_busy` sớm. Khôi phục lại sau khi test.
    - Lệnh gợi ý: `for i in $(seq 1 50); do ./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"1"}' & done; wait`
  - TP_WORKER_WAIT/EXECUTE_OUTSIDE_LOCK: dùng `top -H` xem số thread hoạt động khi tải nhẹ vs nặng; giải thích lợi ích chạy ngoài lock.
  - TP_DESTROY_BROADCAST_JOIN: mô tả trình tự tắt êm (net → pool → DB) và rủi ro nếu đảo thứ tự.

- Network (server/net.c):
  - NET_SOCKET_SETUP: thử bật `SO_REUSEPORT` (bỏ comment) và chạy 2 tiến trình server (port giống) → quan sát có phân phối kết nối không (môi trường cho phép).
  - NET_ACCEPT_LOOP: mô tả đường đi khi accept thành công; thêm log ngắn (tạm thời) để thấy tần suất accept dưới tải.
  - NET_FAST_FAIL_BUSY: cấu hình `THREADS=1 QUEUE_CAP=1`, bắn burst và xác nhận JSON `server_busy` xuất hiện; so sánh với trường hợp tăng `QUEUE_CAP`.
  - NET_TCP_NODELAY_HINT: bật macro `#define ENABLE_TCP_NODELAY 1`, đo `./build/loadgen 50 200 9090` trước/sau, ghi `RPS`/`p95`.

- Handler (server/handler.c):
  - HANDLER_TIMEOUTS: đổi `tv_sec` từ 5 → 1, gửi payload không có `\n` và quan sát kết nối bị đóng ~1s; khôi phục sau test.
    - Lệnh: `{ printf '{"pan":"4111111111111111","amount":"1.00"'; sleep 2; } | nc 127.0.0.1 9090`
  - HANDLER_BUFFER/FRAMING: gửi >8KB trước newline, xác nhận bộ đệm được reset (không crash, không leak):
    - `python3 - <<'PY'\nimport sys\nprint('{"pan":"' + '4'*8000 + '","amount":"1.00"}')\nPY | tr -d '\n' | awk '{printf $0}' | (cat; echo) | nc 127.0.0.1 9090`
  - HANDLER_LUHN/AMOUNT: gửi các trường hợp sai để thấy lý do DECLINED thay đổi (`luhn_failed`, `amount_invalid`).
  - HANDLER_DB_IDEMP: chạy `tests/idempotency.sh` với cùng `request_id`, xác nhận COUNT(*)=1.
  - HANDLER_LATENCY_LOG: bật `LIVE_LOG=1` khi chạy `tests/run_all.sh` để xem latency_us trong log JSON.

Ghi chú: mọi thay đổi tạm thời để thí nghiệm cần hoàn nguyên trước khi commit.

---

## 5) Labs Thực Chiến

### Lab 0 — Warm‑up: Smoke & Keep‑alive
- Mục tiêu: xác nhận server hoạt động đúng và framing newline.
- Bước:
  - `make && DB_URI=... PORT=9090 ./scripts/run.sh`
  - `printf 'GET /healthz\r\n' | nc 127.0.0.1 9090` → OK
  - `./tests/keepalive.sh 9090` → 1 kết nối, nhiều dòng request, nhiều phản hồi.
- Tiêu chí: healthz OK; keep‑alive trả ≥5 phản hồi.

### Lab 1 — Backpressure & Sizing (THREADS, QUEUE_CAP)
- Mục tiêu: thấy rõ ảnh hưởng cấu hình lên `server_busy`, p95, RPS.
- Thiết lập nghèo tài nguyên: `THREADS=1 QUEUE_CAP=1 PORT=9090 ./scripts/run.sh`
- Burst: `for i in $(seq 1 50); do ./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"1"}' & done; wait`
- Sau mỗi lần: `GET /metrics` và ghi `server_busy`.
- Ma trận đề xuất:
  - `THREADS ∈ {1,2,4,8}`, `QUEUE_CAP ∈ {1,32,1024}`.
  - Chạy `./build/loadgen 50 200 9090`, ghi `RPS` và `p95`.
- Tiêu chí: tìm điểm bão hòa (tăng THREADS không còn giảm p95/tăng RPS); nhận xét `QUEUE_CAP` lớn làm p95 tăng khi quá tải (Little’s Law).

### Lab 2 — Graceful Shutdown & Resource Safety
- Mục tiêu: hiểu vòng đời worker, dọn tài nguyên đúng thứ tự.
- Đọc `threadpool_destroy`: vì sao broadcast → join? Rủi ro job còn lại?
- Bài tập (tùy chọn code): bắt SIGTERM → dừng accept, đóng `listen_fd`, `threadpool_destroy`, `db_disconnect`; log `graceful_shutdown`.
- Tiêu chí: server dừng sạch, không rò PID/FD/conn; log một dòng shutdown.

### Lab 3 — Partial Read/Write & Newline Framing
- Mục tiêu: quan sát xử lý “nửa gói”.
- Gửi từng phần:
  - `exec 3<>/dev/tcp/127.0.0.1/9090`
  - `printf '{"pan":"4111111111111111","amount":"10' >&3; sleep 1; printf '.00"}' >&3; sleep 1; printf '\n' >&3`
- Thử gửi dòng >8KB trước `\n` → quan sát xử lý/reset.
- Tiêu chí: phản hồi chỉ đến sau newline; không rò/đổ vỡ buffer; log có cảnh báo nếu vượt ngưỡng.

### Lab 4 — Timeouts, TCP_NODELAY, Backlog
- Mục tiêu: đo tác động của timeout và Nagle.
- Timeouts: dừng trước khi gửi `\n` >5s → kết nối bị đóng; đọc log.
- TCP_NODELAY (cần patch nhỏ ở chỗ accept): bật cờ rồi đo `./build/loadgen 50 200 9090` (so sánh trước/sau) — phù hợp gói nhỏ, nhiều phản hồi.
- Backlog: thay đổi tham số `listen(fd, 128)` → test burst connect rate, quan sát drop/timeout.
- Tiêu chí: p95 giảm khi bật `TCP_NODELAY` với workload gói nhỏ; backlog đủ lớn giúp chịu burst connect tốt hơn.

### Lab 5 — Nâng Cao: SO_REUSEPORT, Non‑blocking I/O
- SO_REUSEPORT: thêm `setsockopt(SO_REUSEPORT)` rồi chạy 2 tiến trình server cùng port, so sánh phân phối tải.
- Non‑blocking: chuyển handler sang non‑blocking + event loop (epoll); đòi hỏi tái cấu trúc.
- Tiêu chí: chứng minh CPU/latency cải thiện khi concurrent connections rất lớn (10k+), tránh head‑of‑line block.

### Lab 6 — DB Thread‑local & Idempotency
- Mục tiêu: xác thực connection per‑thread và tính idempotent.
- Kiểm tra idempotency:
  - `tests/idempotency.sh 9090 idem_$(date +%s)` → lặp lại 2–3 lần.
  - `psql $DB_URI -At -c "SELECT COUNT(*) FROM transactions WHERE request_id='idem_...';"` → 1.
- Theo dõi `max_connections` nếu tăng `THREADS` lớn.
- Tiêu chí: COUNT(*) = 1; p95 ổn định; không có full PAN trong log (`tests/run_all.sh` đã kiểm tra).

---

## 6) Kỷ Luật Ghi Số Liệu
- Bảng theo cấu hình: THREADS, QUEUE_CAP, RPS, p95, server_busy, ghi timestamp và git SHA.
- Sau mỗi thay đổi (ví dụ bật `TCP_NODELAY`), chạy lại cùng kịch bản và so sánh.
- Dùng `tests/run_all.sh` để có log hợp nhất và artifact dễ chia sẻ.

Ví dụ bảng (ghi tay hoặc CSV):
```
threads,queue_cap,rps,p95_us,server_busy,notes
1,1,  1200,  9500, 30, baseline constrained
4,32, 4200,  2200,  5, sweet spot on my laptop
8,1024,4800, 3800,  1, queue too big → p95 up
```

### Script tự động hóa đo
- Dùng `scripts/bench_matrix.sh` để chạy ma trận cấu hình và xuất CSV.
- Biến môi trường:
  - `DB_URI` (bắt buộc), `PORT` (mặc định 9090)
  - `THREADS_SET` (mặc định `1,2,4,8`), `QUEUE_CAP_SET` (mặc định `1,32,1024`)
  - `CONNS` (mặc định 50), `REQS` (mặc định 200), `ROUNDS` (mặc định 1)
  - `CSV_OUT` (đường dẫn file CSV; mặc định vào thư mục `logs/`)

Ví dụ chạy:
```
export DB_URI=postgresql://mini:mini@127.0.0.1:5432/mini_visa
PORT=9090 THREADS_SET=1,2,4 QUEUE_CAP_SET=32,1024 CONNS=50 REQS=200 ROUNDS=3 \
  ./scripts/bench_matrix.sh

# Kết quả CSV tại logs/bench-matrix-YYYYMMDD-HHMMSS.csv
```

---

## 7) Nâng Tầm Kiến Trúc (Gợi Ý)
- Event‑driven: epoll + non‑blocking, 1 acceptor + N workers/co‑routines.
- Work‑stealing/MPSC queue: giảm contention so với hàng đợi chung.
- Metrics: thêm histogram (p50/p95/p99) vào `/metrics`.
- Logging: batch log, thêm trace id, đảm bảo không lộ PAN.
- DB: prepared statements, pgbouncer, retry/backoff.

---

## 8) Dụng Cụ Chuyên Gia
- Race/deadlock: `valgrind --tool=helgrind` (build debug).
- CPU profile: `perf record -g -- ./build/server` → `perf report`.
- Network syscalls: `strace -f -e trace=network -p $(cat server.pid)`.
- Chaos net: `tc qdisc add dev lo root netem delay 50ms loss 1%` (sau đó `tc qdisc del dev lo root`).

---

## 9) Bộ Câu Hỏi Phỏng Vấn (Ôn Tập)
- Bounded vs unbounded queue? Tác động đến p95/p99 theo Little’s Law.
- Tránh thundering herd khi nhiều worker chờ `cv` thế nào?
- Deadlock/livelock khi lồng khoá sai thứ tự? Cách phòng?
- SYN backlog, TIME_WAIT, CLOSE_WAIT: sinh ra khi nào? Ảnh hưởng gì?
- Nagle vs TCP_NODELAY: trade‑off latency vs throughput.
- EINTR/EAGAIN: khác biệt và cách viết I/O an toàn.
- Vì sao dùng newline‑delimited framing? Rủi ro DoS và giới hạn độ dài dòng?

---

## 10) Lộ Trình 4 Tuần
- Tuần 1: đọc mã theo checklist, chạy Lab 0–1, viết tóm tắt 1–2 trang.
- Tuần 2: Lab 2–4; thêm patch nhỏ (`TCP_NODELAY`, metric `queue_size`), đo lại p95/RPS.
- Tuần 3: Lab 5–6; thử REUSEPORT hoặc non‑blocking; dùng `perf/helgrind` tìm nút thắt.
- Tuần 4: tổng hợp “báo cáo 1 trang” chuẩn hóa + luyện bộ câu hỏi.

---

## 11) Mẫu Báo Cáo 1 Trang
- Mục tiêu: (ví dụ) giảm p95 dưới 3ms ở 4k RPS.
- Thiết lập: THREADS, QUEUE_CAP, PORT, DB_URI, commit SHA.
- Can thiệp: (ví dụ) bật `TCP_NODELAY`, tăng backlog, chỉnh queue_cap.
- Số đo: trước/sau (RPS, p95, server_busy, CPU theo `top -H`).
- Kết luận: cấu hình tối ưu + trade‑offs chấp nhận được.
- Việc tiếp theo: (ví dụ) thử REUSEPORT hoặc epoll.

---

Gợi ý mở rộng: nếu cần, thêm scripts tự động hóa ma trận cấu hình để sinh CSV từ `./build/loadgen` và `/metrics`. Khi bạn sẵn sàng, có thể triển khai các patch nhỏ (TCP_NODELAY, queue_size metric, graceful shutdown) rồi cập nhật số liệu theo mẫu trên.
