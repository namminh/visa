# 🛠️ RUNBOOK — Mini‑Visa Payments Ops

Tài liệu thao tác nhanh cho vận hành: xử lý outcome không chắc chắn, prepared txns bị kẹt, circuit breaker mở, và kiểm tra sức khỏe hệ thống.

---

## 1) Unknown Outcome → Reversal
Triệu chứng
- `/metrics`: `twopc_aborted` tăng, `reversal_enqueued` tăng.
- Log: `commit_failed` và `reversal enqueued` theo `txn_id`.

Xử lý
- Reversal worker chạy tự động (backoff, tối đa `REVERSAL_MAX_ATTEMPTS`).
- Theo dõi tiến trình:
  - `tail -f server.err | grep -i reversal`
  - `/metrics`: `reversal_succeeded` tăng dần.
- Nếu nhiều reversal thất bại liên tiếp: kiểm tra clearing/downstream → xem mục 3.

Tạm thời tăng/aggressive retry (thận trọng)
```
export REVERSAL_MAX_ATTEMPTS=10
export REVERSAL_BASE_DELAY_MS=200
```

---

## 2) Postgres Prepared Transactions bị kẹt
Triệu chứng
- 2PC commit fail ở clearing, DB còn prepared txns.
- DB storage tăng; `pg_prepared_xacts` có nhiều dòng.

Kiểm tra
```sql
SELECT * FROM pg_prepared_xacts;
-- name dạng 'visa_<txn_id>'
```

Quyết định
- Nếu clearing đã settle → COMMIT PREPARED.
- Nếu clearing không settle/đã void → ROLLBACK PREPARED.

Thao tác (qua psql)
```sql
-- Commit/rollback theo từng txn name
COMMIT PREPARED 'visa_<txn_id>';
ROLLBACK PREPARED 'visa_<txn_id>';
```

Phòng ngừa
- Giữ reversal worker hoạt động.
- Trong incident, có thể tạm dừng traffic; xử lý backlog prepared trước khi mở lại.

---

## 3) Circuit Breaker (Clearing) mở
Triệu chứng
- `/metrics`: `clearing_cb_short_circuit` tăng.
- Log WARN: "Circuit open: short-circuit PREPARE/COMMIT".

Xử lý
1) Kiểm tra downstream (endpoint clearing) có sự cố? mạng? DNS? TLS?
2) Tăng thời gian mở để giảm áp lực:
```
export CLEARING_CB_OPEN_SECS=60
```
3) Giảm ngưỡng kích hoạt khi cần kín kẽ hơn hoặc tăng cửa sổ nếu báo động giả:
```
export CLEARING_CB_FAILS=5
export CLEARING_CB_WINDOW=30
```
4) Giảm/tăng retry:
```
export CLEARING_RETRY_MAX=2
export CLEARING_TIMEOUT=5
```

Khi nào đóng breaker thủ công?
- Breaker tự chuyển half‑open sau `CLEARING_CB_OPEN_SECS`. Không cần can thiệp nếu downstream hồi phục.

---

## 4) DB/Readyz và Sức khỏe
Quick checks
```
printf 'GET /healthz\n' | nc 127.0.0.1 9090
printf 'GET /readyz\n'  | nc 127.0.0.1 9090
printf 'GET /metrics\n' | nc 127.0.0.1 9090
```

Nếu `/readyz` NOT_READY
- Kiểm tra DB_URI, mạng tới Postgres, quyền user `mini`.
- Thử truy vấn tay với cùng DSN.

---

## 5) Hiệu năng & Backpressure
Triệu chứng
- Latency tăng, declined `server_busy` tăng.

Điều chỉnh
```
export THREADS=8
export QUEUE_CAP=2048
```

Quan sát
- `/metrics`: `total/approved/declined/server_busy`.
- `tail -f server.err` để xem INFO tx latency_us.

---

## 6) Reconciliation (Đối soát)
Mục tiêu
- Đảm bảo DB `transactions` khớp với clearing/settlement báo về.

Thực hành
- Lưu file report từ acquirer (mô phỏng) → so sánh từng RRN/amount/status.
- Với chênh lệch: kiểm tra `pg_prepared_xacts`, log `txn_id`, và `reversal_*`.

---

## 7) Checklist Khi Sự Cố
- [ ] Xem `/metrics` để định hình lỗi (2PC, breaker, reversal, server_busy).
- [ ] `tail -f server.err` và `logs/transactions.log` (theo `txn_id`).
- [ ] Kiểm tra Postgres: kết nối, `pg_prepared_xacts`.
- [ ] Kiểm tra clearing: mạng/tên miền/chứng chỉ/logs.
- [ ] Tạm giảm tải (scale‑in threads/queue) nếu cần hạ nhiệt.
- [ ] Sau khi phục hồi: theo dõi `reversal_succeeded` tăng, prepared txns về 0.

---

## 8) Biến Môi Trường Tóm Tắt
- 2PC: `TWOPC_PREPARE_TIMEOUT`, `TWOPC_COMMIT_TIMEOUT`
- Clearing: `CLEARING_TIMEOUT`, `CLEARING_RETRY_MAX`, `CLEARING_CB_WINDOW`, `CLEARING_CB_FAILS`, `CLEARING_CB_OPEN_SECS`
- Reversal: `REVERSAL_MAX_ATTEMPTS`, `REVERSAL_BASE_DELAY_MS`
- Core: `DB_URI`, `PORT`, `THREADS`/`NUM_THREADS`, `QUEUE_CAP`

