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

## 11) Phụ lục: Oracle (tuỳ chọn — backend hiệu năng cao với Oracle DB)
- Bối cảnh: Bài test mặc định dùng PostgreSQL. Nếu muốn dùng Oracle (XE/SE/EE hoặc 23c Free), giữ nguyên yêu cầu nghiệp vụ; chỉ thay thế lớp truy cập DB và DDL tương ứng. Các mục dưới đây tóm tắt khác biệt thực tế khi tối ưu hiệu năng với Oracle.

- Kết nối & pool:
  - DSN mẫu: `oracle://user:pass@host:1521/service_name` (hoặc `//host:1521/XEPDB1`). Readiness query: `SELECT 1 FROM dual`.
  - Ưu tiên session pool (OCI/ODPI‑C `dpiPool`, UCP, HikariCP for JDBC). Khuyến nghị: `min = THREAD_GROUPS`, `max` theo hạn mức `PROCESSES/SESSIONS` của DB; bật statement cache (`SESSION_CACHED_CURSORS` ~ 100–200) để giảm hard‑parse.
  - Thiết lập prefetch/fetch size phù hợp workload đọc; DML nên dùng array binds để giảm round‑trip (batch size 100–1000 tuỳ latency/jitter giữa app↔DB).

- Schema/DDL tối thiểu (Oracle):
  - Quy ước kiểu: `VARCHAR2(n)` cho text; `NUMBER(18,2)` cho amount; thời gian dùng `TIMESTAMP(6) WITH TIME ZONE` (nếu cần timezone) hoặc `TIMESTAMP(6)`.
  - Id sinh tự động: Oracle 12c+ dùng `GENERATED BY DEFAULT AS IDENTITY`; với phiên bản cũ hơn: tạo `SEQUENCE` và chèn `seq.NEXTVAL` trong `INSERT`.
  - Idempotency: tạo UNIQUE index/constraint trên `request_id`.
  - Index gợi ý: `request_id` (UNIQUE), `created_at`, và cho risk‑velocity: composite `(pan_hash, created_at)` để lọc theo cửa sổ thời gian.

  Ví dụ DDL tham khảo (tối giản):
  - Tạo user/ quyền tối thiểu:
    - `CREATE USER mini IDENTIFIED BY mini;`
    - `GRANT CREATE SESSION, CREATE TABLE, CREATE SEQUENCE, CREATE INDEX TO mini;`
  - Bảng và sequence:
    - `CREATE SEQUENCE tx_id_seq CACHE 1000 NOORDER;`
    - `CREATE TABLE tx (
         id           NUMBER(19)      DEFAULT tx_id_seq.NEXTVAL PRIMARY KEY,
         request_id   VARCHAR2(64)    NOT NULL,
         pan_masked   VARCHAR2(32)    NOT NULL,
         pan_hash     VARCHAR2(64)    NOT NULL,
         amount_cents NUMBER(19)      NOT NULL,
         status       VARCHAR2(16)    NOT NULL,
         reason       VARCHAR2(64),
         created_at   TIMESTAMP(6)    DEFAULT SYSTIMESTAMP NOT NULL
       );`
    - `CREATE UNIQUE INDEX ux_tx_request_id ON tx(request_id);`

- Idempotency không có `ON CONFLICT` (khác Postgres):
  - Cách 1: Bắt ORA‑00001 (unique violation) và trả `idempotent=true` khi `request_id` đã tồn tại.
  - Cách 2: Dùng `MERGE` (upsert tối giản):
    - `MERGE INTO tx t USING dual ON (t.request_id = :rid)
       WHEN NOT MATCHED THEN INSERT (request_id, pan_masked, pan_hash, amount_cents, status)
       VALUES (:rid, :pmask, :phash, :amt, :status)`

- Retry, breaker và mapping lỗi Oracle (thực tế):
  - Retry ngắn hạn: ORA‑12541/12170 (TNS no listener/timeout), ORA‑03135 (connection lost), ORA‑00060 (deadlock — retry an toàn), ORA‑254xx (TAF) — áp dụng backoff + jitter, tối đa ≤ 3 lần như Track B.
  - Không retry: ORA‑01400 (cannot insert NULL), ORA‑02291/02292 (FK), ORA‑12899 (value too large), ORA‑00001 (xem như idempotent trùng).
  - Breaker Open khi gặp chuỗi lỗi kết nối/timeout; Half‑open thử 1 yêu cầu. `/readyz` nên fail nhanh nếu pool cạn hoặc ping `dual` thất bại.

