# 🎯 **2-PHASE COMMIT - HƯỚNG DẪN CHUYÊN GIA**

## 📋 **MỤC ĐÍCH**
Tài liệu chuyên sâu về 2-Phase Commit implementation trong Mini Visa Server - từ lý thuyết đến thực hành production-ready.

---

# 🧠 **PHẦN I: NỀN TẢNG LÝ THUYẾT**

## **🎓 Distributed Transaction Theory**

### **CAP Theorem Context**
```
Consistency + Availability + Partition Tolerance
Pick 2 out of 3

2PC chọn: Consistency + Partition Tolerance
Trade-off: Availability (blocking protocol)
```

### **ACID Properties trong Distributed Systems**

#### **Atomicity (Tính nguyên tử)**
```
Local Transaction:  BEGIN → OPERATIONS → COMMIT/ROLLBACK
Distributed:        PREPARE → VOTE → GLOBAL_DECISION → LOCAL_ACTION

Challenge: Partial failures across network boundaries
Solution: 2PC coordinator tracks all participants
```

#### **Consistency (Tính nhất quán)**
```
Single DB:     Constraints enforced by RDBMS
Distributed:   Cross-system invariants must be maintained

Example Payment Invariant:
  account_balance + pending_transactions = total_available
  
2PC ensures: Either ALL systems update OR NONE update
```

#### **Isolation (Tính cô lập)**
```
Problem: Transaction T1 và T2 chạy concurrent
T1: Transfer $100 from A to B
T2: Check balance of A

Without proper isolation:
T2 might see intermediate state (A debited, B not credited)

2PC Solution: 
- PREPARE phase locks resources
- COMMIT phase releases locks atomically
```

#### **Durability (Tính bền vững)**
```
Challenge: Coordinator crash between PREPARE and COMMIT
Solution: Write-Ahead Logging (WAL)

Transaction Log Entry:
timestamp|txn_id|state|action|participants

Recovery Process:
1. Read log
2. Find incomplete transactions  
3. Query participants for state
4. Complete or abort based on majority vote
```

---

## **🔬 2PC Protocol Deep Dive**

### **State Machine Analysis**

#### **Coordinator State Machine**
```
INIT → PREPARING → PREPARED → COMMITTING → COMMITTED
  ↓       ↓         ↓           ↓
ABORT ← ABORT ← ABORT ← ABORT (on any failure)

State Transitions:
- INIT → PREPARING: Send PREPARE to all participants
- PREPARING → PREPARED: All participants voted YES
- PREPARING → ABORT: Any participant voted NO
- PREPARED → COMMITTING: Decision made to commit
- COMMITTING → COMMITTED: All participants confirmed commit
```

#### **Participant State Machine**
```
INIT → PREPARED → COMMITTED
  ↓       ↓
ABORT ← ABORT

State Transitions:
- INIT → PREPARED: Local validation successful, resources locked
- INIT → ABORT: Local validation failed
- PREPARED → COMMITTED: Received COMMIT from coordinator
- PREPARED → ABORT: Received ABORT from coordinator
```

### **Message Flow Diagram**
```
Coordinator         Participant1         Participant2
     |                    |                    |
     |---- PREPARE ------>|                    |
     |---- PREPARE ------------------>|        |
     |                    |                    |
     |<---- YES -----------|                    |
     |<---- YES -----------------------|        |
     |                    |                    |
     |---- COMMIT ------->|                    |
     |---- COMMIT ------------------->|        |
     |                    |                    |
     |<---- ACK -----------|                    |
     |<---- ACK -----------------------|        |
     |                    |                    |
```

### **Failure Scenarios Matrix**

| Failure Point | Coordinator Action | Participant Action | Recovery Strategy |
|---------------|-------------------|-------------------|-------------------|
| Before PREPARE | Abort immediately | N/A | Simple rollback |
| During PREPARE | Timeout → Abort | Release locks | Query other participants |
| After PREPARE, Before COMMIT | Read log → Complete | Wait for decision | Coordinator recovery |
| During COMMIT | Retry indefinitely | Apply commit | Transaction log replay |
| After COMMIT | Clean up | N/A | Garbage collection |

---

# 🏗️ **PHẦN II: IMPLEMENTATION ARCHITECTURE**

## **📐 System Design Decisions**

### **Threading Model Analysis**

#### **Option 1: Single-Threaded Event Loop**
```c
// Pros: No concurrency issues, simple state management
// Cons: Blocking on I/O, poor CPU utilization

while (true) {
    event = wait_for_event();
    switch (event.type) {
        case NEW_TRANSACTION:
            process_transaction(event.data);
            break;
        case PARTICIPANT_RESPONSE:
            handle_response(event.data);
            break;
    }
}
```

#### **Option 2: Thread-per-Transaction**
```c
// Pros: True parallelism, simple programming model
// Cons: Resource overhead, context switching

void* transaction_thread(void* arg) {
    Transaction* txn = (Transaction*)arg;
    
    // Phase 1: PREPARE
    for (each participant) {
        send_prepare(participant, txn->id);
        wait_for_response(participant, TIMEOUT);
    }
    
    // Phase 2: COMMIT/ABORT
    if (all_prepared) {
        for (each participant) {
            send_commit(participant, txn->id);
        }
    }
}
```

#### **Option 3: Thread Pool + Work Queue (Chosen)**
```c
// Pros: Bounded resources, good throughput, manageable complexity
// Cons: More complex state management

struct TransactionCoordinator {
    ThreadPool* pool;
    WorkQueue* pending_transactions;
    HashMap* active_transactions;
    TransactionLog* log;
};

// Worker threads process from queue
void* worker_thread(void* arg) {
    while (!shutdown) {
        Work* work = queue_dequeue(work_queue);
        process_work(work);
    }
}
```

### **Memory Management Strategy**

#### **Transaction Lifecycle Memory Pattern**
```c
// Creation (Heap allocation)
Transaction* txn = malloc(sizeof(Transaction));
txn->participants = malloc(sizeof(Participant) * MAX_PARTICIPANTS);

// Registration (Stack references)
int register_participant(Transaction* txn, Participant* p) {
    // Store pointer, not copy - avoid memory duplication
    txn->participants[txn->count++] = p;
}

// Execution (Reference semantics)
int execute_2pc(Transaction* txn) {
    // Work with existing pointers, minimal allocation
    for (int i = 0; i < txn->participant_count; i++) {
        Participant* p = &txn->participants[i];
        p->prepare(p->context, txn->id);
    }
}

// Cleanup (Deterministic deallocation)
void transaction_destroy(Transaction* txn) {
    // Note: Don't free participant contexts - they're owned by caller
    free(txn->participants);
    free(txn);
}
```

#### **Lock-Free Data Structures for High Performance**
```c
// Producer-Consumer queue for coordinator work
typedef struct {
    volatile long head;
    volatile long tail;
    Work* items[QUEUE_SIZE];
} LockFreeQueue;

// Atomic operations for thread safety
bool queue_enqueue(LockFreeQueue* q, Work* item) {
    long tail = atomic_load(&q->tail);
    long next_tail = (tail + 1) % QUEUE_SIZE;
    
    if (next_tail == atomic_load(&q->head)) {
        return false; // Queue full
    }
    
    q->items[tail] = item;
    atomic_store(&q->tail, next_tail);
    return true;
}
```

---

## **🔧 Component Deep Dive**

### **Transaction Coordinator Implementation**

#### **Core Data Structures**
```c
// server/transaction_coordinator.h
typedef enum {
    TXN_INIT,         // Transaction created
    TXN_PREPARING,    // PREPARE messages sent
    TXN_PREPARED,     // All participants voted YES
    TXN_COMMITTING,   // COMMIT messages sent
    TXN_COMMITTED,    // All participants confirmed
    TXN_ABORTING,     // ABORT messages sent
    TXN_ABORTED       // All participants aborted
} TransactionState;

typedef struct {
    char transaction_id[MAX_TRANSACTION_ID_LEN];
    TransactionState state;
    
    // Participant management
    size_t participant_count;
    Participant participants[MAX_PARTICIPANTS];
    
    // Timeout management
    time_t start_time;
    time_t prepare_timeout;
    time_t commit_timeout;
    
    // Concurrency control
    pthread_mutex_t state_mutex;
    pthread_cond_t state_changed;
} Transaction;

struct TransactionCoordinator {
    // Active transaction tracking
    pthread_mutex_t mutex;
    Transaction* active_transactions[MAX_ACTIVE_TRANSACTIONS];
    size_t active_count;
    
    // Persistence for recovery
    FILE* txn_log;
    
    // Performance monitoring
    struct {
        unsigned long total_transactions;
        unsigned long committed_transactions;
        unsigned long aborted_transactions;
        double avg_commit_time_ms;
    } metrics;
};
```

