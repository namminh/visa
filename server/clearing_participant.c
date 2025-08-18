#include "clearing_participant.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

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

ClearingParticipantContext *clearing_participant_init(const char *service_url, int timeout_seconds) {
    ClearingParticipantContext *ctx = malloc(sizeof(ClearingParticipantContext));
    if (!ctx) return NULL;
    
    if (service_url) {
        strncpy(ctx->service_url, service_url, sizeof(ctx->service_url) - 1);
        ctx->service_url[sizeof(ctx->service_url) - 1] = '\0';
    } else {
        strcpy(ctx->service_url, "http://clearing.example.com/api");
    }
    
    ctx->timeout_seconds = timeout_seconds > 0 ? timeout_seconds : 30;
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
    
    char response[256];
    int result = simulate_clearing_request(ctx->service_url, "POST", payload, 
                                         response, sizeof(response), 
                                         ctx->timeout_seconds);
    
    if (result != 0) {
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "Clearing PREPARE failed", -1);
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
    
    char response[256];
    int result = simulate_clearing_request(ctx->service_url, "POST", payload, 
                                         response, sizeof(response), 
                                         ctx->timeout_seconds);
    
    if (result != 0) {
        log_message_json("ERROR", "clearing_participant", txn_id, 
                        "Clearing COMMIT failed", -1);
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
    
    // Only proceed if we have a hold to release
    if (!ctx->has_hold || strcmp(ctx->current_txn_id, txn_id) != 0) {
        log_message_json("INFO", "clearing_participant", txn_id, 
                        "No hold to release", -1);
        return 0;  // Not an error - idempotent operation
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