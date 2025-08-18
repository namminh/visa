# ⚡ **CHUYÊN GIA EXPRESS: 2 Ngày Zero-to-Hero Guide**

## 📋 **TÓM TẮT NHANH**
- **Ngày 1**: Làm để hiểu (8h) - Muscle memory + Pattern recognition  
- **Ngày 2**: Chuyên gia hóa (8h) - Advanced experiments + Interview prep
- **Mục tiêu**: Từ newbie → confident interview threads/networking

---

# 📅 **NGÀY 1: "LÀM ĐỂ HIỂU" (8 giờ)**

## ⏰ **MORNING SESSION (4h)**

### **09:00-10:30 (1.5h): Speed Run Labs**

**Chuẩn bị:**
```bash
cd /mnt/d/visa
make clean && make
```

**Test 1: Baseline** ⏱️ *10 phút*
```bash
THREADS=4 QUEUE_CAP=32 DB_URI='postgresql://mini:mini@127.0.0.1:5432/mini_visa' PORT=9090 ./scripts/run.sh 2>server.err & echo $! > server.pid
sleep 2
./build/loadgen 50 200 9090
```
**📝 GHI NGAY**: `RPS=____`, `p95=____us`
```bash
kill $(cat server.pid) && rm -f server.pid
```

**Test 2: Constrained** ⏱️ *10 phút*  
```bash
THREADS=1 QUEUE_CAP=1 DB_URI='postgresql://mini:mini@127.0.0.1:5432/mini_visa' PORT=9090 ./scripts/run.sh 2>server.err & echo $! > server.pid
sleep 2
./build/loadgen 50 200 9090
printf 'GET /metrics\r\n' | nc 127.0.0.1 9090
```
**📝 GHI NGAY**: `RPS=____`, `p95=____us`, `server_busy=____`
```bash
kill $(cat server.pid) && rm -f server.pid
```

**Test 3: High Capacity** ⏱️ *10 phút*
```bash
THREADS=8 QUEUE_CAP=1024 DB_URI='postgresql://mini:mini@127.0.0.1:5432/mini_visa' PORT=9090 ./scripts/run.sh 2>server.err & echo $! > server.pid
sleep 2
./build/loadgen 50 200 9090
```
**📝 GHI NGAY**: `RPS=____`, `p95=____us`
```bash
kill $(cat server.pid) && rm -f server.pid
```

**Burst Test Demo** ⏱️ *15 phút*
```bash
THREADS=1 QUEUE_CAP=1 DB_URI='postgresql://mini:mini@127.0.0.1:5432/mini_visa' PORT=9090 ./scripts/run.sh 2>server.err & echo $! > server.pid
sleep 2
for i in $(seq 1 20); do ./tests/send-json.sh 9090 '{"pan":"4111111111111111","amount":"1.00"}' & done; wait
printf 'GET /metrics\r\n' | nc 127.0.0.1 9090
```
**📝 QUAN SÁT**: Có bao nhiều `"server_busy"` responses?

### **10:30-12:00 (1.5h): Pattern Recognition**

**📊 So sánh kết quả** ⏱️ *30 phút*

| Config | THREADS | QUEUE_CAP | RPS | p95 (us) | server_busy | Nhận xét |
|--------|---------|-----------|-----|----------|-------------|----------|
| Baseline | 4 | 32 | ___ | ___ | ___ | ___ |
| Constrained | 1 | 1 | ___ | ___ | ___ | ___ |
| High Cap | 8 | 1024 | ___ | ___ | ___ | ___ |

**🎯 TÌM SWEET SPOT** ⏱️ *30 phút*
Thử các config khác để tìm optimal:
```bash
# Test thêm 2-3 config
THREADS=4 QUEUE_CAP=64 # Test này
THREADS=2 QUEUE_CAP=16 # Và config này
```

**🧠 PATTERN QUESTIONS** ⏱️ *30 phút*
1. THREADS tăng → RPS tăng hay giảm?
2. QUEUE_CAP lớn → p95 tăng hay giảm khi overload?
3. Config nào cho server_busy nhiều nhất?
4. Sweet spot của bạn: THREADS=? QUEUE_CAP=?

---

## ⏰ **AFTERNOON SESSION (4h)**

### **13:00-14:30 (1.5h): Anchor Speed Reading**

**Quick Code Survey** ⏱️ *45 phút*
```bash
# Chỉ đọc comments, KHÔNG đọc implementation code
grep -n -A3 -B1 "\[ANCHOR:" server/threadpool.c
grep -n -A3 -B1 "\[ANCHOR:" server/net.c  
grep -n -A3 -B1 "\[ANCHOR:" server/handler.c
```

**📝 ANCHOR CHECKLIST** ⏱️ *45 phút* - Đọc và tick ✅ sau khi hiểu:

**ThreadPool (threadpool.c):**
- [ ] `TP_QUEUE_STRUCT` - Bounded queue structure
- [ ] `TP_WORKER_WAIT` - Worker waiting mechanism  
- [ ] `TP_WORKER_EXIT` - Graceful worker exit
- [ ] `TP_EXECUTE_OUTSIDE_LOCK` - Execute job without mutex
- [ ] `TP_SUBMIT_BACKPRESSURE` - Fast-fail when queue full
- [ ] `TP_DESTROY_BROADCAST_JOIN` - Shutdown process

