# HƯỚNG DẪN MICROservice CHO MINI‑VISA (BẢN DỄ HIỂU)

Tài liệu này giúp bạn hình dung rõ “microservice trong C” áp dụng cho hệ thống thanh toán. Không có phép màu: mỗi dịch vụ là một tiến trình/binary độc lập, nói chuyện qua mạng bằng hợp đồng API rõ ràng, mỗi dịch vụ tự chịu trách nhiệm dữ liệu và vòng đời của mình.

---

## 1) Mục Tiêu
- Dễ hiểu, dễ chạy cục bộ, tiến từng bước từ monolith → micro.
- Nắm vững: idempotency (request_id), saga/compensation (không dùng 2PC xuyên service), quan sát (logs/metrics), độ tin cậy (timeout/retry/CB).

---

## 2) Trước & Sau (Monolith vs Microservice)
- Monolith hiện tại: 1 binary C lắng nghe TCP; xử lý JSON; có 2PC (DB + clearing mô phỏng) trong tiến trình.
- Microservice: tách “biên tự nhiên” thành dịch vụ nhỏ, gọi nhau qua HTTP/gRPC; sự kiện bất đồng bộ qua message bus.

Sơ đồ đơn giản:
```
[API Gateway]
    |  (HTTP)
[Payments]  ----(HTTP)--->  [Risk]
    |      |   \--(HTTP)-->  [Clearing]
    |                         |
    |                    (events)
    |                         v
 (events)                 [Reversal]
    v                           [Ledger] <----(events)-----   (void/credit)
    |
 [Query]
```

---

## 3) Các Dịch Vụ (tối thiểu)
- Payments: nhận authorize/capture, idempotency theo `request_id`, phát sự kiện.
- Risk: quyết định allow/deny (rule đơn giản, ví dụ amount threshold hoặc BIN blacklist).
- Clearing: mô phỏng mạng thẻ; expose `/prepare|/commit|/abort`.
- Reversal: tiêu thụ sự kiện `reversal.enqueued` để bù khi commit thất bại.
- Ledger: ghi bút toán kép từ event `payment.authorized|captured`.
- Query: chỉ đọc (lookup transaction), tránh ảnh hưởng đường giao dịch.

Mỗi service có DB/schema riêng (bounded context). Không chia sẻ bảng trực tiếp.

---

## 4) Hợp Đồng API (rút gọn, dễ thử bằng curl)
- Payments
  - POST `/payments/authorize`
    - Req: `{ "request_id": "abc123", "pan": "4111...", "amount": "10.00", "currency": "USD", "merchant": "M001" }`
    - 200: `{ "status": "APPROVED", "txn_id": "visa_..." }`
    - 409 (idempotent): `{ "status": "APPROVED", "idempotent": true, "txn_id": "..." }`
    - 4xx/5xx: `{ "status": "DECLINED", "reason": "..." }`
- Risk
  - POST `/risk/evaluate` → `{ "allow": true, "reason": "ok" }`
- Clearing
  - POST `/clearing/prepare` → `{ "ok": true }`
  - POST `/clearing/commit`  → `{ "ok": true }`
  - POST `/clearing/abort`   → `{ "ok": true }`
- Query
  - GET  `/tx?request_id=...` → `{ "request_id":"...","amount":"...","status":"..." } | { "status":"NOT_FOUND" }`

Giao tiếp nội bộ có thể dùng Bearer token hoặc mTLS. Public API đi qua API Gateway.

---

## 5) Dữ Liệu & Nhất Quán (Idempotency + Saga)
- Payments DB
  - `transactions(id, request_id UNIQUE, pan_masked, amount, currency, merchant, status, created_at)`
  - `outbox(id, topic, payload_json, created_at, published_at NULL)`
- Clearing DB
  - `clearing_state(txn_id PK, phase ENUM(init,prepared,committed,aborted), last_error, updated_at)`
- Ledger DB
  - `journal(id, txn_id, debit, credit, amount, currency, created_at)`

Luồng authorize (không 2PC xuyên service):
1) Payments validate + Risk allow.
2) Payments gọi Clearing.prepare (idempotent theo `txn_id`).
3) Ghi `transactions` (status="APPROVED") + ghi `outbox` → publisher đẩy event `payment.authorized`.
4) Payments gọi Clearing.commit (idempotent theo `txn_id`).
5) Nếu commit thất bại → phát `reversal.enqueued` → Reversal bù.

Idempotency:
- `request_id` (Payments) đảm bảo retry client không tạo giao dịch mới.
- `txn_id` (Clearing) đảm bảo prepare/commit/abort có thể lặp lại an toàn.

