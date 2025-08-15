/*
 * Thread Pool — Visual Overview / Sơ đồ tổng quan (EN + VN)
 *
 *              submit(job)
 *  producers  --------------->  [ LOCK ]  queue (bounded FIFO)  [ UNLOCK ]
 *                                        head ......... tail
 *                                                ^ size<=cap
 *                                                   |
 *                                             condvar signal
 *                                                   v
 *                        [wait cv]  worker ----> pop one ----> run outside lock
 *
 *  EN: Backpressure — when size == cap, submit() returns -1 so caller can
 *      fail fast (e.g., send {"reason":"server_busy"}) instead of queuing
 *      unbounded work which would explode latency (p95/p99).
 *
 *  VN: Chống quá tải — khi hàng đợi đầy (size==cap), submit() trả -1 để bên
 *      gọi (vòng accept) phản hồi "server_busy" ngay, không xếp hàng vô hạn.
 *      Cách này giữ độ trễ (p95/p99) ổn định thay vì phình to khi quá tải.
 */
#include "threadpool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

// A single job to be executed by the pool
// VN: Mỗi công việc là một cặp (hàm, tham số) nối nhau thành hàng đợi FIFO.
typedef struct Job {
    job_fn fn;
    void *arg;
    struct Job *next;
} Job;

// Fixed-size thread pool with a bounded FIFO queue
// VN: Số worker cố định; queue giới hạn để áp dụng backpressure khi quá tải.
struct ThreadPool {
    // [ANCHOR:TP_QUEUE_STRUCT] Hàng đợi FIFO có giới hạn (bounded) để áp dụng backpressure
    pthread_t *threads;
    int num_threads;
    Job *head;
    Job *tail;
    size_t size;
    size_t cap; // bounded queue capacity
    pthread_mutex_t m;
    pthread_cond_t cv;
    int shutting_down;
};

// Worker loop: wait for a job, pop it, run it outside the lock
// VN: Chờ tín hiệu có việc, lấy 1 job khỏi queue, nhả khóa rồi thực thi.
static void *worker_main(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
    for (;;) {
        pthread_mutex_lock(&pool->m);
        // [ANCHOR:TP_WORKER_WAIT]
        // EN: Wait until there is work OR shutdown requested
        // VN: Chờ tới khi có việc HOẶC nhận tín hiệu tắt (shutdown)
        while (!pool->shutting_down && pool->size == 0) {
            pthread_cond_wait(&pool->cv, &pool->m);
        }
        // [ANCHOR:TP_WORKER_EXIT]
        // EN: Graceful exit when no more jobs and shutdown flag is set
        // VN: Thoát êm khi đã yêu cầu shutdown và không còn job trong hàng đợi
        if (pool->shutting_down && pool->size == 0) {
            pthread_mutex_unlock(&pool->m);
            break;
        }
        Job *job = pool->head;
        if (job) {
            pool->head = job->next;
            if (!pool->head) pool->tail = NULL;
            pool->size--;
        }
        pthread_mutex_unlock(&pool->m);
        // [ANCHOR:TP_EXECUTE_OUTSIDE_LOCK]
        // EN: Execute job outside the lock to minimize contention
        // VN: Chạy job ngoài phạm vi khóa để giảm tranh chấp mutex
        if (job) {
            job->fn(job->arg);
            free(job);
        }
    }
    return NULL;
}

// Create a thread pool with num_threads workers and queue_cap capacity
// VN: Tạo các worker trước; đây là biện pháp giới hạn song song an toàn.
ThreadPool *threadpool_create(int num_threads, int queue_cap) {
    if (num_threads <= 0) num_threads = 4;
    ThreadPool *pool = (ThreadPool *)calloc(1, sizeof(*pool));
    if (!pool) {
        perror("calloc");
        return NULL;
    }
    pool->cap = (queue_cap > 0 ? (size_t)queue_cap : 1024); // default bound
    pthread_mutex_init(&pool->m, NULL);
    pthread_cond_init(&pool->cv, NULL);
    pool->threads = (pthread_t *)calloc((size_t)num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        perror("calloc threads");
        pthread_mutex_destroy(&pool->m);
        pthread_cond_destroy(&pool->cv);
        free(pool);
        return NULL;
    }
    pool->num_threads = num_threads;
    // [ANCHOR:TP_CREATE_SPAWN]
    for (int i = 0; i < num_threads; ++i) {
        if (pthread_create(&pool->threads[i], NULL, worker_main, pool) != 0) {
            perror("pthread_create");
            pool->shutting_down = 1;
            pthread_cond_broadcast(&pool->cv);
            for (int j = 0; j < i; ++j) pthread_join(pool->threads[j], NULL);
            free(pool->threads);
            pthread_mutex_destroy(&pool->m);
            pthread_cond_destroy(&pool->cv);
            free(pool);
            return NULL;
        }
    }
    return pool;
}

// Submit a job; if the queue is full, return non-zero so caller can apply backpressure
// VN: Nếu queue đầy, trả lỗi để phía accept đóng/giảm tải thay vì nhận vô hạn.
int threadpool_submit(ThreadPool *pool, job_fn fn, void *arg) {
    if (!pool || !fn) return -1;
    Job *job = (Job *)malloc(sizeof(Job));
    if (!job) return -1;
    job->fn = fn;
    job->arg = arg;
    job->next = NULL;

    pthread_mutex_lock(&pool->m);
    // [ANCHOR:TP_SUBMIT_BACKPRESSURE]
    // EN: Backpressure — refuse when queue is full (non-blocking submit)
    // VN: Chống quá tải — từ chối khi hàng đợi đã đầy (không chặn tại đây)
    if (pool->size >= pool->cap) {
        pthread_mutex_unlock(&pool->m);
        free(job);
        return -1; // queue full
    }
    if (pool->tail) pool->tail->next = job; else pool->head = job;
    pool->tail = job;
    pool->size++;
    pthread_cond_signal(&pool->cv);
    pthread_mutex_unlock(&pool->m);
    return 0;
}

// Graceful shutdown: wait for workers, free queue and resources
// VN: Ra tín hiệu dừng, chờ worker kết thúc, giải phóng toàn bộ tài nguyên.
void threadpool_destroy(ThreadPool *pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->m);
    pool->shutting_down = 1;
    // [ANCHOR:TP_DESTROY_BROADCAST_JOIN]
    // EN: Wake all workers so they can observe shutdown and exit
    // VN: Đánh thức tất cả worker để thấy cờ shutdown và thoát êm
    pthread_cond_broadcast(&pool->cv);
    pthread_mutex_unlock(&pool->m);
    for (int i = 0; i < pool->num_threads; ++i) pthread_join(pool->threads[i], NULL);

    // EN: Free remaining jobs, if any (dropped on shutdown)
    // VN: Giải phóng các job còn lại (nếu có) khi tắt dịch vụ
    Job *j = pool->head;
    while (j) { Job *n = j->next; free(j); j = n; }

    free(pool->threads);
    pthread_mutex_destroy(&pool->m);
    pthread_cond_destroy(&pool->cv);
    free(pool);
}