#### **Critical Section Analysis**
```c
// server/transaction_coordinator.c:204-298
int txn_commit(TransactionCoordinator* coordinator, Transaction* txn) {
    // CRITICAL SECTION 1: State transition protection
    pthread_mutex_lock(&coordinator->mutex);
    
    // Validate transaction state
    if (txn->state != TXN_INIT) {
        pthread_mutex_unlock(&coordinator->mutex);
        return -1; // Invalid state transition
    }
    
    // State transition: INIT → PREPARING
    txn->state = TXN_PREPARING;
    log_transaction_state(coordinator, txn, "PREPARE_START");
    
    pthread_mutex_unlock(&coordinator->mutex);
    // END CRITICAL SECTION 1
    
    // PHASE 1: PREPARE (Outside critical section for performance)
    bool all_prepared = true;
    for (size_t i = 0; i < txn->participant_count; i++) {
        Participant* p = &txn->participants[i];
        
        // Network I/O - can be slow, don't hold global lock
        int result = p->prepare(p->context, txn->transaction_id);
        
        if (result == 0) {
            p->state = PARTICIPANT_PREPARED;
        } else {
            p->state = PARTICIPANT_FAILED;
            all_prepared = false;
            break; // Fail-fast optimization
        }
    }
    
    // CRITICAL SECTION 2: Decision point
    pthread_mutex_lock(&coordinator->mutex);
    
    if (all_prepared) {
        txn->state = TXN_PREPARED;
        log_transaction_state(coordinator, txn, "PREPARED");
        
        // Decision: COMMIT
        txn->state = TXN_COMMITTING;
        log_transaction_state(coordinator, txn, "COMMIT_START");
        
        pthread_mutex_unlock(&coordinator->mutex);
        // END CRITICAL SECTION 2
        
        // PHASE 2: COMMIT (Outside critical section)
        bool commit_success = true;
        for (size_t i = 0; i < txn->participant_count; i++) {
            Participant* p = &txn->participants[i];
            
            if (p->state == PARTICIPANT_PREPARED) {
                int result = p->commit(p->context, txn->transaction_id);
                if (result == 0) {
                    p->state = PARTICIPANT_COMMITTED;
                } else {
                    p->state = PARTICIPANT_FAILED;
                    commit_success = false;
                    // Note: This is a serious problem in 2PC!
                    // Some participants may have committed, others failed
                    // Requires manual intervention or sophisticated recovery
                }
            }
        }
        
        // CRITICAL SECTION 3: Final state update
        pthread_mutex_lock(&coordinator->mutex);
        
        if (commit_success) {
            txn->state = TXN_COMMITTED;
            log_transaction_state(coordinator, txn, "COMMITTED");
            
            // Transaction successful - remove from active list
            remove_transaction(coordinator, txn->transaction_id);
            coordinator->metrics.committed_transactions++;
            
            pthread_mutex_unlock(&coordinator->mutex);
            return 0; // Success
        }
        // Fall through to abort case
    }
    // Decision: ABORT
    txn->state = TXN_ABORTING;
    log_transaction_state(coordinator, txn, "ABORT_START");
    
    pthread_mutex_unlock(&coordinator->mutex);
    // END CRITICAL SECTION 3
    
    // ABORT phase (Outside critical section)
    for (size_t i = 0; i < txn->participant_count; i++) {
        Participant* p = &txn->participants[i];
        p->abort(p->context, txn->transaction_id);
        p->state = PARTICIPANT_ABORTED;
    }
    
    // CRITICAL SECTION 4: Cleanup
    pthread_mutex_lock(&coordinator->mutex);
    txn->state = TXN_ABORTED;
    log_transaction_state(coordinator, txn, "ABORTED");
    
    remove_transaction(coordinator, txn->transaction_id);
    coordinator->metrics.aborted_transactions++;
    
    pthread_mutex_unlock(&coordinator->mutex);
    return -1; // Aborted
}
```

### **Participant Interface Design**

#### **Database Participant Implementation**
```c
// server/db_participant.c
typedef struct {
    DBConnection* dbc;
    char current_txn_id[MAX_TRANSACTION_ID_LEN];
    bool in_transaction;
    
    // PostgreSQL specific state
    char prepared_txn_name[64]; // For PREPARE TRANSACTION 'name'
    
    // Performance tracking
    struct timeval prepare_start;
    struct timeval commit_start;
} DBParticipantContext;

// Phase 1: PREPARE implementation
int db_participant_prepare(void* context, const char* txn_id) {
    DBParticipantContext* ctx = (DBParticipantContext*)context;
    
    // Validation
    if (!ctx || !txn_id) return -1;
    if (!ctx->in_transaction) {
        log_message_json("ERROR", "db_participant", txn_id, 
                        "Not in transaction", -1);
        return -1;
    }
    
    gettimeofday(&ctx->prepare_start, NULL);
    
    // PostgreSQL PREPARE TRANSACTION
    // This makes the transaction durable and recoverable
    snprintf(ctx->prepared_txn_name, sizeof(ctx->prepared_txn_name), 
             "visa_%s", txn_id);
    
    char prepare_cmd[256];
    snprintf(prepare_cmd, sizeof(prepare_cmd), 
             "PREPARE TRANSACTION '%s'", ctx->prepared_txn_name);
    
    PGconn* conn = (PGconn*)ctx->dbc;
    PGresult* res = PQexec(conn, prepare_cmd);
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char* error = PQerrorMessage(conn);
        log_message_json("ERROR", "db_participant", txn_id, 
                        "PREPARE failed", -1);
        PQclear(res);
        return -1;
    }
    PQclear(res);
    
    // Transaction is now "prepared" - it can survive process crashes
    // but is not yet committed
    ctx->in_transaction = false; // No longer active, but prepared
    
    struct timeval prepare_end;
    gettimeofday(&prepare_end, NULL);
    long prepare_time_us = (prepare_end.tv_sec - ctx->prepare_start.tv_sec) * 1000000L +
                          (prepare_end.tv_usec - ctx->prepare_start.tv_usec);
    
    log_message_json("INFO", "db_participant", txn_id, 
                    "PREPARE successful", prepare_time_us);
    return 0;
}

// Phase 2: COMMIT implementation
int db_participant_commit(void* context, const char* txn_id) {
    DBParticipantContext* ctx = (DBParticipantContext*)context;
    
    if (!ctx || !txn_id) return -1;
    
    gettimeofday(&ctx->commit_start, NULL);
    
    // Commit the prepared transaction
    char commit_cmd[256];
    snprintf(commit_cmd, sizeof(commit_cmd), 
             "COMMIT PREPARED '%s'", ctx->prepared_txn_name);
    
    PGconn* conn = (PGconn*)ctx->dbc;
    PGresult* res = PQexec(conn, commit_cmd);
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char* error = PQerrorMessage(conn);
        log_message_json("ERROR", "db_participant", txn_id, 
                        "COMMIT PREPARED failed", -1);
        PQclear(res);
        
        // This is a serious problem - the transaction was prepared
        // but now cannot be committed. Manual intervention required.
        return -1;
    }
    PQclear(res);
    
    // Clear transaction state
    memset(ctx->current_txn_id, 0, sizeof(ctx->current_txn_id));
    memset(ctx->prepared_txn_name, 0, sizeof(ctx->prepared_txn_name));
    
    struct timeval commit_end;
    gettimeofday(&commit_end, NULL);
    long commit_time_us = (commit_end.tv_sec - ctx->commit_start.tv_sec) * 1000000L +
                         (commit_end.tv_usec - ctx->commit_start.tv_usec);
    
    log_message_json("INFO", "db_participant", txn_id, 
                    "COMMIT successful", commit_time_us);
    return 0;
}

// ABORT implementation
int db_participant_abort(void* context, const char* txn_id) {
    DBParticipantContext* ctx = (DBParticipantContext*)context;
    
    if (!ctx || !txn_id) return -1;
    
    PGconn* conn = (PGconn*)ctx->dbc;
    PGresult* res;
    
    if (ctx->in_transaction) {
        // Transaction not yet prepared - use normal ROLLBACK
        res = PQexec(conn, "ROLLBACK");
        ctx->in_transaction = false;
    } else if (strlen(ctx->prepared_txn_name) > 0) {
        // Transaction was prepared - use ROLLBACK PREPARED
        char rollback_cmd[256];
        snprintf(rollback_cmd, sizeof(rollback_cmd), 
                 "ROLLBACK PREPARED '%s'", ctx->prepared_txn_name);
        res = PQexec(conn, rollback_cmd);
    } else {
        // Nothing to abort
        log_message_json("INFO", "db_participant", txn_id, 
                        "No transaction to abort", -1);
        return 0;
    }
    
    // Note: ROLLBACK failures are often non-critical
    // The transaction may have already been rolled back
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        log_message_json("WARN", "db_participant", txn_id, 
                        "ROLLBACK failed (may be OK)", -1);
    } else {
        log_message_json("INFO", "db_participant", txn_id, 
                        "ROLLBACK successful", -1);
    }
    PQclear(res);
    
    // Always clear state for idempotency
    memset(ctx->current_txn_id, 0, sizeof(ctx->current_txn_id));
    memset(ctx->prepared_txn_name, 0, sizeof(ctx->prepared_txn_name));
    
    return 0; // Always return success for abort
}
```

