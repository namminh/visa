# 🔍 **CODE READING CHUYÊN SÂU - Mini Visa Server**

## 📋 **MỤC ĐÍCH**
Tài liệu này giúp bạn đọc hiểu code như một senior engineer - không chỉ biết "cái gì" mà còn hiểu "tại sao" và "trade-offs".

---

# 🧭 **CHIẾN LƯỢC ĐỌC CODE CHUYÊN SÂU**

## **📖 Nguyên tắc 3-Layer Reading:**
1. **Bird's Eye View** - Architecture & Flow
2. **Microscope View** - Implementation Details  
3. **X-Ray View** - Hidden Assumptions & Edge Cases

---

# 🏗️ **LAYER 1: ARCHITECTURE OVERVIEW**

## 📂 Source Map Nhanh (Server)
- `server/main.c`: Entry – init config/log/db/threadpool/network + reversal worker
- `server/net.c`: TCP accept → submit `handler_job`
- `server/handler.c`: Đọc 1 dòng JSON, validate, risk, 2PC, phản hồi, `/healthz|/readyz|/metrics|/version`
- `server/iso8583.{c,h}`: Parser JSON đơn giản thành `IsoRequest`
- `server/db.{c,h}`: Kết nối PG, insert/idempotent insert, TLS per-thread
- `server/db_participant.{c,h}`: Participant 2PC cho Postgres (BEGIN → PREPARE/COMMIT PREPARED)
- `server/clearing_participant.{c,h}`: Participant 2PC mô phỏng clearing + retry + circuit breaker
- `server/transaction_coordinator.{c,h}`: State machine 2PC, log WAL đơn giản
- `server/threadpool.{c,h}`: Hàng đợi bounded + worker threads
- `server/metrics.{c,h}`: Counters in-memory, phục vụ `/metrics`
- `server/reversal.{c,h}`: Worker gửi reversal/void khi outcome không chắc chắn

## **🎯 System Design Questions:**

### **Q1: Tại sao chọn Thread Pool thay vì Thread-per-Connection?**

**📁 Evidence từ code:**
- `server/threadpool.c:89` - Fixed pool size
- `server/net.c:118` - HandlerContext per connection
- `server/main.c` - Pool created once at startup

**💡 Trade-offs Analysis:**
```
Thread Pool:
✅ Bounded resource usage (predictable memory)
✅ Reduced context switching overhead
✅ Better CPU cache locality
❌ Potential head-of-line blocking
❌ Complex queue management

Thread-per-Connection:
✅ True parallelism per request
✅ Simple programming model
❌ Unbounded resource usage
❌ Context switching overhead
❌ Stack memory per thread (8MB default)
```

**🧠 Senior Insight**: Thread pool là choice tốt cho high-concurrency, short-lived requests. Payment processing thường < 100ms/request.

### Q3: Luồng xử lý 1 request (End-to-end)
1. Net: `accept()` → tạo `HandlerContext` → `threadpool_submit()` (net.c)
2. Handler: đọc 1 dòng JSON, parse `IsoRequest` → Luhn + amount + risk (handler.c)
3. Tạo `txn_id`, init `TransactionCoordinator` (TLS) (handler.c)
4. Tạo `DBParticipantContext` + `ClearingParticipantContext` và `txn_register_participant`
5. DB: `BEGIN` → insert/idempotent (db_participant.c + db.c)
6. Clearing: set transaction (pan_masked, amount, merchant)
7. 2PC: `txn_commit()` → PREPARE all → COMMIT all (transaction_coordinator.c)
8. Thành công: trả `APPROVED`; Thất bại: trả `DECLINED` + enqueue reversal (reversal.c)
9. Metrics/log: cập nhật counters, log JSON 1 dòng

Mẹo debug nhanh: grep theo `txn_id` trong logs/transactions.log và stderr để lần theo phases.

### **Q2: Tại sao dùng Bounded Queue thay vì Unbounded?**

**📁 Code Evidence:**
```c
// server/threadpool.c:44
size_t cap; // bounded queue capacity

// server/threadpool.c:139  
if (pool->size >= pool->cap) {
    return -1; // backpressure!
}
```

