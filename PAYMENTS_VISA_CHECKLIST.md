# ✅ PAYMENTS VISA – CHECKLIST CHI TIẾT (kèm lưu ý phỏng vấn C Developer)

Mục tiêu: Checklist thực dụng để thiết kế, triển khai, vận hành luồng thanh toán theo chuẩn mạng thẻ (ưu tiên VISA) trong hệ thống Mini-Visa, đồng thời liệt kê các điểm cần khai thác khi phỏng vấn C developer cho mảng này.

---

## 1) Scope & Định danh Giao Dịch
- Transaction scope: Xác định BEGIN/END rõ ràng cho từng payment.
- Định danh: `txn_id` nội bộ, `idempotency_key`, ISO8583 `STAN(11)`, `RRN(37)`, `ProcessingCode(3)`.
- Tương quan: Lưu mapping `txn_id` ↔ (`STAN`,`RRN`,`amount`,`merchant`,`timestamp`).
- Idempotency: Cấu trúc bảng dedupe, TTL hợp lý, đảm bảo atomic check-then-insert.

## 2) Protocol & Messaging (ISO 8583, 3DS, EMV)
- ISO8583 fields cốt lõi: `MTI`, `PAN(2)`, `ProcessingCode(3)`, `Amount(4)`, `STAN(11)`, `LocalTime(12)`, `LocalDate(13)`, `CaptureDate(17)`, `RRN(37)`, `AuthID(38)`, `Response(39)`, `TID(41)`, `MID(42)`, `EMV(55)`.
- Phân loại thông điệp: Authorization, Financial, Advice, Reversal, Batch/Clearing.
- 3-D Secure 2.x: `ECI`, `CAVV/AAV`, `XID`; phân luồng Frictionless vs Challenge; liability shift.
- EMV/qVSDC: Tag-55 (ARQC/ARPC, AIP/AFL, TVR, TSI...), contactless fallbacks; xử lý GL/TC/ARQC lỗi.
- STIP (Stand-In Processing): Issuer mất kết nối → approval tạm; cần logic reconcile chặt chẽ.

## 3) Authorization → Settlement Flow
- Auth: Đặt hold (tạm giữ), thời hạn hold (theo BIN/issuer/region); lưu trạng thái PENDING.
- Capture/Settlement: Full/partial capture, incremental (hotel/car rental), tip adjust.
- Void: Trước khi settle; Refund: Sau settle (full/partial), có thể async.
- Advice/Reversal: Gửi reversal khi outcome unknown/timeout; handle advice/out-of-order.
- Batch: Cắt batch theo ngày/T+1; chuẩn bị clearing file; theo dõi chênh lệch.

## 4) Reliability, Timeout, Retry, Circuit Breaker
- SLA: Target P95/P99 (vd 2–5s). Timeout per-hop và tổng (`transaction_deadline`).
- Retry: Backoff + jitter; tất cả handler idempotent; không retry vô hạn.
- Reversal: Bắt buộc gửi reversal khi không chắc kết quả; quan sát ACK/timeout.
- Circuit breaker: Tripping theo error rate/latency; fallback queue; health checks.

## 5) Fraud, SCA và Risk
- Checks: AVS, CVV2, velocity, device/IP/BIN lists, geolocation, MCC rules.
- 3DS/RBA: Tối đa frictionless, challenge có điều kiện; log đầy đủ outcome.
- Signals/ML: Thiết bị, lịch sử, tần suất; dùng score để quyết định challenge/decline/step-up.
- Chargebacks: Lý do, thời hạn, representment/pre-arb; lưu bằng chứng (invoice, logs, 3DS result).

## 6) Ledger & Reconciliation
- Double-entry: Bút toán đối ứng (cash, receivable, fee, pending/settled); không ghi đơn.
- Pending → Settled: Flag/bảng riêng; không over-release hold; track partial capture.
- Fees & FX: Snapshot tỷ giá, interchange/assessments, markup; lưu đầy đủ để audit.
- Daily recon: So sánh clearing/acquirer reports; bảng `recon_diff` để điều tra.

