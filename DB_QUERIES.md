# Sổ tay lệnh PostgreSQL (xem dữ liệu mini-visa)

Tài liệu này tổng hợp các lệnh psql hữu ích để xem dữ liệu, kiểm tra lược đồ, thống kê nhanh và bảo trì (môi trường dev).

## 1) Kết nối
- Dùng biến môi trường:
  - `export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"`
  - `psql "$DB_URI"`
- Kết nối trực tiếp:
  - `psql -h 127.0.0.1 -p 5432 -U mini -d mini_visa`

Mẹo hiển thị:
- Gọn mỗi dòng: thêm `-At -F ' | '` vào lệnh `psql -c "..."`
- Dọc (expand): trong psql: `\x on` (tắt: `\x off`)
- Bật thời gian: `\timing on`

## 2) Khám phá lược đồ
- Liệt kê bảng: `psql "$DB_URI" -c "\\dt"`
- Xem cấu trúc bảng: `psql "$DB_URI" -c "\\d+ public.transactions"`
- Liệt kê index: `psql "$DB_URI" -c "\\di"`
- Kiểm tra quyền: `psql "$DB_URI" -c "\\z public.transactions"`
- Thông tin user/db/search_path:
  - `psql "$DB_URI" -c "SELECT current_user, current_database();"`
  - `psql "$DB_URI" -c "SHOW search_path;"`
  - `psql "$DB_URI" -c "SELECT version();"`

## 3) Xem dữ liệu giao dịch
- 10 bản ghi mới nhất:
  - `psql "$DB_URI" -c "SELECT id, pan_masked, amount, status, created_at FROM transactions ORDER BY id DESC LIMIT 10;"`
- Dạng gọn một dòng/bản ghi:
  - `psql "$DB_URI" -At -F ' | ' -c "SELECT id, pan_masked, amount, status, to_char(created_at,'YYYY-MM-DD HH24:MI:SS') FROM transactions ORDER BY id DESC LIMIT 10;"`
- Dạng dọc (dễ đọc):
  - `psql "$DB_URI" -x -c "SELECT id, pan_masked, amount, status, created_at FROM transactions ORDER BY id DESC LIMIT 5;"`
- Bản ghi trong 5 phút gần đây:
  - `psql "$DB_URI" -c "SELECT id, pan_masked, amount, status, created_at FROM transactions WHERE created_at > now() - interval '5 minutes' ORDER BY id DESC;"`

## 4) Thống kê nhanh
- Tổng số bản ghi: `psql "$DB_URI" -c "SELECT COUNT(*) FROM transactions;"`
- Theo trạng thái: `psql "$DB_URI" -c "SELECT status, COUNT(*) FROM transactions GROUP BY status ORDER BY 2 DESC;"`
- Theo ngày: 
  - `psql "$DB_URI" -c "SELECT date_trunc('day',created_at)::date AS day, status, COUNT(*) FROM transactions GROUP BY 1,2 ORDER BY 1 DESC, 2;"`
- Tổng amount theo ngày (APPROVED):
  - `psql "$DB_URI" -c "SELECT date_trunc('day',created_at)::date AS day, SUM(amount) FROM transactions WHERE status='APPROVED' GROUP BY 1 ORDER BY 1 DESC;"`

## 5) Xuất dữ liệu CSV (dev)
- Xuất 100 bản ghi mới nhất:
  - `psql "$DB_URI" -c "\\copy (SELECT id, pan_masked, amount, status, created_at FROM transactions ORDER BY id DESC LIMIT 100) TO 'transactions_latest.csv' CSV HEADER"`

## 6) Bảo trì dữ liệu (chỉ môi trường dev)
- Xoá sạch bảng và reset ID:
  - `psql "$DB_URI" -c "TRUNCATE TABLE public.transactions RESTART IDENTITY;"`
- Xoá bản ghi cũ (ví dụ >30 ngày):
  - `psql "$DB_URI" -c "DELETE FROM public.transactions WHERE created_at < now() - interval '30 days';"`
- Tối ưu (yêu cầu quyền phù hợp):
  - `psql "$DB_URI" -c "VACUUM ANALYZE public.transactions;"`

## 7) Sequence & quyền
- Tên sequence của cột `id`:
  - `psql "$DB_URI" -c "SELECT pg_get_serial_sequence('public.transactions','id');"`
- Xem quyền chi tiết:
  - `psql "$DB_URI" -c "\\z public.transactions"`
  - `psql "$DB_URI" -c "\\z public.transactions_id_seq"`

## 8) Gỡ rối nhanh
- Lỗi quyền (permission denied): xem `DB_PERMISSIONS.md` và cấp quyền cho user.
- Lỗi xác thực: kiểm tra `DB_URI` (user/pass/host/port/dbname) và `pg_hba.conf`.
- Không thấy bản ghi mới: chỉ `APPROVED` mới được ghi; đảm bảo request hợp lệ (Luhn + amount > 0).

Gợi ý: Nhúng các lệnh hay dùng vào script để kiểm tra nhanh trong quá trình học/làm lab.