#### **Clearing Participant Implementation**
```c
// server/clearing_participant.c
typedef struct {
    char service_url[256];          // External service endpoint
    int timeout_seconds;            // Network timeout
    
    // Transaction state
    char current_txn_id[MAX_TRANSACTION_ID_LEN];
    bool has_hold;                  // Authorization hold placed?
    
    // Business data
    char pan_masked[32];            // Masked PAN for logging
    char amount[16];                // Transaction amount
    char merchant_id[32];           // Merchant identifier
    
    // Network resilience
    int retry_count;
    int max_retries;
    
    // Performance tracking
    struct {
        long total_requests;
        long successful_requests;
        long failed_requests;
        double avg_response_time_ms;
    } metrics;
} ClearingParticipantContext;

// Network simulation with realistic behavior
static int simulate_clearing_request(const char* url, 
                                   const char* method,
                                   const char* payload,
                                   char* response,
                                   size_t response_size,
                                   int timeout_seconds) {
    
    // Simulate network latency (50-150ms)
    usleep(50000 + (rand() % 100000));
    
    // Simulate network failures (5% failure rate)
    if (rand() % 100 < 5) {
        snprintf(response, response_size, 
                "{\"status\":\"ERROR\",\"reason\":\"network_timeout\"}");
        return -1;
    }
    
    // Simulate service-specific responses
    if (strcmp(method, "POST") == 0) {
        if (strstr(payload, "prepare")) {
            // Authorization request
            snprintf(response, response_size, 
                    "{\"status\":\"OK\",\"action\":\"hold_placed\","
                    "\"auth_code\":\"AUTH%06d\",\"hold_amount\":\"%s\"}", 
                    rand() % 999999, 
                    extract_amount_from_payload(payload));
        } else if (strstr(payload, "commit")) {
            // Settlement request
            snprintf(response, response_size, 
                    "{\"status\":\"OK\",\"action\":\"settled\","
                    "\"settlement_id\":\"STL%06d\",\"settlement_date\":\"%s\"}", 
                    rand() % 999999,
                    current_date_iso8601());
        } else if (strstr(payload, "abort")) {
            // Hold release request
            snprintf(response, response_size, 
                    "{\"status\":\"OK\",\"action\":\"hold_released\"}");
        }
    }
    
    return 0;
}

// Phase 1: PREPARE - Place authorization hold
int clearing_participant_prepare(void* context, const char* txn_id) {
    ClearingParticipantContext* ctx = (ClearingParticipantContext*)context;
    
    if (!ctx || !txn_id) return -1;
    
    // Validate state
    if (strcmp(ctx->current_txn_id, txn_id) != 0) {
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "Transaction ID mismatch", -1);
        return -1;
    }
    
    // Prepare authorization request
    char payload[512];
    snprintf(payload, sizeof(payload),
            "{"
            "\"action\":\"prepare\","
            "\"transaction_id\":\"%s\","
            "\"pan\":\"%s\","
            "\"amount\":\"%s\","
            "\"merchant_id\":\"%s\","
            "\"timestamp\":\"%ld\""
            "}",
            txn_id, ctx->pan_masked, ctx->amount, ctx->merchant_id,
            time(NULL));
    
    char response[256];
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // Network request with retry logic
    int result = -1;
    for (int retry = 0; retry <= ctx->max_retries; retry++) {
        result = simulate_clearing_request(ctx->service_url, "POST", payload, 
                                         response, sizeof(response), 
                                         ctx->timeout_seconds);
        
        if (result == 0) break;
        
        if (retry < ctx->max_retries) {
            // Exponential backoff
            int delay_ms = 100 * (1 << retry); // 100ms, 200ms, 400ms, ...
            usleep(delay_ms * 1000);
            log_message_json("WARN", "clearing_participant", txn_id, 
                            "Retrying request", retry + 1);
        }
    }
    
    gettimeofday(&end, NULL);
    long response_time_us = (end.tv_sec - start.tv_sec) * 1000000L +
                           (end.tv_usec - start.tv_usec);
    
    ctx->metrics.total_requests++;
    
    if (result != 0) {
        ctx->metrics.failed_requests++;
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "Clearing PREPARE failed after retries", response_time_us);
        return -1;
    }
    
    // Parse response
    if (strstr(response, "\"status\":\"OK\"")) {
        ctx->has_hold = true;
        ctx->metrics.successful_requests++;
        
        // Update average response time
        ctx->metrics.avg_response_time_ms = 
            (ctx->metrics.avg_response_time_ms * (ctx->metrics.total_requests - 1) +
             response_time_us / 1000.0) / ctx->metrics.total_requests;
        
        log_message_json("INFO", "clearing_participant", txn_id, 
                        "Authorization hold placed", response_time_us);
        return 0;
    } else {
        ctx->metrics.failed_requests++;
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "Clearing service declined", response_time_us);
        return -1;
    }
}
```

---

# 🎯 **PHẦN III: ADVANCED TOPICS**

## **⚡ Performance Optimization**

### **Lock Contention Analysis**

#### **Problem: Coordinator Mutex Bottleneck**
```c
// BAD: Holding global lock too long
pthread_mutex_lock(&coordinator->mutex);
for (each participant) {
    participant->prepare(); // Network I/O under lock!
}
pthread_mutex_unlock(&coordinator->mutex);
```

#### **Solution: Fine-grained Locking**
```c
// GOOD: Minimal critical sections
pthread_mutex_lock(&coordinator->mutex);
Transaction* txn = find_and_mark_transaction(coordinator, txn_id);
pthread_mutex_unlock(&coordinator->mutex);

// Network I/O outside critical section
for (each participant) {
    participant->prepare();
}

pthread_mutex_lock(&coordinator->mutex);
update_transaction_state(coordinator, txn, results);
pthread_mutex_unlock(&coordinator->mutex);
```

#### **Lock-Free Transaction Lookup**
```c
// Using atomic operations for high-performance lookup
typedef struct {
    volatile Transaction* transactions[MAX_TRANSACTIONS];
    volatile long count;
} LockFreeTransactionRegistry;

Transaction* lockfree_find_transaction(LockFreeTransactionRegistry* registry, 
                                     const char* txn_id) {
    long current_count = atomic_load(&registry->count);
    
    for (long i = 0; i < current_count; i++) {
        Transaction* txn = (Transaction*)atomic_load(&registry->transactions[i]);
        if (txn && strcmp(txn->transaction_id, txn_id) == 0) {
            return txn;
        }
    }
    return NULL;
}
```

### **Memory Pool Allocation**
```c
// Pre-allocated memory pools for high-frequency objects
typedef struct {
    Transaction* pool[POOL_SIZE];
    volatile long head;
    volatile long tail;
    pthread_mutex_t mutex;
} TransactionPool;

TransactionPool* transaction_pool_init(void) {
    TransactionPool* pool = malloc(sizeof(TransactionPool));
    
    // Pre-allocate all transactions
    for (int i = 0; i < POOL_SIZE; i++) {
        pool->pool[i] = malloc(sizeof(Transaction));
        memset(pool->pool[i], 0, sizeof(Transaction));
    }
    
    pool->head = 0;
    pool->tail = POOL_SIZE;
    pthread_mutex_init(&pool->mutex, NULL);
    
    return pool;
}

Transaction* transaction_pool_acquire(TransactionPool* pool) {
    pthread_mutex_lock(&pool->mutex);
    
    if (pool->head == pool->tail) {
        pthread_mutex_unlock(&pool->mutex);
        return NULL; // Pool exhausted
    }
    
    Transaction* txn = pool->pool[pool->head];
    pool->head = (pool->head + 1) % POOL_SIZE;
    
    pthread_mutex_unlock(&pool->mutex);
    
    // Reset transaction state
    memset(txn, 0, sizeof(Transaction));
    txn->state = TXN_INIT;
    
    return txn;
}

void transaction_pool_release(TransactionPool* pool, Transaction* txn) {
    pthread_mutex_lock(&pool->mutex);
    
    pool->pool[pool->tail] = txn;
    pool->tail = (pool->tail + 1) % POOL_SIZE;
    
    pthread_mutex_unlock(&pool->mutex);
}
```

### **Batch Processing Optimization**
```c
// Process multiple transactions in batches for better throughput
typedef struct {
    Transaction* transactions[BATCH_SIZE];
    size_t count;
    time_t batch_start_time;
} TransactionBatch;

int process_transaction_batch(TransactionCoordinator* coordinator, 
                            TransactionBatch* batch) {
    
    // Phase 1: Send all PREPARE messages concurrently
    for (size_t i = 0; i < batch->count; i++) {
        Transaction* txn = batch->transactions[i];
        
        for (size_t j = 0; j < txn->participant_count; j++) {
            Participant* p = &txn->participants[j];
            
            // Async prepare - don't wait for response yet
            async_send_prepare(p, txn->transaction_id);
        }
    }
    
    // Collect all responses
    for (size_t i = 0; i < batch->count; i++) {
        Transaction* txn = batch->transactions[i];
        
        bool all_prepared = true;
        for (size_t j = 0; j < txn->participant_count; j++) {
            Participant* p = &txn->participants[j];
            
            int result = wait_for_prepare_response(p, txn->transaction_id);
            if (result != 0) {
                all_prepared = false;
                break;
            }
        }
        
        if (all_prepared) {
            txn->state = TXN_PREPARED;
        } else {
            txn->state = TXN_ABORTING;
        }
    }
    
    // Phase 2: Send COMMIT/ABORT messages
    for (size_t i = 0; i < batch->count; i++) {
        Transaction* txn = batch->transactions[i];
        
        if (txn->state == TXN_PREPARED) {
            // Send COMMIT
            for (size_t j = 0; j < txn->participant_count; j++) {
                Participant* p = &txn->participants[j];
                async_send_commit(p, txn->transaction_id);
            }
        } else {
            // Send ABORT
            for (size_t j = 0; j < txn->participant_count; j++) {
                Participant* p = &txn->participants[j];
                async_send_abort(p, txn->transaction_id);
            }
        }
    }
    
    return 0;
}
```

