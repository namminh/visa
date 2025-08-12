# Hướng dẫn sử dụng mini-visa (ngắn gọn, dễ hiểu)

Tài liệu này giúp bạn chạy nhanh dự án mini-visa: build server, kết nối PostgreSQL, và chạy công cụ tạo tải (loadgen).

## 1) Yêu cầu môi trường
- Hệ điều hành: Debian/Ubuntu (hoặc tương đương)
- Gói cần cài: `build-essential`, `libpq-dev`, `valgrind` (tùy chọn), `postgresql`

Cài nhanh (Ubuntu/Debian):
```bash
sudo apt update && sudo apt install -y build-essential libpq-dev valgrind postgresql
```

## 2) Chuẩn bị CSDL
Tạo database và user Postgres (ví dụ đơn giản):
```bash
sudo -u postgres psql -c "CREATE DATABASE mini_visa;"
sudo -u postgres psql -c "CREATE USER mini WITH PASSWORD 'mini';"
sudo -u postgres psql -d mini_visa -f db/schema.sql
sudo -u postgres psql -d mini_visa -f db/seed.sql   # tùy chọn
```

Kết nối server dùng biến môi trường `DB_URI`, ví dụ:
```
postgresql://mini:mini@127.0.0.1:5432/mini_visa
```

## 3) Build nhanh
Tại thư mục gốc dự án:
```bash
make            # tạo cả server và client (loadgen)
```
File sinh ra nằm trong `build/`: `build/server` và `build/loadgen`.

Mẹo cấu hình hiệu năng (tuỳ chọn):
- Số luồng xử lý: `export THREADS=8`
- Kích thước hàng đợi job: `export QUEUE_CAP=2048`
- Cổng lắng nghe: `export PORT=9090`

## 4) Chạy server
Cách 1 (khuyên dùng – qua script):
```bash
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
export PORT=9090   # tùy chọn, mặc định 9090
./scripts/run.sh
```
Cách 2 (chạy trực tiếp binary):
```bash
DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa" ./build/server
```

## 5) Gửi tải thử nghiệm (client/loadgen)
Ví dụ: 100 kết nối, mỗi kết nối 1000 yêu cầu, gửi tới cổng 9090:
```bash
./build/loadgen 100 1000 9090
```
Hoặc dùng script stress:
```bash
CONNS=100 REQS=1000 PORT=9090 ./tests/stress.sh
```

## 6) Script hữu ích
- `scripts/backup.sh`: backup database `mini_visa` (giữ 7 bản gần nhất)
- `scripts/run.sh`: build (nếu cần) và khởi động server (dùng `DB_URI`, `PORT`)
- `scripts/tail-errs.sh <logfile>`: tail log và lọc lỗi (`ERROR`, `DBERR`, ...)
- `tests/stress.sh`: chạy loadgen nhanh với biến `CONNS`, `REQS`, `PORT`
- `tests/chaos.sh`: tạm dừng/tiếp tục tiến trình server để mô phỏng sự cố

## 7) Cấu trúc thư mục
```
server/   mã nguồn C cho server (thread pool, network I/O, handlers)
client/   công cụ tạo tải (C)
db/       schema và dữ liệu mẫu
scripts/  tiện ích chạy/backup/giám sát
tests/    kịch bản stress/chaos
Makefile  luật build
```

## 8) Khắc phục sự cố nhanh
- Không đặt `DB_URI`: script báo lỗi. Hãy export `DB_URI` đúng chuỗi kết nối.
- Thiếu thư viện `libpq`: cài `libpq-dev` rồi `make` lại.
- Server chạy nhưng client không kết nối: kiểm tra `PORT`, firewall, và server có đang listen.
- Lỗi runtime: chạy lại bằng `valgrind` để kiểm tra rò rỉ bộ nhớ.

## 9) Lệnh nhanh thường dùng
```bash
make clean && make              # build sạch
PORT=9090 ./scripts/run.sh      # chạy server
./build/loadgen 50 500 9090     # bắn tải nhẹ
./scripts/backup.sh             # backup database
```

Gợi ý: đọc các file `.c` trong `server/` (có TODO) để hoàn thiện tính năng.
