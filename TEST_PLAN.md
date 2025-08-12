# mini‑visa Test Plan (VN)

Tài liệu này mô tả kịch bản kiểm thử toàn diện cho dự án mini‑visa, bám theo các milestone và tiêu chí trong PRACTICE_PROJECT.md. Đi kèm là script tổng hợp `tests/run_all.sh` để tự động hoá chạy và báo cáo PASS/FAIL.

## Chuẩn bị môi trường
- Cần PostgreSQL, `psql`, `nc` (netcat) và công cụ build C.
- Build: `make`
- Biến môi trường bắt buộc: `DB_URI` (ví dụ: `postgresql://mini:mini@127.0.0.1:5432/mini_visa`).
- Port mặc định: `9090` (có thể đổi bằng `PORT`).

## Danh sách bài test (tự động hoá trong run_all.sh)

1) Cơ bản (M0)
- Approved hợp lệ: gửi PAN hợp lệ + amount > 0 → nhận `APPROVED`.
- Luhn sai: gửi PAN sai checksum → nhận `DECLINED/luhn_failed`.
- Amount sai: amount == 0 hoặc âm → nhận `DECLINED/amount_invalid`.
- PAN masking trong DB: bản ghi APPROVED có `pan_masked` dạng `######********####`.
- Backpressure: dưới tải lớn, `/metrics` cho thấy `server_busy` tăng, server không crash.

2) Idempotency + Health/Readiness (M1)
- Idempotency: gửi 2 lần cùng `request_id` → DB chỉ có 1 bản ghi; phản hồi ổn định.
- Healthz: `GET /healthz` trả `OK` khi process sống.
- Readyz: `GET /readyz` phản ánh trạng thái DB (`OK`/`NOT_READY`).

3) Keep‑alive, Framing, Logging, Metrics (M2)
- Keep‑alive: gửi 5 JSON liên tiếp trên 1 kết nối → nhận đủ 5 response.
- Partial read: gửi JSON theo 2 phần (trước/sau) → vẫn nhận response hợp lệ.
- Log JSON: 1 dòng/req có `ts,lvl,event,request_id,status,latency_us`.
- `/metrics`: trả JSON snapshot, counters tăng đúng sau khi gửi request.

4) Resilience (M3 – nếu đã triển khai)
- Retry: DB gián đoạn ngắn → thấy `retry_attempt` và phục hồi xử lý.
- Circuit‑breaker: lỗi DB vượt ngưỡng → breaker `Open`, trả nhanh `db_unavailable`, sau đó `Half‑open` và `Close`.

5) Biên & An toàn
- Bad JSON/missing field → `DECLINED/bad_request`.
- Payload quá dài hoặc thiếu newline → từ chối hợp lệ/đóng kết nối (nếu limit đã cấu hình).
- FD limit thấp → không crash; log lỗi accept rõ ràng.
- Không log PAN đầy đủ trong `server.err`.

## Cách chạy
- Khuyến nghị: dùng script tổng hợp `tests/run_all.sh` để tự động chạy tất cả bài test và in bảng tổng kết.
- Ví dụ:
  ```bash
  export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
  ./tests/run_all.sh 9090
  ```
- Tuỳ chọn cấu hình (env): `THREADS`, `QUEUE_CAP`, `PORT`.

## Báo cáo
- Kết quả hiển thị PASS/FAIL theo từng case trên stdout.
- Log server ghi ở `server.err` để điều tra khi FAIL.
- Benchmarks ma trận: chạy `./scripts/bench.sh ...` và ghi ra `reports/results.csv`, tóm tắt trong `reports/RESULTS.md`.

