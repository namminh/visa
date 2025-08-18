#include "clearing_participant.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "metrics.h"

/**
 * Simulate HTTP request to external clearing service
 * In production, this would use libcurl or similar
 */
static int simulate_clearing_request(const char *url, 
                                   const char *method,
                                   const char *payload,
                                   char *response,
                                   size_t response_size,
                                   int timeout_seconds) {
    
    // Simulate network delay
    usleep(50000 + (rand() % 100000));  // 50-150ms
    
    // Simulate occasional failures (5% failure rate)
    if (rand() % 100 < 5) {
        snprintf(response, response_size, "{\"status\":\"ERROR\",\"reason\":\"network_timeout\"}");
        return -1;
    }
    
    // Simulate successful response based on method
    if (strcmp(method, "POST") == 0) {
        if (strstr(payload, "prepare")) {
            snprintf(response, response_size, 
                    "{\"status\":\"OK\",\"action\":\"hold_placed\",\"auth_code\":\"AUTH%d\"}", 
                    rand() % 999999);
        } else if (strstr(payload, "commit")) {
            snprintf(response, response_size, 
                    "{\"status\":\"OK\",\"action\":\"settled\",\"settlement_id\":\"STL%d\"}", 
                    rand() % 999999);
        } else if (strstr(payload, "abort")) {
            snprintf(response, response_size, 
                    "{\"status\":\"OK\",\"action\":\"hold_released\"}");
        }
    }
    
    return 0;
}

// --- Simple global circuit breaker for the clearing service (process-wide) ---
typedef struct {
    pthread_mutex_t mu;
    int failure_count;
    time_t window_start;
    int open;            // 0=closed, 1=open
    time_t opened_at;
    // Tunables
    int window_seconds;      // CLEARING_CB_WINDOW (default 30)
    int failure_threshold;   // CLEARING_CB_FAILS (default 5)
    int open_seconds;        // CLEARING_CB_OPEN_SECS (default 20)
    int max_retries;         // CLEARING_RETRY_MAX (default 2)
    int req_timeout_seconds; // CLEARING_TIMEOUT (default from ctx or 30)
} CircuitBreaker;

static CircuitBreaker g_cb = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .failure_count = 0,
    .window_start = 0,
    .open = 0,
    .opened_at = 0,
    .window_seconds = 30,
    .failure_threshold = 5,
    .open_seconds = 20,
    .max_retries = 2,
    .req_timeout_seconds = 30,
};

static int env_get_int(const char *name, int defv) {
    const char *s = getenv(name);
    if (!s || !*s) return defv;
    int v = atoi(s);
    return v > 0 ? v : defv;
}

static void cb_load_env_defaults(void) {
    static int loaded = 0;
    if (loaded) return;
    pthread_mutex_lock(&g_cb.mu);
    if (!loaded) {
        g_cb.window_seconds = env_get_int("CLEARING_CB_WINDOW", g_cb.window_seconds);
        g_cb.failure_threshold = env_get_int("CLEARING_CB_FAILS", g_cb.failure_threshold);
        g_cb.open_seconds = env_get_int("CLEARING_CB_OPEN_SECS", g_cb.open_seconds);
        g_cb.max_retries = env_get_int("CLEARING_RETRY_MAX", g_cb.max_retries);
        g_cb.req_timeout_seconds = env_get_int("CLEARING_TIMEOUT", g_cb.req_timeout_seconds);
        loaded = 1;
    }
    pthread_mutex_unlock(&g_cb.mu);
}

static int cb_should_short_circuit(void) {
    time_t now = time(NULL);
    pthread_mutex_lock(&g_cb.mu);
    if (g_cb.open) {
        if (now - g_cb.opened_at < g_cb.open_seconds) {
            pthread_mutex_unlock(&g_cb.mu);
            return 1; // still open
        }
        // half-open: allow a trial
        g_cb.open = 0;
        g_cb.failure_count = 0;
        g_cb.window_start = now;
    }
    // maintain window
    if (g_cb.window_start == 0 || (now - g_cb.window_start) > g_cb.window_seconds) {
        g_cb.window_start = now;
        g_cb.failure_count = 0;
    }
    pthread_mutex_unlock(&g_cb.mu);
    return 0;
}