- Tối ưu hiệu năng/độ trễ:
  - Array DML: gom 100–1000 bản ghi/round‑trip, commit theo lô để tránh log sync mỗi record. Tránh auto‑commit per‑row.
  - Sequence: `CACHE 1000 NOORDER` giảm contention (đặc biệt trên RAC). Tránh `ORDER` trừ khi bắt buộc.
  - Thống kê: chạy `DBMS_STATS.GATHER_TABLE_STATS(ownname=>USER, tabname=>'TX', cascade=>TRUE)` sau khi nạp dữ liệu lớn để Optimizer chọn kế hoạch tốt.
  - Query velocity: `WHERE pan_hash = :h AND created_at >= SYSTIMESTAMP - NUMTODSINTERVAL(:win_sec,'SECOND')` + index `(pan_hash, created_at)` giúp quét theo dải thời gian hiệu quả.

- Quan sát/ops trong Oracle:
  - Số liệu nhanh: `V$SYSSTAT`, `V$SESSTAT`, `V$SESSION` (events, waits); nếu có AWR/ASH thì đọc báo cáo để tìm wait chính (log file sync, db file sequential/scattered read, latch: shared pool…)
  - Kiểm tra pool/parse: theo dõi `parse count`, `session cursor cache hits`, `opened cursors current`.
  - Health/readiness: `SELECT 1 FROM dual` + xác thực còn slot trong pool. Expose counters lỗi theo nhóm ORA để vận hành dễ hơn.

- Khác biệt cú pháp phổ biến cần lưu ý:
  - `CURRENT_TIMESTAMP` thay cho `now()`; `DUAL` cho select hằng.
  - Giới hạn: `FETCH FIRST :n ROWS ONLY` (12c+) hoặc `ROWNUM` cũ.
  - Upsert: dùng `MERGE` thay `INSERT ... ON CONFLICT DO NOTHING` (trừ Oracle 23c có `ON CONFLICT`).

- Mapping biến môi trường (gợi ý khi dùng Oracle):
  - `DB_URI="oracle://mini:mini@127.0.0.1:1521/XEPDB1"`
  - `ORA_STMT_CACHE=200 ORA_PREFETCH_ROWS=200 ORA_ARRAY_DML_ROWS=500` (tuỳ driver; hoặc cấu hình tương đương trong code/pool).

Lưu ý: Không bắt buộc dùng Oracle cho bài test. Mục này chỉ hỗ trợ ứng viên chọn Oracle vẫn đáp ứng chuẩn hiệu năng/độ tin cậy tương đương với các track, và biết cách ánh xạ đúng các kỹ thuật (idempotency, retry/breaker, backpressure, metrics) sang hệ sinh thái Oracle.

## 12) Phụ lục: C chuyên sâu cho Payment (thực chiến)
- Mô hình I/O & TCP:
  - Ưu tiên `epoll` edge‑triggered + non‑blocking sockets, `accept4(..., SOCK_NONBLOCK|SOCK_CLOEXEC)`; phân phối kết nối bằng `SO_REUSEPORT` để giảm thundering herd.
  - Quản lý partial read/write; dùng `readv/writev` (scatter/gather) hoặc `sendmsg` với `MSG_MORE` cho batching; tránh `printf` trên socket.
  - Tuning kernel (tham khảo): `tcp_nodelay` theo workload, `SO_SNDBUF/SO_RCVBUF` hợp lý; backlog (SOMAXCONN); theo dõi `netstat -s` để phát hiện retransmit.

- Quản lý buffer/memory an toàn & hiệu năng:
  - Dùng pool/arena/slice per‑thread để giảm malloc/free; gom GC mềm ở biên request. Tránh phân mảnh bằng kích thước block cố định (slab) cho message nhỏ (<= 1–2 KB).
  - Căn cache line: cấu trúc counters/queues nên `alignas(64)` để tránh false sharing; tách trường write‑hot và read‑mostly.
  - Zeroize dữ liệu nhạy cảm: `memset_s`/`explicit_bzero` sau khi dùng PAN/CVV/keys; không ghi vào swap (tuỳ chọn: mlock nhỏ, cân nhắc).

