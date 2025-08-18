#pragma once

#include "db.h"
#include "transaction_coordinator.h"

/**
 * Database participant for 2-phase commit
 * 
 * Implements the participant interface for PostgreSQL transactions
 * Uses PostgreSQL's PREPARE TRANSACTION / COMMIT PREPARED functionality
 */

typedef struct {
    DBConnection *dbc;
    char current_txn_id[MAX_TRANSACTION_ID_LEN];
    bool in_transaction;
} DBParticipantContext;

/**
 * Initialize database participant context
 */
DBParticipantContext *db_participant_init(DBConnection *dbc);

/**
 * Cleanup database participant context
 */
void db_participant_destroy(DBParticipantContext *ctx);

/**
 * Begin a database transaction for the given transaction ID
 * This should be called before any DB operations within the transaction
 */
int db_participant_begin(DBParticipantContext *ctx, const char *txn_id);

/**
 * Execute a transactional database operation
 * (wrapper around existing db operations to ensure they're in the current transaction)
 */
int db_participant_insert_transaction(DBParticipantContext *ctx,
                                    const char *request_id,
                                    const char *pan_masked,
                                    const char *amount,
                                    const char *status,
                                    int *out_is_dup,
                                    char *out_status,
                                    size_t out_status_sz);

/**
 * 2PC Participant Interface Functions
 * These are called by the transaction coordinator
 */

/**
 * Phase 1: PREPARE
 * Issues PREPARE TRANSACTION to PostgreSQL
 */
int db_participant_prepare(void *context, const char *txn_id);

/**
 * Phase 2: COMMIT
 * Issues COMMIT PREPARED to PostgreSQL
 */
int db_participant_commit(void *context, const char *txn_id);

/**
 * ABORT
 * Issues ROLLBACK PREPARED to PostgreSQL (or ROLLBACK if not prepared)
 */
int db_participant_abort(void *context, const char *txn_id);