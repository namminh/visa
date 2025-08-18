#include "reversal.h"
#include "clearing_participant.h"
#include "log.h"
#include "metrics.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct ReversalTask {
    char txn_id[MAX_TRANSACTION_ID_LEN];
    char pan_masked[32];
    char amount[16];
    char merchant_id[32];
    int attempts;
    time_t next_at;
    struct ReversalTask *next;
} ReversalTask;

static pthread_t g_thread;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static ReversalTask *g_head = NULL;
static int g_shutdown = 0;

static int max_attempts(void) {
    const char *s = getenv("REVERSAL_MAX_ATTEMPTS");
    int v = s ? atoi(s) : 6; // ~ 6 attempts
    return v > 0 ? v : 6;
}

static int base_delay_ms(void) {
    const char *s = getenv("REVERSAL_BASE_DELAY_MS");
    int v = s ? atoi(s) : 250; // 250ms
    return v > 0 ? v : 250;
}

static void queue_push(ReversalTask *t) {
    t->next = NULL;
    if (!g_head) {
        g_head = t;
    } else {
        ReversalTask *p = g_head;
        while (p->next) p = p->next;
        p->next = t;
    }
}

static ReversalTask *queue_pop_ready(time_t now) {
    ReversalTask *prev = NULL, *cur = g_head;
    while (cur) {
        if (cur->next_at <= now) {
            if (prev) prev->next = cur->next; else g_head = cur->next;
            cur->next = NULL;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    return NULL;
}

static void *reversal_loop(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&g_mu);
        while (!g_shutdown && !g_head) {
            pthread_cond_wait(&g_cv, &g_mu);
        }
        if (g_shutdown && !g_head) {
            pthread_mutex_unlock(&g_mu);
            break;
        }
        time_t now = time(NULL);
        ReversalTask *task = queue_pop_ready(now);
        if (!task) {
            // sleep until next task or signal
            struct timespec ts; ts.tv_sec = now + 1; ts.tv_nsec = 0;
            pthread_cond_timedwait(&g_cv, &g_mu, &ts);
            pthread_mutex_unlock(&g_mu);
            continue;
        }
        pthread_mutex_unlock(&g_mu);

        // Perform best-effort abort/void on clearing
        ClearingParticipantContext *ctx = clearing_participant_init(NULL, 10);
        if (ctx) {
            (void)clearing_participant_set_transaction(ctx, task->txn_id,
                                                       task->pan_masked,
                                                       task->amount,
                                                       task->merchant_id);
            int rc = clearing_participant_abort(ctx, task->txn_id);
            clearing_participant_destroy(ctx);
            if (rc == 0) {
                log_message_json("INFO", "reversal", task->txn_id, "Reversal success", -1);
                metrics_inc_reversal_succeeded();
                free(task);
                continue;
            }
        }
        // schedule retry
        task->attempts++;
        if (task->attempts >= max_attempts()) {
            log_message_json("ERROR", "reversal", task->txn_id, "Reversal failed permanently", -1);
            metrics_inc_reversal_failed();
            free(task);
            continue;
        }
        int delay = base_delay_ms() * (1 << (task->attempts - 1));
        task->next_at = time(NULL) + (delay / 1000);
        pthread_mutex_lock(&g_mu);
        queue_push(task);
        pthread_mutex_unlock(&g_mu);
    }
    return NULL;
}

int reversal_init(void) {
    g_shutdown = 0;
    if (pthread_create(&g_thread, NULL, reversal_loop, NULL) != 0) {
        return -1;
    }
    return 0;
}

int reversal_enqueue(const char *txn_id,
                     const char *pan_masked,
                     const char *amount,
                     const char *merchant_id) {
    if (!txn_id || !*txn_id) return -1;
    ReversalTask *t = (ReversalTask *)malloc(sizeof(ReversalTask));
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    strncpy(t->txn_id, txn_id, sizeof(t->txn_id) - 1);
    if (pan_masked) strncpy(t->pan_masked, pan_masked, sizeof(t->pan_masked) - 1);
    if (amount) strncpy(t->amount, amount, sizeof(t->amount) - 1);
    if (merchant_id) strncpy(t->merchant_id, merchant_id, sizeof(t->merchant_id) - 1);
    t->attempts = 0;
    t->next_at = time(NULL);

    pthread_mutex_lock(&g_mu);
    queue_push(t);
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_mu);
    metrics_inc_reversal_enqueued();
    log_message_json("WARN", "reversal", txn_id, "Reversal enqueued", -1);
    return 0;
}

void reversal_shutdown(void) {
    pthread_mutex_lock(&g_mu);
    g_shutdown = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    pthread_join(g_thread, NULL);
    // free remaining tasks
    pthread_mutex_lock(&g_mu);
    ReversalTask *cur = g_head;
    while (cur) { ReversalTask *n = cur->next; free(cur); cur = n; }
    g_head = NULL;
    pthread_mutex_unlock(&g_mu);
}

