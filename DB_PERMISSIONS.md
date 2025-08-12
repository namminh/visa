# Hướng dẫn cấp quyền PostgreSQL cho mini-visa

Tài liệu này giúp khắc phục lỗi “permission denied for table transactions” khi server kết nối bằng user `mini` nhưng bảng/sequence thuộc sở hữu user khác (thường là `postgres`).

## Bối cảnh
- DB: `mini_visa`
- User ứng dụng: `mini` (kết nối qua biến `DB_URI`)
- Bảng: `public.transactions` (có cột `id SERIAL`, dùng sequence `public.transactions_id_seq`)

## Cách khắc phục nhanh (khuyến nghị)
Chạy các lệnh dưới đây với quyền `postgres`:
```bash
sudo -u postgres psql -d mini_visa -c "GRANT CONNECT ON DATABASE mini_visa TO mini;"
sudo -u postgres psql -d mini_visa -c "GRANT USAGE ON SCHEMA public TO mini;"

# Chuyển owner và cấp quyền trên bảng
sudo -u postgres psql -d mini_visa -c "ALTER TABLE public.transactions OWNER TO mini;"
sudo -u postgres psql -d mini_visa -c "GRANT SELECT,INSERT,UPDATE,DELETE ON TABLE public.transactions TO mini;"

# Tìm tên sequence của cột id (thường là public.transactions_id_seq)
sudo -u postgres psql -d mini_visa -c "SELECT pg_get_serial_sequence('public.transactions','id');"
# Đổi owner và cấp quyền trên sequence
sudo -u postgres psql -d mini_visa -c "ALTER SEQUENCE public.transactions_id_seq OWNER TO mini;"
sudo -u postgres psql -d mini_visa -c "GRANT USAGE,SELECT ON SEQUENCE public.transactions_id_seq TO mini;"
```

## Phương án cấp quyền rộng (tùy chọn)
Áp dụng cho mọi bảng/sequence trong schema `public`:
```bash
sudo -u postgres psql -d mini_visa -c "GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO mini;"
sudo -u postgres psql -d mini_visa -c "GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO mini;"
```

## Cách “sạch” nhất (sở hữu object ngay từ đầu)
Tạo schema/tables bằng chính user `mini` để mọi object thuộc `mini`:
```bash
# Đảm bảo user mini có quyền trên schema public
sudo -u postgres psql -d mini_visa -c "GRANT USAGE,CREATE ON SCHEMA public TO mini;"

# Chạy schema bằng user mini
psql "postgresql://mini:mini@127.0.0.1:5432/mini_visa" -f db/schema.sql
```
Tùy chọn nâng cao: tạo schema riêng (ví dụ `mv`) cho ứng dụng và đặt `search_path`.

## Xác minh
- Gửi một giao dịch hợp lệ để tạo bản ghi APPROVED:
```bash
./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"10.00"}'
```
- Kiểm tra dữ liệu trong DB:
```bash
psql "$DB_URI" -At -F ' | ' -c "SELECT id, pan_masked, amount, status, to_char(created_at,'YYYY-MM-DD HH24:MI:SS') FROM transactions ORDER BY id DESC LIMIT 5;"
```

## Gỡ rối thường gặp
- Tìm sequence đang dùng: `SELECT pg_get_serial_sequence('public.transactions','id');`
- Xem cấu trúc bảng: `psql "$DB_URI" -c "\\d+ public.transactions"`
- Lỗi xác thực (password): kiểm tra `pg_hba.conf` và biến `DB_URI`.

## Lưu ý bảo mật
- Chỉ cấp quyền cần thiết cho user ứng dụng (`SELECT/INSERT`, và `USAGE` trên sequence). Hạn chế `DELETE/UPDATE` nếu không dùng.
- Trên môi trường production, tạo role riêng (ví dụ `mini_app`) và gán quyền cho role thay vì cấp trực tiếp cho user.