---

## **🛡️ Fault Tolerance & Recovery**

### **Transaction Log Design**

#### **Write-Ahead Logging (WAL)**
```c
// High-performance transaction log with WAL properties
typedef struct {
    FILE* log_file;
    char* write_buffer;
    size_t buffer_size;
    size_t buffer_used;
    pthread_mutex_t buffer_mutex;
    
    // Background writer thread
    pthread_t writer_thread;
    pthread_cond_t write_cond;
    volatile bool shutdown;
} TransactionLog;

// Log entry format (binary for performance)
typedef struct __attribute__((packed)) {
    uint64_t timestamp;         // 8 bytes
    uint32_t txn_id_len;       // 4 bytes
    char txn_id[MAX_TXN_ID];   // Variable length
    uint8_t state;             // 1 byte
    uint8_t action;            // 1 byte
    uint16_t participant_count; // 2 bytes
    // Participant data follows...
} LogEntry;

int transaction_log_write(TransactionLog* log, 
                         const Transaction* txn, 
                         const char* action) {
    
    // Prepare log entry
    LogEntry entry;
    entry.timestamp = current_time_micros();
    entry.txn_id_len = strlen(txn->transaction_id);
    strncpy(entry.txn_id, txn->transaction_id, MAX_TXN_ID);
    entry.state = txn->state;
    entry.participant_count = txn->participant_count;
    
    // Calculate total entry size
    size_t entry_size = sizeof(LogEntry) + 
                       entry.participant_count * sizeof(ParticipantLogData);
    
    pthread_mutex_lock(&log->buffer_mutex);
    
    // Check if buffer has space
    if (log->buffer_used + entry_size > log->buffer_size) {
        // Trigger immediate flush
        pthread_cond_signal(&log->write_cond);
        
        // Wait for buffer space
        while (log->buffer_used + entry_size > log->buffer_size) {
            pthread_cond_wait(&log->write_cond, &log->buffer_mutex);
        }
    }
    
    // Copy entry to buffer
    memcpy(log->write_buffer + log->buffer_used, &entry, sizeof(LogEntry));
    log->buffer_used += sizeof(LogEntry);
    
    // Copy participant data
    for (size_t i = 0; i < txn->participant_count; i++) {
        ParticipantLogData pdata;
        strncpy(pdata.name, txn->participants[i].name, MAX_PARTICIPANT_NAME);
        pdata.state = txn->participants[i].state;
        
        memcpy(log->write_buffer + log->buffer_used, &pdata, sizeof(pdata));
        log->buffer_used += sizeof(pdata);
    }
    
    pthread_mutex_unlock(&log->buffer_mutex);
    
    // Signal writer thread
    pthread_cond_signal(&log->write_cond);
    
    return 0;
}

// Background writer thread for async I/O
void* transaction_log_writer(void* arg) {
    TransactionLog* log = (TransactionLog*)arg;
    
    while (!log->shutdown) {
        pthread_mutex_lock(&log->buffer_mutex);
        
        // Wait for data to write
        while (log->buffer_used == 0 && !log->shutdown) {
            pthread_cond_wait(&log->write_cond, &log->buffer_mutex);
        }
        
        if (log->buffer_used > 0) {
            // Write buffer to disk
            size_t written = fwrite(log->write_buffer, 1, log->buffer_used, log->log_file);
            if (written != log->buffer_used) {
                // Handle write error
                log_error("Failed to write transaction log");
            }
            
            // Force write to disk (durability requirement)
            fflush(log->log_file);
            fsync(fileno(log->log_file));
            
            // Reset buffer
            log->buffer_used = 0;
            
            // Signal waiting threads
            pthread_cond_broadcast(&log->write_cond);
        }
        
        pthread_mutex_unlock(&log->buffer_mutex);
    }
    
    return NULL;
}
```

#### **Recovery Algorithm Implementation**
```c
typedef struct {
    char txn_id[MAX_TXN_ID];
    TransactionState last_state;
    time_t last_update;
    Participant participants[MAX_PARTICIPANTS];
    size_t participant_count;
} RecoveryTransaction;

int transaction_coordinator_recover(TransactionCoordinator* coordinator) {
    FILE* log_file = fopen("logs/transactions.log", "rb");
    if (!log_file) {
        log_message_json("INFO", "recovery", NULL, "No log file found", -1);
        return 0;
    }
    
    // Parse log file to build recovery state
    HashMap* recovery_txns = hashmap_create();
    LogEntry entry;
    
    while (fread(&entry, sizeof(LogEntry), 1, log_file) == 1) {
        RecoveryTransaction* rtxn = hashmap_get(recovery_txns, entry.txn_id);
        if (!rtxn) {
            rtxn = malloc(sizeof(RecoveryTransaction));
            memset(rtxn, 0, sizeof(RecoveryTransaction));
            strncpy(rtxn->txn_id, entry.txn_id, MAX_TXN_ID);
            hashmap_put(recovery_txns, entry.txn_id, rtxn);
        }
        
        // Update transaction state
        rtxn->last_state = entry.state;
        rtxn->last_update = entry.timestamp;
        rtxn->participant_count = entry.participant_count;
        
        // Read participant data
        for (size_t i = 0; i < entry.participant_count; i++) {
            ParticipantLogData pdata;
            fread(&pdata, sizeof(pdata), 1, log_file);
            
            strncpy(rtxn->participants[i].name, pdata.name, MAX_PARTICIPANT_NAME);
            rtxn->participants[i].state = pdata.state;
        }
    }
    
    fclose(log_file);
    
    // Process incomplete transactions
    int recovered_count = 0;
    HashMap* iter = hashmap_iterator(recovery_txns);
    
    while (hashmap_next(iter)) {
        RecoveryTransaction* rtxn = hashmap_value(iter);
        
        switch (rtxn->last_state) {
            case TXN_PREPARED:
                // All participants prepared, but commit not completed
                // Query participants to determine final state
                if (query_participants_and_commit(coordinator, rtxn) == 0) {
                    log_message_json("INFO", "recovery", rtxn->txn_id, 
                                    "Recovered and committed", -1);
                } else {
                    log_message_json("WARN", "recovery", rtxn->txn_id, 
                                    "Recovered and aborted", -1);
                }
                recovered_count++;
                break;
                
            case TXN_COMMITTING:
                // Commit was in progress - complete it
                complete_commit_recovery(coordinator, rtxn);
                log_message_json("INFO", "recovery", rtxn->txn_id, 
                                "Completed interrupted commit", -1);
                recovered_count++;
                break;
                
            case TXN_COMMITTED:
            case TXN_ABORTED:
                // Transaction completed - nothing to do
                break;
                
            default:
                // Incomplete transaction - abort it
                abort_incomplete_transaction(coordinator, rtxn);
                log_message_json("INFO", "recovery", rtxn->txn_id, 
                                "Aborted incomplete transaction", -1);
                recovered_count++;
                break;
        }
        
        free(rtxn);
    }
    
    hashmap_destroy(recovery_txns);
    
    log_message_json("INFO", "recovery", NULL, "Recovery completed", recovered_count);
    return recovered_count;
}

// Query participants to determine transaction fate
int query_participants_and_commit(TransactionCoordinator* coordinator, 
                                RecoveryTransaction* rtxn) {
    
    int prepared_count = 0;
    int committed_count = 0;
    int aborted_count = 0;
    
    // Query each participant for transaction state
    for (size_t i = 0; i < rtxn->participant_count; i++) {
        ParticipantState state = query_participant_state(
            rtxn->participants[i].name, rtxn->txn_id);
        
        switch (state) {
            case PARTICIPANT_PREPARED:
                prepared_count++;
                break;
            case PARTICIPANT_COMMITTED:
                committed_count++;
                break;
            case PARTICIPANT_ABORTED:
                aborted_count++;
                break;
            default:
                // Unknown state - treat as abort
                aborted_count++;
                break;
        }
    }
    
    // Decision logic
    if (committed_count > 0) {
        // Some participants already committed - must complete commit
        complete_commit_all_participants(coordinator, rtxn);
        return 0;
    } else if (aborted_count > 0) {
        // Some participants aborted - must abort all
        abort_all_participants(coordinator, rtxn);
        return -1;
    } else if (prepared_count == rtxn->participant_count) {
        // All participants still prepared - can commit
        complete_commit_all_participants(coordinator, rtxn);
        return 0;
    } else {
        // Mixed or unknown states - abort for safety
        abort_all_participants(coordinator, rtxn);
        return -1;
    }
}
```