**Network (net.c):**
- [ ] `NET_SOCKET_SETUP` - Socket creation & options
- [ ] `NET_ACCEPT_LOOP` - Main accept loop
- [ ] `NET_FAST_FAIL_BUSY` - Send "server_busy" response
- [ ] `NET_TCP_NODELAY_HINT` - TCP optimization

**Handler (handler.c):**
- [ ] `HANDLER_TIMEOUTS` - Socket timeout settings
- [ ] `HANDLER_BUFFER` - 8KB buffer for DoS protection
- [ ] `HANDLER_FRAMING` - Newline-delimited parsing
- [ ] `HANDLER_WRITE_ALL` - Partial write handling

### **14:30-16:00 (1.5h): Connect Numbers to Code**

**🔍 Code Detective** ⏱️ *90 phút*

**Quest 1: Find "server_busy" source** ⏱️ *20 phút*
```bash
grep -n "server_busy" server/net.c
```
**📝 Answer**: Line ___, trong function ___

**Quest 2: Find backpressure logic** ⏱️ *20 phút*  
```bash
grep -n -A5 -B5 "pool->size >= pool->cap" server/threadpool.c
```
**📝 Answer**: Line ___, return ___

**Quest 3: Find timeout settings** ⏱️ *20 phút*
```bash
grep -n -A3 -B3 "tv.tv_sec" server/handler.c
```
**📝 Answer**: Timeout = ___ seconds

**Quest 4: Connect the dots** ⏱️ *30 phút*
- Test 2 có server_busy cao → code line nào gây ra?
- QUEUE_CAP=1 → dòng code nào check điều kiện này?
- Khi queue full, điều gì xảy ra với request mới?

### **16:00-17:00 (1h): Mini Deep-Dive**

**🎯 CHỌN 1 TOPIC** (pick your favorite):

**Option A: Backpressure Deep-Dive** ⏱️ *60 phút*
```bash
# Đọc chi tiết function threadpool_submit
# Hiểu: bounded queue → Little's Law → latency control
```

**Option B: I/O Handling Deep-Dive** ⏱️ *60 phút*
```bash  
# Đọc function write_all, handle_connection
# Hiểu: partial writes, EINTR, EAGAIN
```

**Option C: Graceful Shutdown Deep-Dive** ⏱️ *60 phút*
```bash
# Đọc threadpool_destroy
# Hiểu: broadcast → join sequence
```

---

# 📅 **NGÀY 2: "CHUYÊN GIA HÓA" (8 giờ)**

## ⏰ **MORNING SESSION (4h)**

### **09:00-10:30 (1.5h): TCP Tuning Experiments**

**Enable TCP_NODELAY** ⏱️ *45 phút*
```bash
# Backup original
cp server/net.c server/net.c.backup

# Enable TCP_NODELAY
sed -i 's|// #define ENABLE_TCP_NODELAY|#define ENABLE_TCP_NODELAY|' server/net.c

# Test before/after
make && THREADS=4 QUEUE_CAP=32 ./scripts/run.sh 2>server.err & echo $! > server.pid
./build/loadgen 50 200 9090
```
**📝 GHI**: `RPS với TCP_NODELAY = ____`, `p95 = ____us`

**SO_REUSEPORT Test** ⏱️ *45 phút*
```bash
# Uncomment SO_REUSEPORT line
sed -i 's|// setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT|setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT|' server/net.c

# Test with 2 processes (if supported)
make && PORT=9090 ./scripts/run.sh & PORT=9090 ./scripts/run.sh &
./build/loadgen 50 200 9090
```

### **10:30-12:00 (1.5h): Chaos Engineering**

**Timeout Testing** ⏱️ *30 phút*
```bash
THREADS=4 ./scripts/run.sh &
# Send incomplete request (no newline)
echo -n '{"pan":"4111111111111111","amount":"1.00"' | nc 127.0.0.1 9090
# Đợi 6 giây, observe connection dropped
```

**Large Payload Testing** ⏱️ *30 phút*
```bash
# Test 8KB+ payload
python3 -c "print('{\"pan\":\"' + '4'*8000 + '\",\"amount\":\"1.00\"}')" | nc 127.0.0.1 9090
```

**Keep-alive vs New Connection** ⏱️ *30 phút*
```bash
./tests/keepalive.sh 9090  # Multiple requests, same connection
# Compare latency: first vs subsequent requests
```

---

## ⏰ **AFTERNOON SESSION (4h)**

### **13:00-14:30 (1.5h): Storytelling với số liệu**

**📝 Viết "Elevator Pitch"** ⏱️ *45 phút*

Template:
> "Tôi đã optimize một payment server từ ____ RPS lên ____ RPS bằng cách:
> 1. Tuning threadpool: THREADS=___, QUEUE_CAP=___
> 2. Enable TCP_NODELAY → giảm p95 từ ___us xuống ___us  
> 3. Implement backpressure để control latency theo Little's Law
> 4. Key insight: bounded queue prevents latency explosion khi overload"