**📊 Little's Law Impact:**
```
Unbounded Queue:
- Arrival Rate = 1000 req/s
- Service Time = 100ms
- Queue Length = 1000 * 0.1 = 100 requests
- Wait Time = 100/1000 = 0.1s = 100ms

Bounded Queue (cap=32):
- Max Queue Length = 32
- Max Wait Time = 32ms (best case)
- Excess requests → fast fail (0ms response)
```

**💼 Business Logic**: Payment systems prefer fast rejection over long delays. Customer experience: "Thử lại" tốt hơn "Đợi 30 giây".

---

# 🔬 **LAYER 2: IMPLEMENTATION DEEP DIVE**

## **🧵 ThreadPool Implementation Analysis**

### **Critical Section Analysis:**

**📍 server/threadpool.c:55-85 (worker_main)**

```c
pthread_mutex_lock(&pool->m);
// === CRITICAL SECTION START ===
while (!pool->shutting_down && pool->size == 0) {
    pthread_cond_wait(&pool->cv, &pool->m);  // Atomically unlock → wait → relock
}

if (pool->shutting_down && pool->size == 0) {
    pthread_mutex_unlock(&pool->m);
    break; // Clean exit
}

Job *job = pool->head;  // Dequeue operation
if (job) {
    pool->head = job->next;
    if (!pool->head) pool->tail = NULL;
    pool->size--;
}
// === CRITICAL SECTION END ===
pthread_mutex_unlock(&pool->m);

// Execute OUTSIDE critical section - KEY OPTIMIZATION
if (job) {
    job->fn(job->arg);  
    free(job);
}
```

**🔍 Concurrency Patterns Identified:**

1. **Producer-Consumer with Condition Variables**
   - Producers: `net.c:123` (accept loop)
   - Consumers: `threadpool.c:52` (worker threads)
   - Synchronization: mutex + condvar

2. **Execute Outside Lock Pattern**
   - **Why**: Minimize critical section time
   - **Impact**: Multiple workers can execute jobs in parallel
   - **Trade-off**: More complex state management

3. **Graceful Shutdown Pattern**
   - **Signal**: `shutting_down = 1` + `broadcast()`
   - **Drain**: Workers exit when `size == 0`
   - **Join**: Main thread waits for all workers

### **Memory Management Deep Dive:**

**📍 Job Lifecycle:**
```c
// Allocation: net.c:112
HandlerContext *ctx = malloc(sizeof(*ctx));

// Submission: threadpool.c:129
Job *job = malloc(sizeof(Job));
job->fn = handler_job;
job->arg = ctx;

// Execution: threadpool.c:80
job->fn(job->arg);  // handler_job(ctx)

// Cleanup: threadpool.c:81 + handler.c:281
free(job);          // Job freed by worker
free(ctx);          // Context freed by handler
```

**🚨 Memory Leak Risks:**
- Job allocated nhưng queue full → freed immediately (✅ Safe)
- Context allocated nhưng submit failed → freed by net.c (✅ Safe)
- Shutdown time: remaining jobs freed by destroy (✅ Safe)

---

## **🌐 Network Layer Deep Analysis**

### **📍 Accept Loop Pattern (server/net.c:92-131):**

```c
for (;;) {  // Infinite accept loop
    int fd = accept(listen_fd, ...);
    
    // Per-connection context
    HandlerContext *ctx = malloc(sizeof(*ctx));
    ctx->client_fd = fd;
    ctx->db = dbc;  // Shared DB connection handle
    
    // Async submission with backpressure
    if (threadpool_submit(pool, handler_job, ctx) != 0) {
        // Fast-fail path
        const char *busy = "{\"status\":\"DECLINED\",\"reason\":\"server_busy\"}\n";
        send(fd, busy, strlen(busy), 0);
        close(fd);
        free(ctx);
    }
}
```

**🧠 Design Patterns:**

1. **Accept-and-Dispatch Pattern**
   - Accept thread: Single-threaded, non-blocking
   - Handler threads: Multi-threaded, blocking I/O
   - **Why**: Accept is fast, handlers are slow

2. **Context Passing Pattern**
   - Each connection gets its own context
   - Context carries: socket FD + shared resources
   - **Memory**: O(concurrent_connections)

3. **Circuit Breaker Pattern**
   - When threadpool full → immediate failure
   - **Alternative**: Could queue at TCP level (backlog)
   - **Choice**: Application-level rejection for faster feedback

### **🔧 TCP Socket Options Analysis:**