static void cb_on_result(int ok) {
    time_t now = time(NULL);
    pthread_mutex_lock(&g_cb.mu);
    if (ok) {
        // Success closes/refreshes window
        g_cb.failure_count = 0;
        if (g_cb.window_start == 0) g_cb.window_start = now;
    } else {
        if (g_cb.window_start == 0 || (now - g_cb.window_start) > g_cb.window_seconds) {
            g_cb.window_start = now;
            g_cb.failure_count = 0;
        }
        g_cb.failure_count++;
        if (!g_cb.open && g_cb.failure_count >= g_cb.failure_threshold) {
            g_cb.open = 1;
            g_cb.opened_at = now;
        }
    }
    pthread_mutex_unlock(&g_cb.mu);
}

ClearingParticipantContext *clearing_participant_init(const char *service_url, int timeout_seconds) {
    ClearingParticipantContext *ctx = malloc(sizeof(ClearingParticipantContext));
    if (!ctx) return NULL;
    
    if (service_url) {
        strncpy(ctx->service_url, service_url, sizeof(ctx->service_url) - 1);
        ctx->service_url[sizeof(ctx->service_url) - 1] = '\0';
    } else {
        strcpy(ctx->service_url, "http://clearing.example.com/api");
    }
    
    cb_load_env_defaults();
    int def_timeout = timeout_seconds > 0 ? timeout_seconds : 30;
    int env_timeout = env_get_int("CLEARING_TIMEOUT", def_timeout);
    ctx->timeout_seconds = env_timeout > 0 ? env_timeout : def_timeout;
    ctx->has_hold = false;
    memset(ctx->current_txn_id, 0, sizeof(ctx->current_txn_id));
    memset(ctx->pan_masked, 0, sizeof(ctx->pan_masked));
    memset(ctx->amount, 0, sizeof(ctx->amount));
    memset(ctx->merchant_id, 0, sizeof(ctx->merchant_id));
    
    srand((unsigned int)time(NULL));  // Initialize random seed for simulation
    
    return ctx;
}

void clearing_participant_destroy(ClearingParticipantContext *ctx) {
    if (!ctx) return;
    
    // Cleanup any outstanding holds
    if (ctx->has_hold) {
        clearing_participant_abort(ctx, ctx->current_txn_id);
    }
    
    free(ctx);
}

int clearing_participant_set_transaction(ClearingParticipantContext *ctx,
                                       const char *txn_id,
                                       const char *pan_masked,
                                       const char *amount,
                                       const char *merchant_id) {
    if (!ctx || !txn_id || !pan_masked || !amount) return -1;
    
    strncpy(ctx->current_txn_id, txn_id, MAX_TRANSACTION_ID_LEN - 1);
    ctx->current_txn_id[MAX_TRANSACTION_ID_LEN - 1] = '\0';
    
    strncpy(ctx->pan_masked, pan_masked, sizeof(ctx->pan_masked) - 1);
    ctx->pan_masked[sizeof(ctx->pan_masked) - 1] = '\0';
    
    strncpy(ctx->amount, amount, sizeof(ctx->amount) - 1);
    ctx->amount[sizeof(ctx->amount) - 1] = '\0';
    
    if (merchant_id) {
        strncpy(ctx->merchant_id, merchant_id, sizeof(ctx->merchant_id) - 1);
        ctx->merchant_id[sizeof(ctx->merchant_id) - 1] = '\0';
    } else {
        strcpy(ctx->merchant_id, "MERCHANT001");
    }
    
    log_message_json("INFO", "clearing_participant", txn_id, 
                    "Transaction details set", -1);
    return 0;
}

int clearing_participant_prepare(void *context, const char *txn_id) {
    ClearingParticipantContext *ctx = (ClearingParticipantContext *)context;
    if (!ctx || !txn_id) return -1;
    
    if (strcmp(ctx->current_txn_id, txn_id) != 0) {
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "Transaction ID mismatch", -1);
        return -1;
    }
    
    // Prepare request payload
    char payload[512];
    snprintf(payload, sizeof(payload),
            "{"
            "\"action\":\"prepare\","
            "\"transaction_id\":\"%s\","
            "\"pan\":\"%s\","
            "\"amount\":\"%s\","
            "\"merchant_id\":\"%s\""
            "}",
            txn_id, ctx->pan_masked, ctx->amount, ctx->merchant_id);
    
    cb_load_env_defaults();
    if (cb_should_short_circuit()) {
        log_message_json("WARN", "clearing_participant", txn_id, "Circuit open: short-circuit PREPARE", -1);
        metrics_inc_cb_short_circuit();
        return -1;
    }

    char response[256];
    int result = -1;
    for (int attempt = 0; attempt <= g_cb.max_retries; attempt++) {
        result = simulate_clearing_request(ctx->service_url, "POST", payload,
                                           response, sizeof(response),
                                           ctx->timeout_seconds);
        if (result == 0) break;
        // exponential backoff: 100ms, 200ms, 400ms...
        int backoff_ms = 100 * (1 << attempt);
        usleep((useconds_t)backoff_ms * 1000);
    }
    cb_on_result(result == 0);
    if (result != 0) {
        log_message_json("ERROR", "clearing_participant", txn_id, "Clearing PREPARE failed", -1);
        return -1;
    }
    
    // Parse response (simplified)
    if (strstr(response, "\"status\":\"OK\"")) {
        ctx->has_hold = true;
        log_message_json("INFO", "clearing_participant", txn_id, 
                        "Authorization hold placed", -1);
        return 0;
    } else {
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "Clearing service declined", -1);
        return -1;
    }
}