**📊 Performance Report** ⏱️ *45 phút*

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| RPS | ___ | ___ | ___% |
| p95 latency | ___us | ___us | ___% |
| server_busy rate | ___% | ___% | ___% |

### **14:30-16:00 (1.5h): Mock Interview Prep**

**⚡ RAPID FIRE QUESTIONS** - Practice trả lời trong 30 giây:

1. **"Bounded vs unbounded queue khác biệt gì?"**
   *Answer prep: bounded prevents latency explosion...*

2. **"TCP_NODELAY khi nào nên dùng?"**
   *Answer prep: small packets, low latency requirements...*

3. **"Backpressure là gì và tại sao quan trọng?"**
   *Answer prep: flow control, prevent system overload...*

4. **"Graceful shutdown process như thế nào?"**
   *Answer prep: stop accepting → drain queue → join workers...*

5. **"EINTR vs EAGAIN khác biệt?"**
   *Answer prep: interrupted syscall vs would block...*

6. **"Thread-per-connection vs thread pool model?"**
   *Answer prep: resource usage, scalability...*

7. **"Little's Law áp dụng như thế nào?"**
   *Answer prep: Queue Length = Arrival Rate × Wait Time...*

8. **"Partial read/write xử lý ra sao?"**
   *Answer prep: retry loop, handle short operations...*

### **16:00-17:00 (1h): Demo Chuẩn Bị**

**🎬 5-Minute Demo Script** ⏱️ *60 phút*

```bash
#!/bin/bash
echo "=== Payment Server Performance Demo ==="

echo "1. Baseline performance:"
THREADS=4 QUEUE_CAP=32 ./scripts/run.sh &
./build/loadgen 50 200 9090
echo "RPS: ____, p95: ____us"

echo "2. Demonstrating backpressure:"
kill %1 && THREADS=1 QUEUE_CAP=1 ./scripts/run.sh &
for i in {1..10}; do echo '{"pan":"4111111111111111","amount":"1"}' | nc 127.0.0.1 9090 & done
printf 'GET /metrics\r\n' | nc 127.0.0.1 9090
echo "server_busy count: ____"

echo "3. Optimization result:"
# Show your best configuration
kill %1 && THREADS=___ QUEUE_CAP=___ ./scripts/run.sh &
./build/loadgen 50 200 9090
echo "Optimized: RPS=____, p95=____us"
```

---

# 📚 **CHEAT SHEET - In túi áo**

## **🔧 Core Commands**
```bash
# Build & Run
make clean && make
THREADS=4 QUEUE_CAP=32 ./scripts/run.sh &

# Performance Test
./build/loadgen 50 200 9090

# Metrics Check  
printf 'GET /metrics\r\n' | nc 127.0.0.1 9090

# Health Check
printf 'GET /healthz\r\n' | nc 127.0.0.1 9090
```

## **⚡ Key Concepts (5-second recall)**
- **Bounded Queue**: `if (size >= cap) return -1` → "server_busy"
- **Execute Outside Lock**: Minimize mutex contention → parallel execution  
- **Newline Framing**: `memchr(buf, '\n')` → reliable protocol
- **Thread-local DB**: One PG connection per worker thread
- **Little's Law**: Queue Size = Arrival Rate × Service Time

## **🎯 Magic Numbers**
- **Sweet Spot**: THREADS=4, QUEUE_CAP=32 (adjust theo kết quả)
- **Timeout**: 5 seconds for socket read/write
- **Buffer**: 8192 bytes max line length  
- **Listen Backlog**: 128 connections

## **💬 Interview Killer Phrases**
- *"I implemented bounded queue backpressure to prevent latency explosion"*
- *"We handle partial I/O with proper EINTR/EAGAIN retry mechanisms"*  
- *"TCP_NODELAY optimization reduced p95 latency for small responses"*
- *"Thread-local connections eliminate database connection pool contention"*

---

# ✅ **SUCCESS CHECKLIST CUỐI 2 NGÀY**

**Day 1 Completed:**
- [ ] Chạy được 3+ performance tests
- [ ] Tìm được sweet spot configuration  
- [ ] Hiểu được 15+ anchors trong code
- [ ] Connect được performance numbers với code logic

**Day 2 Completed:**
- [ ] Test được TCP optimizations
- [ ] Viết được elevator pitch với số liệu
- [ ] Trả lời được 8/10 interview questions
- [ ] Chuẩn bị được demo script 5 phút
- [ ] Confident giải thích threads/networking concepts

**🏆 Expert Level Achieved:**
- [ ] Có thể optimize server performance based on metrics
- [ ] Có thể debug networking issues từ logs  
- [ ] Có thể design scalable concurrent systems
- [ ] Sẵn sàng interview senior backend roles

---

**🌙 Ngủ ngon! Mai sẽ thành chuyên gia! 🚀**

*P.S: In guide này ra, để bên cạnh laptop. Tick ✅ từng item khi complete!*