```c
// server/net.c:67
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

**💡 SO_REUSEADDR Impact:**
- **Problem**: Server restart sau khi crash → "Address already in use"
- **Cause**: Previous sockets trong TIME_WAIT state
- **Solution**: SO_REUSEADDR allows binding to TIME_WAIT addresses
- **Production**: Essential for zero-downtime deployments

```c
// server/net.c:70 (commented)
// setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
```

**💡 SO_REUSEPORT Potential:**
- **Capability**: Multiple processes bind to same port
- **Load Balancing**: Kernel distributes connections
- **Use Case**: Multi-process deployment (vs multi-threading)
- **Trade-off**: Process isolation vs thread sharing

```c
// server/net.c:84
listen(listen_fd, 128);
```

**💡 Listen Backlog = 128:**
- **Purpose**: Queue for pending connections

---

# 🔗 Endpoint Mới (bổ sung)

- GET `/tx?request_id=...` — tra cứu nhanh giao dịch theo `request_id`.
  - Response khi tìm thấy: `{"request_id":"...","amount":"...","status":"..."}`
  - Khi không có: `{"status":"NOT_FOUND"}`
  - Ví dụ nhanh:
    - `printf 'GET /tx?request_id=abc123\r\n' | nc 127.0.0.1 9090`
  - Ghi chú: parsing query trong handler là tối giản (demo). Nếu mở rộng query string, nên tách parser chuẩn.

---

# 🩺 Observability Playbook (thực chiến)

**Health/Ready**
- `printf 'GET /healthz\r\n' | nc 127.0.0.1 9090` → OK khi process sống.
- `printf 'GET /readyz\r\n'  | nc 127.0.0.1 9090` → phụ thuộc DB `CONNECTION_OK`.

**Metrics Snapshot**
- `printf 'GET /metrics\r\n' | nc 127.0.0.1 9090`
- Ý nghĩa chính:
  - `total/approved/declined/server_busy` → throughput, tỉ lệ lỗi, backpressure.
  - `twopc_committed/aborted` → sức khỏe 2PC.
  - `clearing_cb_short_circuit` → circuit breaker có mở không.
  - `reversal_enqueued/succeeded/failed` → tính nhất quán bù (sau lỗi commit).

**Log Tracing (stderr JSON 1 dòng / file `server.err`)**
- Theo `request_id`: `grep '"request_id":"abc123"' server.err`
- Theo `txn_id`: `grep '"txn_id":"visa_abc' -n server.err`
- Lọc lỗi: `grep '"lvl":"ERROR"' server.err`
- Quan sát latency: `grep '"event":"tx"' server.err | awk -F'"latency_us":' '{print $2}' | cut -d',' -f1 | head`

**DB Quick Checks (psql)**
- Đếm nhanh: `SELECT status, COUNT(*) FROM transactions GROUP BY 1 ORDER BY 2 DESC;`
- Tra cứu theo `request_id`: `SELECT request_id, amount, status FROM transactions WHERE request_id = 'abc123';`
- Theo thời gian: `SELECT date_trunc('minute', created_at) AS m, COUNT(*) FROM transactions GROUP BY 1 ORDER BY 1 DESC LIMIT 30;`

**Loadgen sanity**
- `./build/loadgen 50 200 9090` → tăng dần, theo dõi khi `server_busy` bắt đầu tăng.
- Tune nhanh: `THREADS=8 QUEUE_CAP=2048 ./build/server` (tham số qua ENV).

**Lưu ý bảo mật/đầu vào**
- Newline framing + buffer 8K giúp tránh DoS đơn giản; nếu mở rộng payload, cân nhắc dùng JSON lib chuẩn.
- Không log PAN thô: chỉ mask 6+4; kiểm tra lại mọi điểm log để tránh rò rỉ.
- **Tuning**: Balance memory vs burst capacity
- **Calculation**: Should be > max_concurrent_accepts
- **OS Limit**: Limited by `/proc/sys/net/core/somaxconn`

---

## **📡 I/O Handling Deep Analysis**

### **📍 Partial I/O Pattern (server/handler.c:88-103):**

```c
static int write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;      // Interrupted system call
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // Non-blocking would block
            return -1;  // Real error
        }
        if (n == 0) return -1;  // Connection closed
        off += (size_t)n;  // Partial write handled
    }
    return 0;
}
```

**🔍 Error Handling Patterns:**

1. **EINTR Handling**
   - **Cause**: System call interrupted by signal
   - **Response**: Retry the operation
   - **Example**: SIGCHLD, SIGUSR1 during I/O

2. **EAGAIN/EWOULDBLOCK**
   - **Cause**: Non-blocking I/O would block
   - **Response**: Retry (in blocking mode, shouldn't happen)
   - **Design**: Defensive programming

3. **Partial Write Handling**
   - **Reality**: write() may not write all bytes
   - **Reasons**: Socket buffer full, signal interruption
   - **Solution**: Loop until all bytes written

### **📍 Framing Protocol Analysis (server/handler.c:136-150):**

```c
// Process complete lines (newline-delimited framing)
char *start = buf;
for (;;) {
    char *nl = memchr(start, '\n', (buf + used) - start);
    if (!nl) break;  // No complete line yet
    
    *nl = '\0';  // Null-terminate the line
    // Process line...
    start = nl + 1;  // Move to next line
}
```

**🧠 Protocol Design Decisions:**

1. **Why Newline Framing?**
   - **Simple**: Easy to implement and debug
   - **Streaming**: Can process partial buffers
   - **Text-friendly**: Human readable, tool friendly
   - **Alternative**: Length-prefixed (more complex but more efficient)

2. **Buffer Management:**
   - **Size**: 8192 bytes (typical page size)
   - **DoS Protection**: Reject lines > buffer size
   - **Streaming**: Handle partial reads gracefully

3. **State Management:**
   - **Remainder Handling**: Incomplete lines kept for next read
   - **Memory**: Single buffer per connection (not per request)

---

# 🔬 **LAYER 3: X-RAY VIEW - HIDDEN COMPLEXITIES**

## ⚙️ 2PC State Machine – Khía cạnh concurrency (smart upgrade)

File: `server/transaction_coordinator.c`
- Đặt state dưới lock, nhưng THỰC HIỆN I/O ngoài lock để tránh giữ mutex lâu:
  - PREPARE: set `TXN_PREPARING` + log → unlock → gọi `p->prepare()` tuần tự → lock lại để set `TXN_PREPARED`
  - COMMIT: set `TXN_COMMITTING` + log → unlock → gọi `p->commit()` → lock lại finalize
- Abort path: set `TXN_ABORTING` + log → unlock → gọi `p->abort()` → lock lại set `TXN_ABORTED`
- Timeouts (env): `TWOPC_PREPARE_TIMEOUT`, `TWOPC_COMMIT_TIMEOUT` lưu trong transaction, sẵn sàng để enforce.

Điểm đọc nhanh:
- Bắt đầu PREPARE: khoảng dòng 206–216
- Vòng prepare participants: ~217–233
- Đặt `TXN_PREPARED/COMMITTING`: ~235–245
- COMMIT participants và finalize: ~245–270
- ABORT path: ~274–297

## 🌐 Clearing Participant – Retry + Circuit Breaker

File: `server/clearing_participant.c`
- `simulate_clearing_request()`: mô phỏng HTTP; có delay/ngẫu nhiên lỗi.
- Circuit breaker toàn tiến trình:
  - Env: `CLEARING_CB_WINDOW` (mặc định 30s), `CLEARING_CB_FAILS` (5), `CLEARING_CB_OPEN_SECS` (20s)
  - Khi mở: short‑circuit `prepare/commit` (đếm metrics `clearing_cb_short_circuit`)
- Retry: `CLEARING_RETRY_MAX` (mặc định 2) với backoff 100ms, 200ms, 400ms…
- Timeout: `CLEARING_TIMEOUT` (mặc định 30s)
- Abort idempotent: ngay cả khi không thấy hold cục bộ vẫn gửi abort (best‑effort release).

Điểm đọc nhanh:
- Cấu trúc `CircuitBreaker g_cb`: phần đầu file
- Check mở breaker: `cb_should_short_circuit()`
- PREPARE/COMMIT dùng retry + breaker: trong các hàm `clearing_participant_prepare/commit`

## 🗄️ DB Participant – Prepared Transactions

File: `server/db_participant.c`
- `db_participant_begin()` → `BEGIN`
- `db_participant_prepare()` → `PREPARE TRANSACTION 'visa_<txn_id>'`
- `db_participant_commit()` → `COMMIT PREPARED 'visa_<txn_id>'`
- `db_participant_abort()` → `ROLLBACK` nếu chưa prepare, hoặc `ROLLBACK PREPARED` nếu đã prepare.
- Idempotent insert: `db_insert_or_get_by_reqid()` trong `db.c`: ON CONFLICT(request_id) DO NOTHING → SELECT status.

## 🔁 Reversal Worker – Unknown Outcome Handler

Files: `server/reversal.{c,h}`, tích hợp ở `main.c` và `handler.c`
- Khi `txn_commit()` thất bại, handler enqueue reversal: `reversal_enqueue(txn_id, pan_masked, amount, merchant)`
- Worker nền: lấy task theo `next_at`, gửi `clearing_participant_abort()` best‑effort.
- Retry exponential backoff: `REVERSAL_BASE_DELAY_MS` (mặc định 250ms) × 2^attempt, tối đa `REVERSAL_MAX_ATTEMPTS` (mặc định 6).
- Metrics: `reversal_enqueued|reversal_succeeded|reversal_failed`.

Điểm đọc nhanh:
- Enqueue trong `handler.c` ngay sau `commit_failed`
- Vòng worker trong `reversal.c`: hàm `reversal_loop()`

## 📊 Metrics & Endpoints

Handler phục vụ các endpoint đơn giản per-line:
- `GET /healthz` → `OK`
- `GET /readyz` → kiểm tra PG sẵn sàng (`db_is_ready`)
- `GET /version` → từ `version.h`
- `GET /metrics` → JSON counters:
  - Cơ bản: `total, approved, declined, server_busy, risk_declined`
  - 2PC: `twopc_committed, twopc_aborted`
  - Clearing: `clearing_cb_short_circuit`
  - Reversal: `reversal_enqueued, reversal_succeeded, reversal_failed`

Files: `server/handler.c`, `server/metrics.{c,h}`

## 📈 Sequence Diagrams (ASCII)

2PC End‑to‑End
```
Client        Handler           Coordinator         DB Participant        Clearing
  |   JSON       |                    |                    |                   |
  |------------->|                    |                    |                   |
  |              | parse/validate     |                    |                   |
  |              | begin txn_id       |                    |                   |
  |              |------------------->| TXN_PREPARING      |                   |
  |              |                    | prepare(db)        |                   |
  |              |                    |------------------->| BEGIN, PREPARE    |
  |              |                    | prepare(clearing)  |                   |
  |              |                    |------------------------------->| hold   |
  |              |                    | TXN_PREPARED       |                   |
  |              |                    | TXN_COMMITTING     |                   |
  |              |                    | commit(db)         |                   |
  |              |                    |------------------->| COMMIT PREPARED   |
  |              |                    | commit(clearing)   |                   |
  |              |                    |------------------------------->| settle |
  |              |                    | TXN_COMMITTED      |                   |
  |  APPROVED    |                    |                    |                   |
  |<-------------|                    |                    |                   |