---

## 6) Cụ Thể Bằng C: Thư Viện & Mẫu Mã
- HTTP client: libcurl (dễ, ổn định, có timeout/TLS)
- HTTP server: civetweb/mongoose hoặc libmicrohttpd (gọn, dễ nhúng)
- JSON: jansson hoặc cJSON
- MQ/Event: Redis Streams (dễ chạy) → nâng cấp Kafka (librdkafka) khi quen

Ví dụ gọi HTTP POST JSON bằng libcurl (Payments → Clearing):
```
CURL *h = curl_easy_init();
struct curl_slist *hdr = NULL;
hdr = curl_slist_append(hdr, "Content-Type: application/json");
curl_easy_setopt(h, CURLOPT_URL, "http://clearing:8080/clearing/prepare");
curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
curl_easy_setopt(h, CURLOPT_POST, 1L);
curl_easy_setopt(h, CURLOPT_POSTFIELDS, body_json); // ví dụ: {"txn_id":"visa_...","amount":"10.00"}
curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 1000L); // timeout 1s
CURLcode rc = curl_easy_perform(h);
long code = 0; curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
curl_slist_free_all(hdr); curl_easy_cleanup(h);
```

Server nhỏ với civetweb (Clearing):
- Đăng ký handler `/clearing/prepare|commit|abort`, lấy body từ `hm->body`, parse JSON, trả `{ "ok": true }`.
- Đặt timeout socket, trả mã lỗi rõ ràng.

---

## 7) Lộ Trình Học 4 Sprint (thực dụng)
- Sprint 1: Tách Clearing Service (C)
  - Viết `clearingd` lắng nghe 8080; 3 route: prepare/commit/abort.
  - Sửa Payments dùng libcurl gọi Clearing thay vì in‑proc.
  - Thêm circuit breaker đơn giản (đếm lỗi, open trong vài giây).
- Sprint 2: Outbox + Publisher
  - Thêm bảng `outbox`; thread publisher đọc outbox và đẩy sang Redis Streams hoặc log.
  - Reversal tiêu thụ `reversal.enqueued`.
- Sprint 3: Risk Service
  - Service HTTP `/risk/evaluate` với rule đơn giản; Payments gọi trước Clearing; thêm cache TTL ngắn.
- Sprint 4: Ledger + Query
  - Ledger tiêu thụ `payment.authorized` để ghi journal; Query cung cấp `/tx?request_id` từ DB riêng.

Mỗi sprint đều thêm: `/healthz`, `/readyz`, `/metrics`; logs JSON 1 dòng; kịch bản test bằng curl.

---

## 8) Triển Khai Cục Bộ (gợi ý)
- docker-compose gồm: `payments`, `clearingd`, `postgres_payments`, `postgres_clearing`, `redis`.
- Env ví dụ:
  - `PAYMENTS_DB_URI=postgresql://...`
  - `CLEARING_URL=http://clearingd:8080`
  - `API_TOKEN=secret123`
- Healthchecks: HTTP GET `/healthz` cho từng service.

Bạn có thể bắt đầu mà chưa cần Kafka: dùng Redis Streams hoặc thậm chí ghi file để thấy luồng event hoạt động.

---

## 9) Checklist Chất Lượng
- Timeout & retry (client + server), có jitter.
- Circuit breaker + backpressure (bounded threadpool/queue).
- Idempotency ở Payments (request_id) + Clearing (txn_id).
- Logs JSON (ts, lvl, event, request_id/txn_id, latency_us), không log PAN thô (mask 6+4).
- Metrics RED + domain (approved/declined/server_busy/commit_failed/reversal_*).
- Bảo mật: Bearer/mTLS, secrets quản lý an toàn, PCI scope tối thiểu.
- Test: contract tests, fault injection (timeout, partial failure), idempotency.

---

## 10) Q&A Nhanh
- Vì sao không 2PC giữa các service? Vì tốn kém và dễ lỗi; Saga + outbox thực dụng hơn.
- Nếu Clearing down thì sao? Circuit breaker mở; Payments trả lỗi + enque reversal để bù khi cần.
- Có cần gRPC không? HTTP/JSON đủ cho học; nâng cấp gRPC khi cần hiệu năng/IDL mạnh.

---

## 11) Bước Tiếp Theo
- Nếu bạn muốn, mình sẽ scaffold `clearingd` (C) mẫu + adapter libcurl trong Payments, kèm docker-compose tối thiểu để bạn chạy thử bằng `curl`.
