#pragma once

#include <stddef.h>

typedef struct ThreadPool ThreadPool;

/**
 * Job function signature. A job takes a void pointer to user data
 * and returns nothing. The user is responsible for freeing the data
 * if necessary.
 */
typedef void (*job_fn)(void *arg);
/* VN (Phỏng vấn – Thread pool API)
 * - job_fn: hàm công việc dạng void*(arg) → void (tự giải phóng arg nếu cần).
 * - threadpool_create(n, cap): tạo N worker và hàng đợi tối đa cap phần tử.
 * - threadpool_submit(pool, fn, arg): đẩy công việc vào queue (có thể bị từ chối nếu đầy).
 * - threadpool_destroy(pool): chờ worker kết thúc, giải phóng queue & mutex/cond.
 */

/**
 * Initialize a thread pool with a fixed number of worker threads.
 *
 * @param num_threads Number of worker threads
 * @return Pointer to a new ThreadPool or NULL on error
 */
ThreadPool *threadpool_create(int num_threads, int queue_cap);

/**
 * Submit a job to the thread pool for execution.
 *
 * @param pool The thread pool
 * @param fn Function pointer for the job
 * @param arg User data pointer passed to fn
 * @return 0 on success, non‑zero on error
 */
int threadpool_submit(ThreadPool *pool, job_fn fn, void *arg);

/**
 * Shut down the thread pool and free resources. All queued jobs
 * should finish executing before this returns.
 *
 * @param pool The thread pool
 */
void threadpool_destroy(ThreadPool *pool);