## 7) Compliance & Security
- PCI DSS: Giảm phạm vi; không lưu PAN thô; tokenization/vault; PAN masking trong log/UI.
- Crypto: TLS mạnh, pinning; P2PE/DUKPT với POS; quản lý khóa/HSM (nếu có).
- PII & Retention: Chính sách giữ/xóa; phân quyền truy cập logs/cấu hình nhạy cảm.

## 8) Observability & SLOs
- Metrics: Approval rate, decline reasons, reversal rate, P95/P99 latency, TPS, error buckets.
- Tracing: Theo `txn_id`/`RRN` xuyên các hops; span cho auth/capture/refund/reversal.
- Logging: Có tương quan (correlation id), không lộ PII; sampling hợp lý.
- Alerts: Spike decline/timeout, reversal tăng, lệch recon, backlog queue, health failures.

## 9) Edge Cases Bắt Buộc
- Duplicate submits: Double-click/refresh/timeout → chặn bằng idempotency trước xử lý.
- Out-of-order: Advice/ack đến trễ; state machine chịu được replays/lệch thứ tự.
- Network partitions: Mark outcome unknown → reversal + schedule reconcile.
- Partial captures/refunds: Đa lần, đúng tổng; không vượt hold; rounding chính xác.
- Offline/fallback: POS offline, deferred clearing; clock skew; DST/UTC xử lý chuẩn hóa.

## 10) Mapping 2PC vào Mini‑Visa
- Prepare = DB `PREPARE TRANSACTION` + Clearing đặt authorization hold.
- Commit = DB `COMMIT PREPARED` + Clearing capture/settle.
- Abort = DB `ROLLBACK PREPARED` + Clearing void/release hold.
- WAL/Recovery: Ghi log PREPARE/DECISION/COMMIT; khởi động đọc WAL → query participants → hoàn tất.
- Metrics: Tổng txn, commit/abort, timeout, reversal, histogram latency, active txns, amount processed.
- Config: JSON hot-reload (timeouts/retries/threads/logging/flags); feature flags tracing/metrics/chaos.
- Runbook: Xử lý `pg_prepared_xacts`, stuck holds, manual void/refund, replay WAL an toàn.

## 11) Testing, Load & Chaos
- Unit/Integration: Parser ISO8583, EMV, idempotency store, 2PC coordinator/participants.
- Simulators: Issuer/acquirer mock, network delay/loss, error codes.
- Load: Target TPS, warm-up, P95/P99, saturation; soak test (memory/leaks, fd usage).
- Chaos/Fault injection: Delay, partition, crash coordinator/participant, disk full; verify A/C/D.

## 12) Vận hành & Runbooks
- Incident playbooks: Unknown outcome → send reversal; backlog queue; circuit trip; degraded mode.
- Reconciliation: Quy trình điều tra chênh lệch; bảng `recon_diff`; SLA xử lý.
- Keys & Certs: Gia hạn chứng chỉ, rotation khóa; secret management.
- Backups & DR: RPO/RTO, thử nghiệm restore định kỳ, kiểm chứng logs/WAL.

---

# 🎯 Lưu Ý Khi Phỏng Vấn C Developer (Payments/2PC)

## A) Kiến thức nền C/Hệ thống
- Chuẩn C (C11/C17), UB, strict aliasing, alignment, `volatile` đúng ngữ cảnh, `restrict`.
- Quản lý bộ nhớ: `malloc/free`, pool/arena allocators, fragmentation; ownership & lifetimes.
- Concurrency: `pthread` (mutex/cond/rwlock), C11 atomics, memory order; deadlock/livelock/starvation.
- Lock-free: Vòng đệm lock-free, ABA problem, hazard pointers/epoch/RCU; false sharing/cacheline.
- Networking: BSD sockets, non-blocking I/O, `epoll/kqueue`, timeouts, backpressure; cơ bản TLS (OpenSSL/mbedTLS).
- Filesystem & I/O: `mmap`, `fsync`, `O_DIRECT`, `O_DSYNC`, buffered vs direct I/O; WAL semantics.
- Signals/Timers: `signalfd`, `timerfd`, `SIGUSR1` hot-reload; safe async-signal patterns.
- Build/Tooling: Make/CMake, pkg-config; clang-tidy/cppcheck; coverage; cross-compilation.
- Debug/QA: `gdb/lldb`, Valgrind, ASan/UBSan/TSan/LSan, perf/flamegraph, fuzzing (libFuzzer/AFL++).

