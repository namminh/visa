#include "db_participant.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>

DBParticipantContext *db_participant_init(DBConnection *dbc) {
    if (!dbc) return NULL;
    
    DBParticipantContext *ctx = malloc(sizeof(DBParticipantContext));
    if (!ctx) return NULL;
    
    ctx->dbc = dbc;
    memset(ctx->current_txn_id, 0, sizeof(ctx->current_txn_id));
    ctx->in_transaction = false;
    
    return ctx;
}

void db_participant_destroy(DBParticipantContext *ctx) {
    if (!ctx) return;
    
    // Cleanup any ongoing transaction
    if (ctx->in_transaction) {
        db_participant_abort(ctx, ctx->current_txn_id);
    }
    
    free(ctx);
}

int db_participant_begin(DBParticipantContext *ctx, const char *txn_id) {
    if (!ctx || !txn_id) return -1;
    
    if (ctx->in_transaction) {
        log_message_json("ERROR", "db_participant", txn_id, 
                        "Already in transaction", -1);
        return -1;
    }
    
    // Start PostgreSQL transaction
    PGconn *conn = (PGconn *)ctx->dbc;  // Assuming DBConnection wraps PGconn
    PGresult *res = PQexec(conn, "BEGIN");
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        log_message_json("ERROR", "db_participant", txn_id, 
                        "Failed to begin transaction", -1);
        PQclear(res);
        return -1;
    }
    PQclear(res);
    
    strncpy(ctx->current_txn_id, txn_id, MAX_TRANSACTION_ID_LEN - 1);
    ctx->current_txn_id[MAX_TRANSACTION_ID_LEN - 1] = '\0';
    ctx->in_transaction = true;
    
    log_message_json("INFO", "db_participant", txn_id, "Transaction started", -1);
    return 0;
}

int db_participant_insert_transaction(DBParticipantContext *ctx,
                                    const char *request_id,
                                    const char *pan_masked,
                                    const char *amount,
                                    const char *status,
                                    int *out_is_dup,
                                    char *out_status,
                                    size_t out_status_sz) {
    
    if (!ctx || !ctx->in_transaction) {
        log_message_json("ERROR", "db_participant", ctx ? ctx->current_txn_id : NULL, 
                        "Not in transaction", -1);
        return -1;
    }
    
    // Use the existing db function but ensure we're in the current transaction
    // This assumes the underlying db functions respect the current transaction context
    return db_insert_or_get_by_reqid(ctx->dbc, request_id, pan_masked, amount, 
                                   status, out_is_dup, out_status, out_status_sz);
}

int db_participant_prepare(void *context, const char *txn_id) {
    DBParticipantContext *ctx = (DBParticipantContext *)context;
    if (!ctx || !txn_id) return -1;
    
    if (!ctx->in_transaction || strcmp(ctx->current_txn_id, txn_id) != 0) {
        log_message_json("ERROR", "db_participant", txn_id, 
                        "Transaction mismatch or not active", -1);
        return -1;
    }
    
    PGconn *conn = (PGconn *)ctx->dbc;
    
    // Create prepared transaction name (PostgreSQL requirement)
    char prepare_cmd[256];
    snprintf(prepare_cmd, sizeof(prepare_cmd), "PREPARE TRANSACTION 'visa_%s'", txn_id);
    
    PGresult *res = PQexec(conn, prepare_cmd);
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char *error = PQerrorMessage(conn);
        log_message_json("ERROR", "db_participant", txn_id, 
                        "PREPARE failed", -1);
        PQclear(res);
        return -1;
    }
    PQclear(res);
    
    // Transaction is now prepared, no longer "active" but not yet committed
    ctx->in_transaction = false;
    
    log_message_json("INFO", "db_participant", txn_id, "PREPARE successful", -1);
    return 0;
}

int db_participant_commit(void *context, const char *txn_id) {
    DBParticipantContext *ctx = (DBParticipantContext *)context;
    if (!ctx || !txn_id) return -1;
    
    PGconn *conn = (PGconn *)ctx->dbc;
    
    // Commit the prepared transaction
    char commit_cmd[256];
    snprintf(commit_cmd, sizeof(commit_cmd), "COMMIT PREPARED 'visa_%s'", txn_id);
    
    PGresult *res = PQexec(conn, commit_cmd);
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char *error = PQerrorMessage(conn);
        log_message_json("ERROR", "db_participant", txn_id, 
                        "COMMIT PREPARED failed", -1);
        PQclear(res);
        return -1;
    }
    PQclear(res);
    
    // Clear transaction state
    memset(ctx->current_txn_id, 0, sizeof(ctx->current_txn_id));
    
    log_message_json("INFO", "db_participant", txn_id, "COMMIT successful", -1);
    return 0;
}

int db_participant_abort(void *context, const char *txn_id) {
    DBParticipantContext *ctx = (DBParticipantContext *)context;
    if (!ctx || !txn_id) return -1;
    
    PGconn *conn = (PGconn *)ctx->dbc;
    PGresult *res;
    
    if (ctx->in_transaction && strcmp(ctx->current_txn_id, txn_id) == 0) {
        // Transaction not yet prepared, use normal ROLLBACK
        res = PQexec(conn, "ROLLBACK");
        ctx->in_transaction = false;
    } else {
        // Transaction was prepared, use ROLLBACK PREPARED
        char rollback_cmd[256];
        snprintf(rollback_cmd, sizeof(rollback_cmd), "ROLLBACK PREPARED 'visa_%s'", txn_id);
        res = PQexec(conn, rollback_cmd);
    }
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char *error = PQerrorMessage(conn);
        log_message_json("WARN", "db_participant", txn_id, 
                        "ROLLBACK failed (may be OK if already rolled back)", -1);
        PQclear(res);
        // Don't return error - rollback failures are often non-critical
    } else {
        PQclear(res);
        log_message_json("INFO", "db_participant", txn_id, "ROLLBACK successful", -1);
    }
    
    // Clear transaction state
    memset(ctx->current_txn_id, 0, sizeof(ctx->current_txn_id));
    
    return 0;
}