---

## **🔬 Testing Strategies**

### **Property-Based Testing**

#### **Transaction Properties to Verify**
```c
// Property 1: Atomicity
// Either all participants commit or all abort
bool property_atomicity(Transaction* txn) {
    int committed = 0, aborted = 0;
    
    for (size_t i = 0; i < txn->participant_count; i++) {
        switch (txn->participants[i].state) {
            case PARTICIPANT_COMMITTED:
                committed++;
                break;
            case PARTICIPANT_ABORTED:
                aborted++;
                break;
            default:
                return false; // Invalid final state
        }
    }
    
    // Must be all committed OR all aborted
    return (committed == txn->participant_count) || 
           (aborted == txn->participant_count);
}

// Property 2: Consistency
// State transitions must follow the protocol
bool property_consistency(TransactionLog* log, const char* txn_id) {
    TransactionState prev_state = TXN_INIT;
    bool valid = true;
    
    LogEntry* entries = read_log_entries_for_transaction(log, txn_id);
    
    for (size_t i = 0; entries[i].txn_id[0] != '\0'; i++) {
        TransactionState curr_state = entries[i].state;
        
        // Valid state transitions
        switch (prev_state) {
            case TXN_INIT:
                valid = (curr_state == TXN_PREPARING || curr_state == TXN_ABORTING);
                break;
            case TXN_PREPARING:
                valid = (curr_state == TXN_PREPARED || curr_state == TXN_ABORTING);
                break;
            case TXN_PREPARED:
                valid = (curr_state == TXN_COMMITTING || curr_state == TXN_ABORTING);
                break;
            case TXN_COMMITTING:
                valid = (curr_state == TXN_COMMITTED || curr_state == TXN_ABORTING);
                break;
            case TXN_COMMITTED:
            case TXN_ABORTED:
                valid = false; // Terminal states
                break;
            default:
                valid = false;
        }
        
        if (!valid) break;
        prev_state = curr_state;
    }
    
    free(entries);
    return valid;
}

// Property 3: Isolation
// Concurrent transactions don't interfere with each other
bool property_isolation(Transaction* txn1, Transaction* txn2) {
    // Check if transactions share any resources
    for (size_t i = 0; i < txn1->participant_count; i++) {
        for (size_t j = 0; j < txn2->participant_count; j++) {
            if (strcmp(txn1->participants[i].name, 
                      txn2->participants[j].name) == 0) {
                
                // Shared participant - check for proper isolation
                // This would require participant-specific isolation checking
                if (!check_participant_isolation(
                        &txn1->participants[i], 
                        &txn2->participants[j])) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

// Property 4: Durability
// Committed transactions survive system crashes
bool property_durability(const char* txn_id) {
    // Check if committed transaction is in the log
    TransactionLog* log = transaction_log_open_readonly();
    
    TransactionState final_state = get_final_transaction_state(log, txn_id);
    
    transaction_log_close(log);
    
    return (final_state == TXN_COMMITTED);
}
```

#### **Chaos Testing Implementation**
```c
// Chaos testing: Inject failures at random points
typedef enum {
    CHAOS_NETWORK_DELAY,
    CHAOS_NETWORK_PARTITION,
    CHAOS_PARTICIPANT_CRASH,
    CHAOS_COORDINATOR_CRASH,
    CHAOS_DISK_FULL,
    CHAOS_MEMORY_EXHAUSTION
} ChaosType;

typedef struct {
    ChaosType type;
    double probability;     // 0.0 - 1.0
    int duration_ms;       // How long the chaos lasts
    bool active;
} ChaosInjector;

// Network delay injection
int chaos_network_delay(int original_delay_ms) {
    static ChaosInjector injector = {
        .type = CHAOS_NETWORK_DELAY,
        .probability = 0.1,  // 10% chance
        .duration_ms = 5000, // 5 second delay
        .active = false
    };
    
    if (!injector.active && (rand() / (double)RAND_MAX) < injector.probability) {
        injector.active = true;
        log_message_json("CHAOS", "network", NULL, "Injecting network delay", 
                        injector.duration_ms);
        return original_delay_ms + injector.duration_ms;
    }
    
    return original_delay_ms;
}

// Participant crash simulation
int chaos_participant_crash(Participant* participant, const char* txn_id) {
    static ChaosInjector injector = {
        .type = CHAOS_PARTICIPANT_CRASH,
        .probability = 0.05,  // 5% chance
        .duration_ms = 0,
        .active = false
    };
    
    if ((rand() / (double)RAND_MAX) < injector.probability) {
        log_message_json("CHAOS", "participant", txn_id, 
                        "Simulating participant crash", -1);
        
        // Simulate crash by returning error and cleaning up state
        participant->state = PARTICIPANT_FAILED;
        return -1;  // Crash!
    }
    
    return 0;  // Normal operation
}

// Comprehensive chaos test
void run_chaos_test(int duration_seconds, int transaction_rate) {
    printf("🌪️  Starting chaos test...\n");
    printf("Duration: %d seconds\n", duration_seconds);
    printf("Transaction rate: %d TPS\n", transaction_rate);
    
    time_t start_time = time(NULL);
    time_t end_time = start_time + duration_seconds;
    
    int total_transactions = 0;
    int successful_transactions = 0;
    int failed_transactions = 0;
    
    while (time(NULL) < end_time) {
        // Generate transaction
        char txn_id[64];
        snprintf(txn_id, sizeof(txn_id), "chaos_%d_%ld", 
                total_transactions, time(NULL));
        
        TransactionCoordinator* coordinator = txn_coordinator_init();
        Transaction* txn = txn_begin(coordinator, txn_id);
        
        // Add participants with chaos injection
        MockParticipant db_mock = {
            .name = "database",
            .fail_prepare = false,
            .fail_commit = false
        };
        
        MockParticipant clearing_mock = {
            .name = "clearing", 
            .fail_prepare = false,
            .fail_commit = false
        };
        
        // Inject chaos into participant behavior
        if (chaos_participant_crash(NULL, txn_id) != 0) {
            db_mock.fail_prepare = true;
        }
        
        txn_register_participant(txn, "database", &db_mock,
                               mock_participant_prepare_with_chaos,
                               mock_participant_commit_with_chaos,
                               mock_participant_abort);
        
        txn_register_participant(txn, "clearing", &clearing_mock,
                               mock_participant_prepare_with_chaos,
                               mock_participant_commit_with_chaos,
                               mock_participant_abort);
        
        // Execute transaction
        int result = txn_commit(coordinator, txn);
        
        total_transactions++;
        if (result == 0) {
            successful_transactions++;
        } else {
            failed_transactions++;
        }
        
        txn_coordinator_destroy(coordinator);
        
        // Rate limiting
        if (transaction_rate > 0) {
            usleep(1000000 / transaction_rate);  // Convert TPS to delay
        }
        
        // Progress reporting
        if (total_transactions % 100 == 0) {
            printf("Processed: %d, Success: %d, Failed: %d\n",
                   total_transactions, successful_transactions, failed_transactions);
        }
    }
    
    printf("\n🏁 Chaos test completed!\n");
    printf("Total transactions: %d\n", total_transactions);
    printf("Successful: %d (%.1f%%)\n", successful_transactions,
           (successful_transactions * 100.0) / total_transactions);
    printf("Failed: %d (%.1f%%)\n", failed_transactions,
           (failed_transactions * 100.0) / total_transactions);
    
    // Verify system properties still hold
    bool atomicity_ok = verify_atomicity_property();
    bool consistency_ok = verify_consistency_property();
    bool durability_ok = verify_durability_property();
    
    printf("\nProperty verification:\n");
    printf("Atomicity: %s\n", atomicity_ok ? "✅ PASS" : "❌ FAIL");
    printf("Consistency: %s\n", consistency_ok ? "✅ PASS" : "❌ FAIL");
    printf("Durability: %s\n", durability_ok ? "✅ PASS" : "❌ FAIL");
}
```

---

# 🎯 **PHẦN IV: PRODUCTION DEPLOYMENT**

## **📊 Monitoring & Observability**

