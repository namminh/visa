# MÔ TẢ HỆ THỐNG THANH TOÁN BÙ TRỪ NAPAS (QUÉT QR → CHUYỂN TIỀN)

Tài liệu mô tả đơn giản, dễ hiểu về luồng thanh toán qua NAPAS khi người dùng mở app ngân hàng, quét QR (VietQR/NAPAS), đọc được tài khoản + ngân hàng thụ hưởng và thực hiện chuyển tiền 24/7.

---

## 1) Tổng Quan Kiến Trúc

Ký hiệu: "––HTTP––>" là gọi API đồng bộ; "==ISO8583/ISO20022==>" là thông điệp liên ngân hàng; "⟂" là bù trừ/settlement.

```
[User Mobile App]
   |
   | ––HTTP––> [Issuer Mobile Backend/API] ––> [Issuer Switch/Payment Hub]
   |                                            |        |
   |                                            |        ==ISO8583/ISO20022==>  [NAPAS Switch]
   |                                            |                                 |
   |                                            |                                 v
   |                                            |                         [Beneficiary Bank Switch]
   |                                            |                                 |
   |                                            |                          ––Core posting––> [Beneficiary Core]
   |                                            |                                 |
   |                                            |                             Credit Account
   v                                            |                                 |
[Merchant/Payee]  <–– (notif/webhook optional) ––+                                 |
                                                                                   |
                                          [NAPAS Clearing/Netting Engine] ––⟂––> [SBV Settlement A/C]
```

- Issuer: ngân hàng người trả.
- Beneficiary: ngân hàng người nhận.
- NAPAS: chuyển mạch & bù trừ liên ngân hàng; settlement qua NHNN (SBV).

---

## 2) QR VietQR (NAPAS) – Trường Chính

- AID VietQR, BIN ngân hàng thụ hưởng (NAPAS), số tài khoản/alias.
- Số tiền (dynamic QR) + tham chiếu/memo + CRC.
- QR static: chứa ngân hàng + tài khoản; người dùng tự nhập số tiền.
- QR dynamic: kèm sẵn số tiền, OrderId/InvoiceId để đối soát tự động.

---

## 3) Luồng Thành Công (Real-time Credit)

1. App quét QR → decode: {bank_to(BIN), account_to, amount?, ref?}.
2. Người dùng xác nhận + SCA (PIN/OTP/biometrics).
3. App ––HTTP––> Issuer Backend: tạo lệnh (request_id idempotent).
4. Issuer Risk/AML: kiểm tra limit/velocity/blacklist.
5. Issuer Switch gửi liên ngân hàng:
   - ISO 8583 0200 hoặc ISO 20022 pacs.008 tới NAPAS.
6. NAPAS định tuyến tới Beneficiary; Beneficiary Core hạch toán có (credit) → trả Approved (0210/pacs.002).
7. Issuer ghi nợ (debit) tài khoản người trả (có thể hold trước), trả 200 cho App.
8. (Tuỳ chọn) Gửi webhook/Push cho Merchant nếu QR dynamic có endpoint.
9. Lưu vết & đối soát: request_id, napas_msg_id, issuer_txn_id, bene_txn_id.
10. Bù trừ: NAPAS tính vị thế ròng, settlement qua NHNN.

---

## 4) Luồng Lỗi & Bù (Compensation)

- Beneficiary down/timeout: NAPAS/Beneficiary trả lỗi → Issuer không ghi nợ; trả Declined.
- Beneficiary credit OK nhưng Issuer ghi nợ fail (hiếm): dùng reversal/void
  - ISO 8583 0420/0430 hoặc ISO 20022 pacs.004.
  - Tạo sự kiện `reversal.enqueued` để worker bù; xử lý idempotent theo `txn_id`.

---

## 5) Chuẩn Thông Điệp (Mapping Rút Gọn)

- ISO 8583 0200/0210: MTI 0200 (request), 0210 (response)
  - F2 PAN/alias; F4 amount; F7 date; F11 STAN; F32 from_bank; F33 to_bank; F37 RRN; F41 terminal; F102 account_from; F103 account_to; F123/127 private fields.
- ISO 20022 pacs.008/pacs.002/pacs.004: chuyển tiền/ACK/Reversal
  - MsgId, EndToEndId(request_id), InstdAmt, Dbtr/Cdtr, DbtrAgt/CdtrAgt (BIC/BIN), RmtInf (remittance).

Gợi ý: giữ `request_id` ở EndToEndId; `txn_id` nội bộ map sang RRN/MsgId để tra soát.

---

## 6) Yêu Cầu Kỹ Thuật Cốt Lõi

- Idempotency: `request_id` tại Issuer; `txn_id` end-to-end cho tra soát.
- Bảo mật: TLS, có thể ký số; không log dữ liệu nhạy cảm (mask 6+4).
- Timeout/Retry: SLA real-time ~ vài giây; retry có backoff + jitter; circuit breaker với đối tác.
- Observability: logs JSON (request_id/txn_id/latency), `/metrics` (approved/declined/timeout/reversal), tracing xuyên hop.
- Đối soát: đối chiếu cặp (RRN/MsgId) giữa Issuer–NAPAS–Beneficiary; hàng chờ ngoại lệ.

---

## 7) Kiến Trúc Microservice Gợi Ý (Triển khai nội bộ)

- QR Decoder (App/Backend): decode VietQR; kiểm tra CRC; tạo yêu cầu.
- Payments API (Issuer): nhận authorize, quản lý idempotency, phát sự kiện.
- Risk: rule/AML.
- NAPAS Adapter: build/parse ISO8583/ISO20022, retry + circuit breaker.
- Reversal Worker: tiêu thụ `reversal.enqueued` để bù khi cần.
- Reconciliation: đối soát tự động, xuất lệch.
- Notification: webhook/Push cho merchant.

