#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/**
 * 2-Phase Commit Transaction Coordinator
 * 
 * Manages distributed transactions across multiple participants:
 * - Database (PostgreSQL)
 * - Clearing System (external service)
 * - Future: Fraud Detection, Acquirer, etc.
 */

#define MAX_PARTICIPANTS 8
#define MAX_TRANSACTION_ID_LEN 64
#define MAX_PARTICIPANT_NAME_LEN 32

typedef enum {
    TXN_INIT,
    TXN_PREPARING,
    TXN_PREPARED,
    TXN_COMMITTING,
    TXN_COMMITTED,
    TXN_ABORTING,
    TXN_ABORTED
} TransactionState;

typedef enum {
    PARTICIPANT_INIT,
    PARTICIPANT_PREPARED,
    PARTICIPANT_COMMITTED,
    PARTICIPANT_ABORTED,
    PARTICIPANT_FAILED
} ParticipantState;

typedef struct {
    char name[MAX_PARTICIPANT_NAME_LEN];
    ParticipantState state;
    void *context;  // participant-specific context
    
    // Participant interface functions
    int (*prepare)(void *context, const char *txn_id);
    int (*commit)(void *context, const char *txn_id);
    int (*abort)(void *context, const char *txn_id);
} Participant;

typedef struct {
    char transaction_id[MAX_TRANSACTION_ID_LEN];
    TransactionState state;
    
    size_t participant_count;
    Participant participants[MAX_PARTICIPANTS];
    
    // Timestamps for timeout management
    time_t start_time;
    time_t prepare_timeout;
    time_t commit_timeout;
} Transaction;

typedef struct TransactionCoordinator TransactionCoordinator;

/**
 * Initialize the transaction coordinator
 */
TransactionCoordinator *txn_coordinator_init(void);

/**
 * Cleanup and free the coordinator
 */
void txn_coordinator_destroy(TransactionCoordinator *coordinator);

/**
 * Begin a new distributed transaction
 * 
 * @param coordinator The transaction coordinator
 * @param txn_id Unique transaction identifier
 * @return Transaction handle or NULL on failure
 */
Transaction *txn_begin(TransactionCoordinator *coordinator, const char *txn_id);

/**
 * Register a participant in the transaction
 * 
 * @param txn Transaction handle
 * @param name Participant name (e.g., "db", "clearing")
 * @param context Participant-specific context
 * @param prepare Function to prepare participant
 * @param commit Function to commit participant
 * @param abort Function to abort participant
 * @return 0 on success, -1 on failure
 */
int txn_register_participant(Transaction *txn,
                           const char *name,
                           void *context,
                           int (*prepare)(void *context, const char *txn_id),
                           int (*commit)(void *context, const char *txn_id),
                           int (*abort)(void *context, const char *txn_id));

/**
 * Execute 2-phase commit protocol
 * 
 * Phase 1: Send PREPARE to all participants
 * Phase 2: Send COMMIT if all prepared, else ABORT
 * 
 * @param coordinator The transaction coordinator
 * @param txn Transaction to commit
 * @return 0 on successful commit, -1 on abort
 */
int txn_commit(TransactionCoordinator *coordinator, Transaction *txn);

/**
 * Abort a transaction (can be called at any time)
 * 
 * @param coordinator The transaction coordinator
 * @param txn Transaction to abort
 */
void txn_abort(TransactionCoordinator *coordinator, Transaction *txn);

/**
 * Get transaction by ID (for recovery purposes)
 * 
 * @param coordinator The transaction coordinator
 * @param txn_id Transaction identifier
 * @return Transaction handle or NULL if not found
 */
Transaction *txn_get_by_id(TransactionCoordinator *coordinator, const char *txn_id);

/**
 * Recovery: Find and handle incomplete transactions
 * Called on startup to recover from crashes
 * 
 * @param coordinator The transaction coordinator
 * @return Number of transactions recovered
 */
int txn_recover(TransactionCoordinator *coordinator);

/**
 * Get transaction state as string (for logging/debugging)
 */
const char *txn_state_to_string(TransactionState state);

/**
 * Get participant state as string (for logging/debugging)
 */
const char *participant_state_to_string(ParticipantState state);