### **Metrics Collection**
```c
// Comprehensive metrics for production monitoring
typedef struct {
    // Transaction metrics
    atomic_long total_transactions;
    atomic_long committed_transactions;
    atomic_long aborted_transactions;
    atomic_long failed_transactions;
    
    // Performance metrics
    atomic_long total_prepare_time_us;
    atomic_long total_commit_time_us;
    atomic_long max_prepare_time_us;
    atomic_long max_commit_time_us;
    
    // Error metrics
    atomic_long prepare_timeouts;
    atomic_long commit_timeouts;
    atomic_long network_errors;
    atomic_long participant_failures;
    
    // Resource metrics
    atomic_long active_transaction_count;
    atomic_long peak_active_transactions;
    atomic_long memory_usage_bytes;
    
    // Business metrics
    atomic_long total_amount_processed_cents;
    atomic_long declined_amount_cents;
    
    // Health metrics
    time_t last_successful_transaction;
    atomic_long consecutive_failures;
} TransactionMetrics;

// Prometheus-style metrics export
void export_metrics_prometheus(TransactionMetrics* metrics, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size,
        "# HELP visa_transactions_total Total number of transactions\n"
        "# TYPE visa_transactions_total counter\n"
        "visa_transactions_total %ld\n"
        
        "# HELP visa_transactions_committed_total Committed transactions\n"
        "# TYPE visa_transactions_committed_total counter\n"
        "visa_transactions_committed_total %ld\n"
        
        "# HELP visa_transactions_aborted_total Aborted transactions\n" 
        "# TYPE visa_transactions_aborted_total counter\n"
        "visa_transactions_aborted_total %ld\n"
        
        "# HELP visa_transaction_duration_seconds Transaction duration\n"
        "# TYPE visa_transaction_duration_seconds histogram\n"
        "visa_transaction_duration_seconds_bucket{le=\"0.001\"} %ld\n"
        "visa_transaction_duration_seconds_bucket{le=\"0.005\"} %ld\n"
        "visa_transaction_duration_seconds_bucket{le=\"0.01\"} %ld\n"
        "visa_transaction_duration_seconds_bucket{le=\"0.05\"} %ld\n"
        "visa_transaction_duration_seconds_bucket{le=\"0.1\"} %ld\n"
        "visa_transaction_duration_seconds_bucket{le=\"+Inf\"} %ld\n"
        
        "# HELP visa_active_transactions Current active transactions\n"
        "# TYPE visa_active_transactions gauge\n" 
        "visa_active_transactions %ld\n"
        
        "# HELP visa_amount_processed_total Total amount processed in cents\n"
        "# TYPE visa_amount_processed_total counter\n"
        "visa_amount_processed_total %ld\n",
        
        atomic_load(&metrics->total_transactions),
        atomic_load(&metrics->committed_transactions),
        atomic_load(&metrics->aborted_transactions),
        // ... histogram buckets would be calculated from timing data
        calculate_duration_bucket(metrics, 1000),   // < 1ms
        calculate_duration_bucket(metrics, 5000),   // < 5ms  
        calculate_duration_bucket(metrics, 10000),  // < 10ms
        calculate_duration_bucket(metrics, 50000),  // < 50ms
        calculate_duration_bucket(metrics, 100000), // < 100ms
        atomic_load(&metrics->total_transactions),  // total
        atomic_load(&metrics->active_transaction_count),
        atomic_load(&metrics->total_amount_processed_cents)
    );
}

// Health check endpoint
typedef enum {
    HEALTH_HEALTHY,
    HEALTH_DEGRADED, 
    HEALTH_UNHEALTHY
} HealthStatus;

HealthStatus get_system_health(TransactionCoordinator* coordinator, 
                              TransactionMetrics* metrics) {
    
    time_t now = time(NULL);
    
    // Check 1: Recent transaction activity
    if (now - metrics->last_successful_transaction > 300) {  // 5 minutes
        return HEALTH_UNHEALTHY;  // No successful transactions recently
    }
    
    // Check 2: Error rate
    long total = atomic_load(&metrics->total_transactions);
    long failed = atomic_load(&metrics->failed_transactions);
    
    if (total > 100 && (failed * 100 / total) > 10) {  // > 10% error rate
        return HEALTH_DEGRADED;
    }
    
    // Check 3: Resource usage
    long active = atomic_load(&metrics->active_transaction_count);
    if (active > MAX_ACTIVE_TRANSACTIONS * 0.9) {  // > 90% capacity
        return HEALTH_DEGRADED;
    }
    
    // Check 4: Consecutive failures
    if (atomic_load(&metrics->consecutive_failures) > 5) {
        return HEALTH_DEGRADED;
    }
    
    return HEALTH_HEALTHY;
}
```

### **Distributed Tracing Integration**
```c
// OpenTelemetry-style distributed tracing
typedef struct {
    char trace_id[33];      // 32 hex chars + null terminator
    char span_id[17];       // 16 hex chars + null terminator
    char parent_span_id[17];
    time_t start_time;
    time_t end_time;
    
    // Span attributes
    char operation_name[64];
    char service_name[32];
    int status_code;        // 0 = OK, non-zero = error
    
    // Custom attributes
    HashMap* attributes;
} TraceSpan;

TraceSpan* trace_start_transaction(const char* txn_id) {
    TraceSpan* span = malloc(sizeof(TraceSpan));
    
    // Generate trace ID (in real implementation, use proper UUID)
    generate_trace_id(span->trace_id, sizeof(span->trace_id));
    generate_span_id(span->span_id, sizeof(span->span_id));
    span->parent_span_id[0] = '\0';  // Root span
    
    span->start_time = time(NULL);
    strcpy(span->operation_name, "transaction.2pc");
    strcpy(span->service_name, "visa-server");
    span->status_code = 0;
    
    span->attributes = hashmap_create();
    hashmap_put(span->attributes, "transaction.id", strdup(txn_id));
    hashmap_put(span->attributes, "transaction.protocol", strdup("2pc"));
    
    return span;
}

TraceSpan* trace_start_participant_span(TraceSpan* parent, 
                                       const char* participant_name,
                                       const char* operation) {
    TraceSpan* span = malloc(sizeof(TraceSpan));
    
    // Inherit trace ID from parent
    strcpy(span->trace_id, parent->trace_id);
    generate_span_id(span->span_id, sizeof(span->span_id));
    strcpy(span->parent_span_id, parent->span_id);
    
    span->start_time = time(NULL);
    snprintf(span->operation_name, sizeof(span->operation_name), 
             "participant.%s", operation);
    strcpy(span->service_name, "visa-server");
    
    span->attributes = hashmap_create();
    hashmap_put(span->attributes, "participant.name", strdup(participant_name));
    hashmap_put(span->attributes, "participant.operation", strdup(operation));
    
    return span;
}

void trace_finish_span(TraceSpan* span, int status_code) {
    span->end_time = time(NULL);
    span->status_code = status_code;
    
    // Export span to tracing backend (Jaeger, Zipkin, etc.)
    export_span_to_backend(span);
    
    // Cleanup
    hashmap_destroy_with_values(span->attributes, free);
    free(span);
}

// Usage in transaction coordinator
int txn_commit_with_tracing(TransactionCoordinator* coordinator, 
                           Transaction* txn) {
    
    // Start root trace span
    TraceSpan* txn_span = trace_start_transaction(txn->transaction_id);
    
    // Phase 1: PREPARE with tracing
    hashmap_put(txn_span->attributes, "phase", strdup("prepare"));
    
    bool all_prepared = true;
    for (size_t i = 0; i < txn->participant_count; i++) {
        Participant* p = &txn->participants[i];
        
        // Start participant span
        TraceSpan* prepare_span = trace_start_participant_span(
            txn_span, p->name, "prepare");
        
        int result = p->prepare(p->context, txn->transaction_id);
        
        // Add participant-specific attributes
        hashmap_put(prepare_span->attributes, "result", 
                   strdup(result == 0 ? "success" : "failure"));
        
        trace_finish_span(prepare_span, result);
        
        if (result != 0) {
            all_prepared = false;
            break;
        }
    }
    
    // Phase 2: COMMIT/ABORT with tracing
    hashmap_put(txn_span->attributes, "phase", 
               strdup(all_prepared ? "commit" : "abort"));
    
    if (all_prepared) {
        for (size_t i = 0; i < txn->participant_count; i++) {
            Participant* p = &txn->participants[i];
            
            TraceSpan* commit_span = trace_start_participant_span(
                txn_span, p->name, "commit");
            
            int result = p->commit(p->context, txn->transaction_id);
            
            hashmap_put(commit_span->attributes, "result",
                       strdup(result == 0 ? "success" : "failure"));
            
            trace_finish_span(commit_span, result);
        }
        
        trace_finish_span(txn_span, 0);  // Success
        return 0;
    } else {
        // Abort all participants
        for (size_t i = 0; i < txn->participant_count; i++) {
            Participant* p = &txn->participants[i];
            
            TraceSpan* abort_span = trace_start_participant_span(
                txn_span, p->name, "abort");
            
            p->abort(p->context, txn->transaction_id);
            
            trace_finish_span(abort_span, 0);  // Abort always succeeds
        }
        
        trace_finish_span(txn_span, -1);  // Aborted
        return -1;
    }
}
```

---

## **🔧 Configuration Management**

