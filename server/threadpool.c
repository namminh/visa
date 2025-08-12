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
        while (!pool->shutting_down && pool->size == 0) {
            pthread_cond_wait(&pool->cv, &pool->m);
        }
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
    pthread_cond_broadcast(&pool->cv);
    pthread_mutex_unlock(&pool->m);
    for (int i = 0; i < pool->num_threads; ++i) pthread_join(pool->threads[i], NULL);

    // free remaining jobs, if any
    Job *j = pool->head;
    while (j) { Job *n = j->next; free(j); j = n; }

    free(pool->threads);
    pthread_mutex_destroy(&pool->m);
    pthread_cond_destroy(&pool->cv);
    free(pool);
}