```

Reversal Worker (Unknown Outcome)
```
Handler            ReversalQueue       ReversalWorker          Clearing
  | enqueue fail      |                     |                    |
  |------------------>| (task: txn_id,amt) |                    |
  |                   |                     | dequeue/backoff   |
  |                   |                     |------------------>| abort/void
  |                   |                     | <----- OK/ERR ----|
  |                   |                     | retry or done     |
```

## ⚙️ Env Config Tổng Hợp (phần liên quan nâng cấp “smart”)
- 2PC: `TWOPC_PREPARE_TIMEOUT`, `TWOPC_COMMIT_TIMEOUT`
- Clearing: `CLEARING_TIMEOUT`, `CLEARING_RETRY_MAX`, `CLEARING_CB_WINDOW`, `CLEARING_CB_FAILS`, `CLEARING_CB_OPEN_SECS`
- Reversal: `REVERSAL_MAX_ATTEMPTS`, `REVERSAL_BASE_DELAY_MS`
- Core: `DB_URI`, `PORT`, `THREADS`/`NUM_THREADS`, `QUEUE_CAP`

## **🕳️ Edge Cases & Subtle Bugs**

### **Race Condition Analysis:**

**📍 Potential Race in server/threadpool.c:158-163:**

```c
void threadpool_destroy(ThreadPool *pool) {
    pthread_mutex_lock(&pool->m);
    pool->shutting_down = 1;
    pthread_cond_broadcast(&pool->cv);  // Wake all workers
    pthread_mutex_unlock(&pool->m);
    
    // Race window here? Workers might not see shutting_down yet?
    for (int i = 0; i < pool->num_threads; ++i) {
        pthread_join(pool->threads[i], NULL);
    }
}
```

**🔍 Analysis:**
- **Is it safe?** YES - broadcast ensures all workers wake up
- **Memory ordering**: pthread functions provide memory barriers
- **Worker response**: Each worker checks shutting_down under mutex

### **Memory Ordering Subtleties:**

**📍 Context sharing in server/net.c:119:**

```c
ctx->db = dbc;  // Shared DB connection handle
```

**🤔 Thread Safety Questions:**
- Is `dbc` accessed concurrently?
- **Answer**: No - each handler gets thread-local connection via `db_thread_get()`
- **Pattern**: Shared handle, per-thread instances

### **Resource Cleanup Edge Cases:**

**📍 Signal Handling During I/O:**

```c
// What if SIGTERM arrives during handler_job()?
void handler_job(void *arg) {
    HandlerContext *ctx = (HandlerContext *)arg;
    // Long-running operation here...
    // What if process terminated?
}
```

**💡 Current State:**
- ❌ No signal handling
- ❌ No graceful degradation
- ✅ OS will clean up FDs and memory
- **Production Need**: SIGTERM handler for graceful shutdown

## **🏗️ Architectural Assumptions**

### **Assumption 1: Single Request Per Connection**

**📁 Evidence:**
```c
// server/handler.c:281
close(fd);  // Connection closed after each request
```

**🤔 Trade-offs:**
```
Single Request/Connection:
✅ Simple connection management
✅ No connection state to track
✅ Natural resource cleanup
❌ TCP handshake overhead per request
❌ No HTTP keep-alive benefits
❌ Higher latency for request bursts