### **Runtime Configuration**
```c
// Production-ready configuration system
typedef struct {
    // Coordinator settings
    int max_active_transactions;
    int coordinator_thread_count;
    int transaction_timeout_seconds;
    
    // Participant timeouts
    int prepare_timeout_seconds;
    int commit_timeout_seconds;
    int abort_timeout_seconds;
    
    // Retry settings
    int max_retries;
    int initial_retry_delay_ms;
    int max_retry_delay_ms;
    bool exponential_backoff;
    
    // Logging settings
    char log_level[16];          // DEBUG, INFO, WARN, ERROR
    char log_format[16];         // JSON, TEXT
    bool log_to_file;
    char log_file_path[256];
    int log_rotation_size_mb;
    
    // Performance tuning
    int transaction_pool_size;
    int io_thread_count;
    bool enable_batching;
    int batch_size;
    int batch_timeout_ms;
    
    // Health check settings
    int health_check_interval_seconds;
    int unhealthy_threshold_consecutive_failures;
    
    // Feature flags
    bool enable_tracing;
    bool enable_metrics;
    bool enable_chaos_testing;
    
} Config;

// Load configuration from JSON file
Config* config_load_from_file(const char* config_file) {
    FILE* file = fopen(config_file, "r");
    if (!file) {
        fprintf(stderr, "Cannot open config file: %s\n", config_file);
        return NULL;
    }
    
    // Read entire file into buffer
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* json_buffer = malloc(file_size + 1);
    fread(json_buffer, 1, file_size, file);
    json_buffer[file_size] = '\0';
    fclose(file);
    
    // Parse JSON (using a JSON library like cJSON)
    cJSON* json = cJSON_Parse(json_buffer);
    free(json_buffer);
    
    if (!json) {
        fprintf(stderr, "Invalid JSON in config file\n");
        return NULL;
    }
    
    Config* config = malloc(sizeof(Config));
    
    // Parse coordinator settings
    cJSON* coordinator = cJSON_GetObjectItem(json, "coordinator");
    if (coordinator) {
        config->max_active_transactions = 
            cJSON_GetObjectItem(coordinator, "max_active_transactions")->valueint;
        config->coordinator_thread_count = 
            cJSON_GetObjectItem(coordinator, "thread_count")->valueint;
        config->transaction_timeout_seconds = 
            cJSON_GetObjectItem(coordinator, "timeout_seconds")->valueint;
    }
    
    // Parse timeout settings
    cJSON* timeouts = cJSON_GetObjectItem(json, "timeouts");
    if (timeouts) {
        config->prepare_timeout_seconds = 
            cJSON_GetObjectItem(timeouts, "prepare_seconds")->valueint;
        config->commit_timeout_seconds = 
            cJSON_GetObjectItem(timeouts, "commit_seconds")->valueint;
        config->abort_timeout_seconds = 
            cJSON_GetObjectItem(timeouts, "abort_seconds")->valueint;
    }
    
    // Parse retry settings
    cJSON* retries = cJSON_GetObjectItem(json, "retries");
    if (retries) {
        config->max_retries = 
            cJSON_GetObjectItem(retries, "max_attempts")->valueint;
        config->initial_retry_delay_ms = 
            cJSON_GetObjectItem(retries, "initial_delay_ms")->valueint;
        config->max_retry_delay_ms = 
            cJSON_GetObjectItem(retries, "max_delay_ms")->valueint;
        config->exponential_backoff = 
            cJSON_IsTrue(cJSON_GetObjectItem(retries, "exponential_backoff"));
    }
    
    // Parse logging settings
    cJSON* logging = cJSON_GetObjectItem(json, "logging");
    if (logging) {
        strcpy(config->log_level, 
               cJSON_GetObjectItem(logging, "level")->valuestring);
        strcpy(config->log_format, 
               cJSON_GetObjectItem(logging, "format")->valuestring);
        config->log_to_file = 
            cJSON_IsTrue(cJSON_GetObjectItem(logging, "to_file"));
        strcpy(config->log_file_path, 
               cJSON_GetObjectItem(logging, "file_path")->valuestring);
    }
    
    // Parse feature flags
    cJSON* features = cJSON_GetObjectItem(json, "features");
    if (features) {
        config->enable_tracing = 
            cJSON_IsTrue(cJSON_GetObjectItem(features, "tracing"));
        config->enable_metrics = 
            cJSON_IsTrue(cJSON_GetObjectItem(features, "metrics"));
        config->enable_chaos_testing = 
            cJSON_IsTrue(cJSON_GetObjectItem(features, "chaos_testing"));
    }
    
    cJSON_Delete(json);
    return config;
}

// Hot-reload configuration without restarting
volatile sig_atomic_t config_reload_requested = 0;

void signal_handler_reload_config(int sig) {
    config_reload_requested = 1;
}

void setup_config_hot_reload(void) {
    signal(SIGUSR1, signal_handler_reload_config);
}

int check_and_reload_config(TransactionCoordinator* coordinator, 
                           Config** current_config,
                           const char* config_file) {
    
    if (!config_reload_requested) {
        return 0;  // No reload requested
    }
    
    config_reload_requested = 0;  // Reset flag
    
    Config* new_config = config_load_from_file(config_file);
    if (!new_config) {
        log_message_json("ERROR", "config", NULL, 
                        "Failed to reload configuration", -1);
        return -1;
    }
    
    // Apply new configuration
    Config* old_config = *current_config;
    *current_config = new_config;
    
    // Update coordinator settings that can be changed at runtime
    coordinator->max_active_transactions = new_config->max_active_transactions;
    
    // Log configuration change
    log_message_json("INFO", "config", NULL, 
                    "Configuration reloaded successfully", -1);
    
    free(old_config);
    return 0;
}

// Example configuration file (config.json)
const char* example_config_json = 
"{"
"  \"coordinator\": {"
"    \"max_active_transactions\": 1024,"
"    \"thread_count\": 8,"
"    \"timeout_seconds\": 300"
"  },"
"  \"timeouts\": {"
"    \"prepare_seconds\": 30,"
"    \"commit_seconds\": 30,"
"    \"abort_seconds\": 10"
"  },"
"  \"retries\": {"
"    \"max_attempts\": 3,"
"    \"initial_delay_ms\": 100,"
"    \"max_delay_ms\": 5000,"
"    \"exponential_backoff\": true"
"  },"
"  \"logging\": {"
"    \"level\": \"INFO\","
"    \"format\": \"JSON\","
"    \"to_file\": true,"
"    \"file_path\": \"/var/log/visa/transactions.log\","
"    \"rotation_size_mb\": 100"
"  },"
"  \"performance\": {"
"    \"transaction_pool_size\": 2048,"
"    \"io_thread_count\": 4,"
"    \"enable_batching\": true,"
"    \"batch_size\": 100,"
"    \"batch_timeout_ms\": 10"
"  },"
"  \"features\": {"
"    \"tracing\": true,"
"    \"metrics\": true,"
"    \"chaos_testing\": false"
"  }"
"}";
```

---

# 🎓 **PHẦN V: EXERCISE & CHALLENGES**

## **🏋️ Thực hành nâng cao**

### **Challenge 1: Connection Pooling Integration**
```c
/*
 * CHALLENGE: Implement database connection pooling for participants
 * 
 * Requirements:
 * 1. Pool of reusable database connections
 * 2. Connection health checking
 * 3. Connection timeout and cleanup
 * 4. Thread-safe connection acquisition/release
 * 5. Performance monitoring
 * 
 * Difficulty: ⭐⭐⭐
 */

typedef struct DBConnectionPool DBConnectionPool;

DBConnectionPool* db_pool_create(const char* connection_string, 
                                int min_connections, 
                                int max_connections);

DBConnection* db_pool_acquire(DBConnectionPool* pool, int timeout_ms);
void db_pool_release(DBConnectionPool* pool, DBConnection* conn);

// Integration với db_participant.c
int db_participant_prepare_with_pool(void* context, const char* txn_id) {
    DBParticipantContext* ctx = (DBParticipantContext*)context;
    
    // Acquire connection from pool
    DBConnection* conn = db_pool_acquire(ctx->pool, 5000);  // 5s timeout
    if (!conn) {
        return -1;  // Pool exhausted
    }
    
    // Execute prepare logic
    int result = execute_prepare_transaction(conn, txn_id);
    
    // Release connection back to pool
    db_pool_release(ctx->pool, conn);
    
    return result;
}
```

