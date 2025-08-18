#pragma once

#include "transaction_coordinator.h"

/**
 * Clearing participant for 2-phase commit
 * 
 * Simulates integration with external clearing/settlement systems
 * In production, this would communicate with actual payment networks
 */

typedef struct {
    char service_url[256];
    int timeout_seconds;
    
    // State tracking for current transaction
    char current_txn_id[MAX_TRANSACTION_ID_LEN];
    bool has_hold;
    
    // Transaction details (stored during prepare phase)
    char pan_masked[32];
    char amount[16];
    char merchant_id[32];
} ClearingParticipantContext;

/**
 * Initialize clearing participant context
 */
ClearingParticipantContext *clearing_participant_init(const char *service_url, int timeout_seconds);

/**
 * Cleanup clearing participant context
 */
void clearing_participant_destroy(ClearingParticipantContext *ctx);

/**
 * Set transaction details for the clearing operation
 * Must be called before prepare
 */
int clearing_participant_set_transaction(ClearingParticipantContext *ctx,
                                       const char *txn_id,
                                       const char *pan_masked,
                                       const char *amount,
                                       const char *merchant_id);

/**
 * 2PC Participant Interface Functions
 */

/**
 * Phase 1: PREPARE
 * - Validate transaction with clearing system
 * - Place authorization hold on funds
 * - Reserve settlement capacity
 */
int clearing_participant_prepare(void *context, const char *txn_id);

/**
 * Phase 2: COMMIT
 * - Convert authorization hold to actual charge
 * - Submit for settlement batch
 * - Release reserved capacity
 */
int clearing_participant_commit(void *context, const char *txn_id);

/**
 * ABORT
 * - Release authorization hold
 * - Cancel settlement reservation
 * - Clean up transaction state
 */
int clearing_participant_abort(void *context, const char *txn_id);