int clearing_participant_commit(void *context, const char *txn_id) {
    ClearingParticipantContext *ctx = (ClearingParticipantContext *)context;
    if (!ctx || !txn_id) return -1;
    
    if (strcmp(ctx->current_txn_id, txn_id) != 0 || !ctx->has_hold) {
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "No prepared transaction to commit", -1);
        return -1;
    }
    
    // Commit request payload
    char payload[512];
    snprintf(payload, sizeof(payload),
            "{"
            "\"action\":\"commit\","
            "\"transaction_id\":\"%s\","
            "\"pan\":\"%s\","
            "\"amount\":\"%s\","
            "\"merchant_id\":\"%s\""
            "}",
            txn_id, ctx->pan_masked, ctx->amount, ctx->merchant_id);
    
    cb_load_env_defaults();
    if (cb_should_short_circuit()) {
        log_message_json("WARN", "clearing_participant", txn_id, "Circuit open: short-circuit COMMIT", -1);
        metrics_inc_cb_short_circuit();
        return -1;
    }
    char response[256];
    int result = -1;
    for (int attempt = 0; attempt <= g_cb.max_retries; attempt++) {
        result = simulate_clearing_request(ctx->service_url, "POST", payload,
                                           response, sizeof(response),
                                           ctx->timeout_seconds);
        if (result == 0) break;
        int backoff_ms = 100 * (1 << attempt);
        usleep((useconds_t)backoff_ms * 1000);
    }
    cb_on_result(result == 0);
    if (result != 0) {
        log_message_json("ERROR", "clearing_participant", txn_id, "Clearing COMMIT failed", -1);
        return -1;
    }
    
    // Parse response (simplified)
    if (strstr(response, "\"status\":\"OK\"")) {
        ctx->has_hold = false;
        memset(ctx->current_txn_id, 0, sizeof(ctx->current_txn_id));
        log_message_json("INFO", "clearing_participant", txn_id, 
                        "Transaction settled", -1);
        return 0;
    } else {
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "Clearing service commit failed", -1);
        return -1;
    }
}

int clearing_participant_abort(void *context, const char *txn_id) {
    ClearingParticipantContext *ctx = (ClearingParticipantContext *)context;
    if (!ctx || !txn_id) return -1;
    
    // Proceed even if we think no hold exists (idempotent best-effort abort)
    if (!ctx->has_hold || strcmp(ctx->current_txn_id, txn_id) != 0) {
        log_message_json("INFO", "clearing_participant", txn_id,
                        "No local hold; sending idempotent abort", -1);
    }
    
    // Abort request payload
    char payload[512];
    snprintf(payload, sizeof(payload),
            "{"
            "\"action\":\"abort\","
            "\"transaction_id\":\"%s\","
            "\"pan\":\"%s\","
            "\"amount\":\"%s\","
            "\"merchant_id\":\"%s\""
            "}",
            txn_id, ctx->pan_masked, ctx->amount, ctx->merchant_id);
    
    char response[256];
    int result = simulate_clearing_request(ctx->service_url, "POST", payload,
                                           response, sizeof(response),
                                           ctx->timeout_seconds);
    
    // Best effort - don't fail if abort fails (idempotent)
    if (result == 0 && strstr(response, "\"status\":\"OK\"")) {
        log_message_json("INFO", "clearing_participant", txn_id, 
                        "Authorization hold released", -1);
    } else {
        log_message_json("WARN", "clearing_participant", txn_id, 
                        "Hold release failed (may timeout naturally)", -1);
    }
    
    // Always clear our state
    ctx->has_hold = false;
    memset(ctx->current_txn_id, 0, sizeof(ctx->current_txn_id));
    
    return 0;  // Always return success for abort
}