### **Challenge 2: Saga Pattern Alternative**
```c
/*
 * CHALLENGE: Implement Saga pattern as alternative to 2PC
 * 
 * Requirements:
 * 1. Choreography-based saga (event-driven)
 * 2. Compensation actions for rollback
 * 3. Saga execution coordinator
 * 4. Event sourcing for saga state
 * 5. Compare performance with 2PC
 * 
 * Difficulty: ⭐⭐⭐⭐
 */

typedef struct {
    char saga_id[64];
    char transaction_id[64];
    SagaState state;
    
    // Saga steps
    SagaStep steps[MAX_SAGA_STEPS];
    int current_step;
    int total_steps;
    
    // Compensation trail
    CompensationAction compensations[MAX_SAGA_STEPS];
    int compensation_count;
} Saga;

typedef struct {
    char step_name[64];
    int (*execute)(void* context, const char* saga_id);
    int (*compensate)(void* context, const char* saga_id);
    void* context;
} SagaStep;

// Example: Payment processing saga
Saga* create_payment_saga(const char* txn_id) {
    Saga* saga = malloc(sizeof(Saga));
    
    // Step 1: Reserve funds
    saga->steps[0] = (SagaStep) {
        .step_name = "reserve_funds",
        .execute = reserve_funds_step,
        .compensate = release_funds_compensation,
        .context = create_funds_context()
    };
    
    // Step 2: Authorize with clearing
    saga->steps[1] = (SagaStep) {
        .step_name = "authorize_clearing",
        .execute = authorize_clearing_step,
        .compensate = cancel_authorization_compensation,
        .context = create_clearing_context()
    };
    
    // Step 3: Update ledger
    saga->steps[2] = (SagaStep) {
        .step_name = "update_ledger",
        .execute = update_ledger_step,
        .compensate = revert_ledger_compensation,
        .context = create_ledger_context()
    };
    
    saga->total_steps = 3;
    saga->current_step = 0;
    saga->state = SAGA_STARTED;
    
    return saga;
}

int execute_saga(SagaCoordinator* coordinator, Saga* saga) {
    for (int i = 0; i < saga->total_steps; i++) {
        SagaStep* step = &saga->steps[i];
        
        int result = step->execute(step->context, saga->saga_id);
        
        if (result == 0) {
            // Step succeeded - record compensation
            saga->compensations[saga->compensation_count++] = (CompensationAction) {
                .step_index = i,
                .compensate = step->compensate,
                .context = step->context
            };
            
            saga->current_step++;
        } else {
            // Step failed - run compensations in reverse order
            saga->state = SAGA_COMPENSATING;
            
            for (int j = saga->compensation_count - 1; j >= 0; j--) {
                CompensationAction* comp = &saga->compensations[j];
                comp->compensate(comp->context, saga->saga_id);
            }
            
            saga->state = SAGA_COMPENSATED;
            return -1;
        }
    }
    
    saga->state = SAGA_COMPLETED;
    return 0;
}
```

### **Challenge 3: Performance Optimization**
```c
/*
 * CHALLENGE: Optimize 2PC for high-throughput scenarios
 * 
 * Requirements:
 * 1. Lock-free data structures where possible
 * 2. Batch processing of transactions
 * 3. Pipeline parallelism (overlap prepare/commit phases)
 * 4. Memory pool allocation
 * 5. NUMA-aware thread placement
 * 
 * Difficulty: ⭐⭐⭐⭐⭐
 */

// Lock-free transaction queue
typedef struct {
    alignas(64) volatile long head;
    alignas(64) volatile long tail;
    alignas(64) Transaction* items[QUEUE_SIZE];
} LockFreeTransactionQueue;

// NUMA-aware coordinator
typedef struct {
    int numa_node;
    cpu_set_t cpu_set;
    TransactionCoordinator* coordinators[MAX_NUMA_NODES];
} NUMACoordinator;

// Pipelined batch processor
typedef struct {
    // Stage 1: Prepare phase
    ThreadPool* prepare_pool;
    WorkQueue* prepare_queue;
    
    // Stage 2: Commit phase  
    ThreadPool* commit_pool;
    WorkQueue* commit_queue;
    
    // Pipeline buffers
    TransactionBatch* pipeline_buffer[PIPELINE_DEPTH];
    volatile int buffer_head;
    volatile int buffer_tail;
} PipelinedBatchProcessor;

int process_pipelined_batch(PipelinedBatchProcessor* processor) {
    // Stage 1: Prepare phase (parallel)
    TransactionBatch* prepare_batch = acquire_prepare_batch(processor);
    
    for (int i = 0; i < prepare_batch->count; i++) {
        Transaction* txn = prepare_batch->transactions[i];
        
        // Submit prepare work to thread pool
        PrepareWork* work = create_prepare_work(txn);
        threadpool_submit(processor->prepare_pool, prepare_worker, work);
    }
    
    // Wait for all prepares to complete
    wait_for_prepare_completion(prepare_batch);
    
    // Stage 2: Move successful prepares to commit pipeline
    TransactionBatch* commit_batch = create_commit_batch();
    
    for (int i = 0; i < prepare_batch->count; i++) {
        Transaction* txn = prepare_batch->transactions[i];
        
        if (txn->state == TXN_PREPARED) {
            commit_batch->transactions[commit_batch->count++] = txn;
        }
    }
    
    // Submit commit batch to next stage
    if (commit_batch->count > 0) {
        enqueue_commit_batch(processor, commit_batch);
    }
    
    return 0;
}
```

### **Challenge 4: Cross-Datacenter 2PC**
```c
/*
 * CHALLENGE: Extend 2PC across multiple datacenters
 * 
 * Requirements:
 * 1. Network partition tolerance
 * 2. Cross-DC latency optimization
 * 3. Regional coordinator failover
 * 4. Consensus-based recovery (Raft/PBFT)
 * 5. Geo-distributed transaction logs
 * 
 * Difficulty: ⭐⭐⭐⭐⭐
 */

typedef struct {
    char datacenter_id[32];
    char region[32];
    NetworkEndpoint endpoints[MAX_ENDPOINTS];
    int endpoint_count;
    
    // Network characteristics
    int avg_latency_ms;
    double packet_loss_rate;
    int bandwidth_mbps;
    
    // Health status
    bool is_reachable;
    time_t last_heartbeat;
} Datacenter;

typedef struct {
    Datacenter datacenters[MAX_DATACENTERS];
    int datacenter_count;
    
    // Consensus group for coordinator election
    RaftNode* consensus_nodes[MAX_CONSENSUS_NODES];
    int consensus_node_count;
    
    // Cross-DC transaction log replication
    ReplicationGroup* log_replication;
    
} GeodistributedCoordinator;

// Cross-DC transaction with consensus
int geodistributed_2pc_commit(GeodistributedCoordinator* geo_coord,
                             Transaction* txn) {
    
    // Step 1: Elect coordinator using Raft consensus
    char coordinator_dc[32];
    if (elect_coordinator_datacenter(geo_coord, coordinator_dc) != 0) {
        return -1;  // No consensus on coordinator
    }
    
    // Step 2: Execute 2PC with cross-DC participants
    bool all_prepared = true;
    
    for (int i = 0; i < txn->participant_count; i++) {
        Participant* p = &txn->participants[i];
        
        // Determine participant's datacenter
        char participant_dc[32];
        get_participant_datacenter(p, participant_dc);
        
        // Cross-DC prepare with retry and timeout
        int result = cross_dc_prepare(geo_coord, participant_dc, p, txn->transaction_id);
        
        if (result != 0) {
            all_prepared = false;
            break;
        }
    }
    
    // Step 3: Replicate decision to all DCs before proceeding
    TransactionDecision decision = all_prepared ? DECISION_COMMIT : DECISION_ABORT;
    
    if (replicate_decision_to_all_dcs(geo_coord, txn->transaction_id, decision) != 0) {
        // Replication failed - cannot proceed safely
        return -1;
    }
    
    // Step 4: Execute final phase across all DCs
    if (all_prepared) {
        return cross_dc_commit_all(geo_coord, txn);
    } else {
        return cross_dc_abort_all(geo_coord, txn);
    }
}
```

---

## **📝 Certification Questions**

### **Level 1: Basic Understanding**

1. **Giải thích sự khác biệt giữa 2PC và 3PC?**
2. **Tại sao 2PC được gọi là "blocking protocol"?**
3. **Trong tình huống nào 2PC có thể dẫn đến inconsistency?**
4. **So sánh 2PC với Saga pattern về performance và consistency?**

### **Level 2: Implementation Details**

5. **Phân tích race condition có thể xảy ra trong implementation?**
6. **Thiết kế timeout strategy cho từng phase của 2PC?**
7. **Implement recovery algorithm cho coordinator crash?**
8. **Tối ưu hóa 2PC cho high-throughput environment?**

### **Level 3: Production Scenarios**

9. **Thiết kế monitoring và alerting cho 2PC system?**
10. **Xử lý network partition trong multi-datacenter setup?**
11. **Implement circuit breaker pattern cho participant failures?**
12. **So sánh trade-offs giữa 2PC và eventual consistency?**

---

## **🏆 Graduation Project**

### **Mini Banking System với 2PC**

**Objective**: Xây dựng hệ thống banking hoàn chỉnh sử dụng 2PC

**Components**:
1. **Account Service** - Quản lý tài khoản
2. **Transaction Service** - Xử lý giao dịch
3. **Notification Service** - Gửi thông báo
4. **Audit Service** - Ghi log compliance
5. **Fraud Detection Service** - Kiểm tra gian lận

**Requirements**:
- [ ] Implement complete 2PC across all services
- [ ] Add circuit breaker cho each service
- [ ] Implement distributed tracing
- [ ] Add comprehensive monitoring
- [ ] Load test với 1000+ TPS
- [ ] Chaos testing với network failures
- [ ] Recovery testing từ coordinator crash
- [ ] Documentation và runbook

**Evaluation Criteria**:
- **Correctness**: ACID properties maintained
- **Performance**: Sub-100ms P99 latency
- **Reliability**: 99.9% uptime under chaos
- **Observability**: Complete monitoring stack
- **Documentation**: Production-ready docs

---

**🎯 Completion của guide này chứng tỏ bạn đã master 2-Phase Commit ở production level!**

**Next Steps**: Microservices Patterns, Event Sourcing, CQRS, Distributed Consensus Algorithms