Sự kiện (Kafka/NATS): `payment.authorized`, `payment.failed`, `reversal.enqueued`, `reversal.succeeded`.

---

## 8) Test & Thực hành

- Contract test với mock NAPAS/Beneficiary: Approved, Declined, Timeout.
- Fault injection: drop network, delay, duplicate message.
- Idempotency test: lặp `request_id` và `txn_id` → kết quả ổn định.
- Đối soát: sinh dữ liệu lệch có chủ đích → kiểm tra hàng chờ ngoại lệ.

---

## 9) Thuật ngữ

- Issuer: ngân hàng người trả. Beneficiary: ngân hàng người nhận.
- RRN: Retrieval Reference Number. STAN: System Trace Audit Number.
- EndToEndId (ISO20022): mang `request_id` để theo dõi end-to-end.
- Settlement: quyết toán điều chỉnh tài khoản thanh toán tại NHNN.

---

Gợi ý mở rộng: thêm phụ lục format VietQR (EMVCo tags) và ví dụ message ISO 8583/20022 mẫu theo case thực tế.

---

## 10) Switching (Chuyển mạch) chi tiết qua NAPAS

### 10.1 Chức năng switch
- Định tuyến: chọn ngân hàng thụ hưởng theo BIN/BIC, giữ cặp tương quan STAN/RRN (8583) hoặc MsgId/EndToEndId (20022).
- Xác thực/thẩm tra: kiểm tra phiên (sign‑on), khoá/MAC (8583), schema/chữ ký (20022 khi áp dụng), format số tiền/currency/private fields.
- Chống trùng: phát hiện duplicate theo (STAN+NII+Amt+Date/Time) hoặc (MsgId/EndToEndId) trong SLA nhất định.
- Chuẩn hoá mã phản hồi (RC) khi trả về Issuer; ghi vết đầy đủ hop in/out.

### 10.2 Luồng ISO 8583 (0200/0210; reversal 0420/0430)
- Thiết lập mạng: 0800/0810 Sign‑on (DE70=001); 0800/0810 Echo/Key change (DE70=302/301); 0820 Logoff.
- 0200 Issuer → NAPAS: DE11 STAN; DE37 RRN; DE24 NII; DE32/33/100 mã tổ chức; DE4 amount; DE7 date/time; DE41 terminal; DE102/103 account; DE60/62 private; DE64/128 MAC.
- NAPAS định tuyến 0200 → Benef: bảo toàn STAN/RRN; điều chỉnh private fields nếu cần.
- 0210 Benef → NAPAS → Issuer: DE39 RC=00/xx; giữ STAN/RRN; MAC hợp lệ.
- Timeout/unknown: quá T1 (ví dụ 10–15s), trả RC 68/91; Issuer chủ động 0420 (DE90 Original Data) để bù; 0430 phản hồi.
- Chống trùng: loại trùng theo (DE11+NII+Amt+Date) hoặc (RRN); Benef idempotent theo RRN/Ref.

### 10.3 Luồng ISO 20022 (pacs.008/pacs.002; pacs.004)
- Issuer → NAPAS: pacs.008.001.x với MsgId (duy nhất), EndToEndId (map request_id), InstdAmt, Dbtr/Cdtr, DbtrAcct/CdtrAcct, DbtrAgt/CdtrAgt, RmtInf.
- NAPAS → Benef: forward pacs.008 (có thể thêm ClearingSystem ref).
- Benef → NAPAS → Issuer: pacs.002 ACK/ACCP/RJCT, giữ nguyên MsgId/EndToEndId.
- Reversal/Return: pacs.004 khi cần hoàn/bù giao dịch.
- Chống trùng: dựa MsgId/EndToEndId + thời gian.

### 10.4 Timeout, retry, idempotency (3 tầng)
- Issuer: App timeout 5–10s; backend 10–15s; retry có backoff+jitter; luôn idempotent theo request_id/EndToEndId.
- Switch (NAPAS): khi thiếu 0210/pacs.002 kịp thời → trả timeout, ghi unknown outcome; yêu cầu 0420/pacs.004 nếu lệch hạch toán.
- Benef: idempotent theo RRN/MsgId; duplicate phải trả kết quả cũ, không hạch toán lần 2.

### 10.5 Bảo mật & khoá
- ISO 8583: TLS/IPSec + MAC (DE64/128) với khoá phiên (HSM quản lý); 0800 key change theo lịch.
- ISO 20022: TLS bắt buộc; có thể ký số XML; quản lý chứng thư/khoá riêng.
- Không log dữ liệu nhạy cảm; mask PAN 6+4; bảo toàn request_id/txn_id/MsgId trong log.

### 10.6 Trường định tuyến
- 8583: DE24 (NII), DE32/33 (acquiring/forwarding), DE100 (receiving) hoặc private fields chứa BIN/BIC.
- 20022: DbtrAgt/CdtrAgt (BIC/BIN) + ClearingSystemId.
- NAPAS có bảng route theo BIN/BIC, sản phẩm, thời gian, trạng thái đối tác.

### 10.7 Clearing & settlement
- Real‑time credit online; netting theo chu kỳ (intra‑day/EoD).
- NAPAS tổng hợp vị thế ròng → lệnh settlement qua NHNN điều chỉnh tài khoản thanh toán; báo cáo đối soát Issuer/Benef.

### 10.8 Khuyến nghị triển khai
- EndToEndId = request_id để tra soát end‑to‑end.
- Lưu cặp tương quan: STAN, RRN, MsgId, EndToEndId, napas_msg_id, bene_ref.
- Hàng chờ ngoại lệ + reversal worker (tự động 0420/pacs.004).
- Circuit breaker + echo test (0800/302) theo dõi đường kết nối.