- Xử lý số tiền chính xác:
  - Dùng số nguyên cent (`int64_t amount_cents`) thay `double`; parse `"10.05"` → cent bằng thuật toán base‑10 cố định, kiểm tràn (`> 9e16` cent) và quy tắc round banker nếu cần.
  - Kiểm overflow khi cộng/trừ/nhân phí; dùng helpers an toàn: `bool add_i64_checked(int64_t a,b,int64_t* out)`.

- Parser JSON/tĩnh nhẹ, chống lỗi:
  - Tránh `scanf/atoi/strtod` trực tiếp trên payload; viết parser giới hạn field dự kiến, kiểm độ dài tối đa, chặn ký tự lạ, và dừng ở newline (framing NL‑JSON).
  - Không log dữ liệu thô; nếu cần debug, chỉ log độ dài và hash.

- Đồng thời & backpressure:
  - Mô hình gợi ý: 1 thread accept + N worker threads với hàng đợi MPSC hoặc per‑core SPSC; trả `server_busy` khi hàng đầy để giữ tail‑latency thấp.
  - Atomics C11: counters `atomic_uint64_t` với `memory_order_relaxed` cho tăng đếm; snapshot tổng hợp định kỳ để tránh contention.
  - Tránh deadlock: không gọi DB trong vùng khoá kéo dài; tách bước parse/validate → enqueue → xử lý DB.

- Retry & Circuit Breaker ở mức C:
  - Backoff: dùng `clock_gettime(CLOCK_MONOTONIC, ...)` + ngủ `nanosleep`/`clock_nanosleep` với jitter ±20ms; ngắt sớm khi `max_elapsed` vượt ngưỡng.
  - Breaker: state nhỏ gọn (`closed/open/half_open`, `consecutive_failures`, `opened_at`). Dùng atomics hoặc mutex mỏng cho cập nhật, tránh rò rỉ trạng thái trong multi‑thread.

- Băm/mask PAN và nhạy cảm:
  - Mask: `######********####`; hash/tokens: SHA‑256/HMAC‑SHA‑256 với pepper lưu ngoài (env/HSM). Không lưu CVV/PIN; tuân thủ PCI DSS (truncation + masking + access control).
  - So sánh hằng thời gian cho secret: `CRYPTO_memcmp` hoặc tự viết vòng lặp không branch.

- Giao tiếp DB từ C (tổng quát):
  - Luôn dùng prepared statements + bind variables; batch/array DML khi ghi nhiều; bật statement caching ở driver.
  - Map lỗi thành nhóm `retryable/non_retryable/idempotent_violation` để lớp retry/breaker xử lý thống nhất.

- Logging & metrics hiệu năng cao:
  - Log một dòng, không chặn: buffer per‑thread + `write(2)` hợp nhất; tránh `fprintf` có khoá toàn cục.
  - Metrics counter/gauge đơn giản; p50/p95/p99 dùng HDR Histogram (tuỳ chọn) hoặc bins thô; tránh lock global trong đường nóng.

- Kiểm thử độ tin cậy & an toàn bộ nhớ:
  - Fuzz parser với libFuzzer/AFL; bật `ASAN/UBSAN` trong CI; kiểm soát leak với `valgrind` ở chế độ test.
  - Biên dịch: `-O2 -pipe -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wall -Wextra -Werror` (tuỳ repo; có thể nới lỏng `-Werror` khi cần).

- Thời gian & timeout chuẩn xác:
  - Dùng `CLOCK_MONOTONIC` cho đo latency và timeout; tránh `gettimeofday` (bị NTP ảnh hưởng). Timer nền: `timerfd` hoặc vòng lặp tick per‑thread.

- ISO/chuẩn ngành (tham khảo nhanh):
  - Luhn, ISO 8583 (nếu mở rộng), ISO 9564 (PIN block — không lưu), PCI DSS (tokenization, logging, key mgmt). Bản demo này chỉ chấp nhận PAN+amount, nhưng cấu trúc nên sẵn sàng mở rộng.

Các mục trên nhằm giúp ứng viên thể hiện tư duy hệ thống C hiệu năng cao cho bài toán thanh toán: đúng, nhanh, an toàn và có thể vận hành.