## B) 2PC/Payments Kiến thức ứng dụng
- State machines: Coordinator/participant transitions; tránh giữ global lock khi I/O.
- WAL & Recovery: Ghi buffer + writer thread + `fsync`; replay an toàn; idempotent apply.
- Idempotency: Dedupe store, key design, exactly-once semantics theo business.
- Timeout/Retry: Backoff+jitter, tổng deadline; circuit breaker; handling `unknown outcome` + reversal.
- ISO8583/3DS/EMV cơ bản: Mapping field, `STAN/RRN`, response codes; advice/reversal flows.
- Ledger/Reconcile: Double-entry, pending/settled, fees/FX; daily recon và triage diff.
- Observability: Metrics, logs (không PII), tracing spans; SLOs và alerting.

## C) Bài tập/Thảo luận gợi ý
- Viết vòng đệm lock-free MPSC bằng C11 atomics; phân tích ABA và cách tránh.
- Implement hàm WAL write: buffer → flush → `fsync`; đảm bảo durability và throughput.
- Thiết kế state machine 2PC (coordinator) chịu timeout; pseudo-code và critical sections.
- Parser khung ISO8583 tối giản: đọc MTI/DE3/DE4/DE11/DE37; idempotent dedupe logic.
- Phân tích và sửa deadlock trong đoạn code giữ mutex khi gọi I/O mạng.
- Viết retry handler có backoff+jitter và idempotency; thêm circuit breaker đơn giản.
- Trace/metrics: Thiết kế metrics tối thiểu và span sơ đồ cho auth→commit→reversal.

## D) Red flags / Green flags
- Red flags: Không hiểu UB/atomics; giữ lock quanh I/O; không phân biệt buffered vs durable I/O; log PII; idempotency hời hợt; “retry vô hạn”.
- Green flags: Rành memory/concurrency; biết thiết kế WAL/recovery; nói được về ISO8583/3DS căn bản; có tư duy SLO/observability và runbook.

## E) Câu hỏi nhanh mẫu
- Sự khác nhau giữa `flushing` và `fsync`? Tại sao WAL cần `fsync` ở checkpoint?
- Giải thích memory ordering `release/acquire` với ví dụ queue.
- Làm sao tránh double-charge khi timeout nhưng issuer thật ra đã approve?
- Khi nào dùng `PREPARE TRANSACTION` (Postgres) và rủi ro `COMMIT PREPARED` fail?
- Thiết kế idempotency key cho payment capture partial nhiều lần.

---

## Phụ lục: Gợi ý tích hợp Mini‑Visa
- Thêm file config `config.json` cho timeouts/retries/threads/logging/flags; hỗ trợ `SIGUSR1` hot-reload.
- Bảng `idempotency_keys` và `recon_diff`; chỉ mục theo (`key`,`merchant`,`amount`,`last4`).
- Metrics Prometheus: tổng/commit/abort/reversal/latency buckets/active txns/amount processed.
- Worker reversal: cron/queue xử lý unknown outcome; backoff + dedupe; cảnh báo khi quá ngưỡng.
- Runbooks trong `RUNBOOK_PAYMENTS.md`: prepared txns stuck, manual void/refund, recon triage.

***

Tài liệu này là checklist sống: cập nhật theo thực nghiệm (load test/chaos), sự cố thực tế, và yêu cầu từ Acquirer/VISA.