Keep-Alive Alternative:
✅ Reduced TCP overhead
✅ Better throughput for multiple requests
❌ Complex connection state management
❌ Resource tracking complexity
❌ Timeout management needed
```

### **Assumption 2: Synchronous Database I/O**

**📁 Evidence:**
```c
// server/handler.c: DB operations block the handler thread
db_insert_transaction(...)  // Blocking call
```

**🤔 Scalability Impact:**
```
Blocking I/O:
- Thread blocked during DB query (~1-10ms)
- Max concurrent queries = thread count
- Simple programming model

Async I/O Alternative:
- Single thread, event-driven
- Higher concurrency potential
- Complex state machines
- Callback hell or coroutine complexity
```

### **Assumption 3: In-Memory Metrics**

**📁 Evidence:**
```c
// server/metrics.c: Simple counters in memory
static unsigned long total_count = 0;
static unsigned long approved_count = 0;
```

**🤔 Persistence Trade-offs:**
```
In-Memory Metrics:
✅ Fast increment/read
✅ No I/O overhead
❌ Lost on crash/restart
❌ No historical data
❌ No cross-instance aggregation

Persistent Metrics:
✅ Survive restarts
✅ Historical analysis
✅ Cross-instance views
❌ I/O overhead
❌ Storage complexity
```

---

# 🎯 **CODE REVIEW CHECKLIST CHUYÊN SÂU**

## **🔒 Concurrency Safety:**
- [ ] **Shared State Protection**: Mọi shared variables được protect bởi mutex?
- [ ] **Lock Ordering**: Consistent lock acquisition order để tránh deadlock?
- [ ] **Signal Safety**: Signal handlers chỉ dùng async-signal-safe functions?
- [ ] **Memory Barriers**: Proper synchronization cho cross-thread communication?

## **⚡ Performance Hotspots:**
- [ ] **Critical Section Size**: Mutex held time minimized?
- [ ] **Coordinator I/O Outside Lock**: PREPARE/COMMIT/ABORT gọi ngoài global lock?
- [ ] **Memory Allocation**: Có malloc/free trong hot path không?
- [ ] **System Calls**: Syscall frequency optimized?
- [ ] **Buffer Sizes**: Buffer size phù hợp với workload?

## **🛡️ Error Handling:**
- [ ] **Resource Cleanup**: Mọi malloc có tương ứng free?
- [ ] **Exception Safety**: Cleanup paths được test?
- [ ] **Partial Failures**: System handle được partial operations?
- [ ] **Error Propagation**: Lỗi được propagate đúng cách?
- [ ] **Unknown Outcome**: Có enqueue reversal/advice khi không chắc kết quả?

## **📈 Scalability Limits:**
- [ ] **Resource Bounds**: Fixed limits documented và tunable?
- [ ] **Memory Growth**: Memory usage bounded?
- [ ] **File Descriptors**: FD leaks prevented?
- [ ] **Thread Limits**: Thread pool size configurable?
- [ ] **Breaker Tunables**: Env cho circuit breaker/retry/timeout rõ ràng?

---

# 💡 **SENIOR ENGINEER INSIGHTS**

## **🏆 What Makes This Code "Production Ready"?**

### **✅ Strong Points:**
1. **Bounded Resources**: Thread pool, queue capacity limits
2. **Graceful Degradation**: Backpressure instead of crash
3. **Error Isolation**: Per-connection error handling
4. **Clean Separation**: Network, threading, DB layers separated
5. **Testability**: Components can be unit tested

### **⚠️ Production Gaps:**
1. **Observability**: Metrics are basic, no tracing
2. **Configuration**: Hard-coded constants, no hot reload
3. **Security**: No TLS, authentication, rate limiting
4. **Monitoring**: No health checks beyond basic /healthz
5. **Deployment**: No graceful shutdown signal handling

## **🚀 Scaling Evolution Path:**

### **Phase 1: Current (Single Process)**
```
1 Process → N Threads → DB
Bottleneck: Thread pool size
```

### **Phase 2: Multi-Process**
```
N Processes (SO_REUSEPORT) → DB
Bottleneck: Database connections
```

### **Phase 3: Async I/O**
```  
1 Process → Event Loop → Async DB
Bottleneck: CPU or network bandwidth
```

### **Phase 4: Microservices**
```
Load Balancer → Service Mesh → DB Cluster
Bottleneck: Network latency
```

---

# 🎓 **MASTERY EXERCISES**

## **🔧 Code Modification Challenges:**

### **Challenge 1: Add Connection Pooling**
- Modify để support keep-alive connections
- Handle connection timeout and cleanup
- Maintain connection-to-thread mapping

### **Challenge 2: Implement Graceful Shutdown**
- Add SIGTERM handler
- Stop accepting new connections
- Drain existing requests
- Clean shutdown sequence

### **Challenge 3: Add Request Tracing**
- Generate unique request IDs
- Propagate through all components
- Add structured logging with trace context

### **Challenge 4: Implement Rate Limiting**
- Per-IP rate limiting
- Sliding window or token bucket
- Integration with existing backpressure

---

# 📚 **REFERENCE PATTERNS**

## **🏗️ Architecture Patterns Found:**
- **Thread Pool Pattern**: Fixed worker threads
- **Producer-Consumer**: Accept → Queue → Process
- **Circuit Breaker**: Fast-fail when overloaded  
- **Context Object**: Per-request state passing
- **Execute Around**: Mutex acquire/release pattern

## **🔧 Implementation Patterns:**
- **RAII-style**: Resource cleanup in destructors
- **Error Codes**: Return -1 for failures, 0 for success
- **Defensive Programming**: Check all inputs, handle all errors
- **Fail Fast**: Early validation and rejection

---

**🎯 Kết luận**: Code này demonstrate solid understanding của concurrent system design. Production deployment cần thêm observability, security, và configuration management, nhưng core architecture rất sound.

**📖 Reading Time**: ~2-3 giờ để đọc thoroughly, 30 phút để review nhanh.
1) Không giữ global lock khi làm I/O mạng/DB trong 2PC → throughput cao hơn, giảm contention.
2) Circuit breaker + retry + timeout cho external dependency → tránh “dogpile effect”.
3) Reversal worker cho outcome không chắc chắn → đảm bảo tính nhất quán nghiệp vụ.
4) Idempotency thực tế (request_id) ở DB → chống double-charge.
5) Endpoint health/ready/metrics đơn giản giúp vận hành quan sát nhanh.

Mẹo phỏng vấn: chỉ vào các anchor trong code (`[ANCHOR:...]` ở handler/net/threadpool), giải thích vì sao execute outside lock, và đưa ra chiến lược recovery+observability.
