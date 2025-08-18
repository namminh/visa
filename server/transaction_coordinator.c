#include "transaction_coordinator.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define MAX_ACTIVE_TRANSACTIONS 1024
#define DEFAULT_PREPARE_TIMEOUT 30  // seconds
#define DEFAULT_COMMIT_TIMEOUT 30   // seconds

struct TransactionCoordinator {
    pthread_mutex_t mutex;
    Transaction *active_transactions[MAX_ACTIVE_TRANSACTIONS];
    size_t active_count;
    
    // Transaction log for persistence/recovery
    FILE *txn_log;
};

static const char *txn_state_strings[] = {
    "INIT", "PREPARING", "PREPARED", "COMMITTING", 
    "COMMITTED", "ABORTING", "ABORTED"
};

static const char *participant_state_strings[] = {
    "INIT", "PREPARED", "COMMITTED", "ABORTED", "FAILED"
};

/**
 * Log transaction state changes for recovery
 */
static void log_transaction_state(TransactionCoordinator *coordinator, 
                                const Transaction *txn, 
                                const char *action) {
    if (!coordinator->txn_log) return;
    
    time_t now = time(NULL);
    fprintf(coordinator->txn_log, 
            "%ld|%s|%s|%s\n", 
            now, txn->transaction_id, 
            txn_state_to_string(txn->state), 
            action);
    fflush(coordinator->txn_log);
}

/**
 * Find transaction by ID in active list
 */
static Transaction *find_transaction(TransactionCoordinator *coordinator, 
                                   const char *txn_id) {
    for (size_t i = 0; i < coordinator->active_count; i++) {
        if (coordinator->active_transactions[i] && 
            strcmp(coordinator->active_transactions[i]->transaction_id, txn_id) == 0) {
            return coordinator->active_transactions[i];
        }
    }
    return NULL;
}

/**
 * Remove transaction from active list
 */
static void remove_transaction(TransactionCoordinator *coordinator, 
                             const char *txn_id) {
    for (size_t i = 0; i < coordinator->active_count; i++) {
        if (coordinator->active_transactions[i] && 
            strcmp(coordinator->active_transactions[i]->transaction_id, txn_id) == 0) {
            
            free(coordinator->active_transactions[i]);
            
            // Shift remaining transactions down
            for (size_t j = i; j < coordinator->active_count - 1; j++) {
                coordinator->active_transactions[j] = coordinator->active_transactions[j + 1];
            }
            coordinator->active_count--;
            break;
        }
    }
}

TransactionCoordinator *txn_coordinator_init(void) {
    TransactionCoordinator *coordinator = malloc(sizeof(TransactionCoordinator));
    if (!coordinator) return NULL;
    
    if (pthread_mutex_init(&coordinator->mutex, NULL) != 0) {
        free(coordinator);
        return NULL;
    }
    
    memset(coordinator->active_transactions, 0, sizeof(coordinator->active_transactions));
    coordinator->active_count = 0;
    
    // Open transaction log for persistence
    coordinator->txn_log = fopen("logs/transactions.log", "a");
    if (!coordinator->txn_log) {
        log_message_json("WARN", "txn_coordinator", NULL, "Failed to open transaction log", -1);
    }
    
    log_message_json("INFO", "txn_coordinator", NULL, "Initialized", -1);
    return coordinator;
}

void txn_coordinator_destroy(TransactionCoordinator *coordinator) {
    if (!coordinator) return;
    
    pthread_mutex_lock(&coordinator->mutex);
    
    // Clean up active transactions
    for (size_t i = 0; i < coordinator->active_count; i++) {
        if (coordinator->active_transactions[i]) {
            free(coordinator->active_transactions[i]);
        }
    }
    
    if (coordinator->txn_log) {
        fclose(coordinator->txn_log);
    }
    
    pthread_mutex_unlock(&coordinator->mutex);
    pthread_mutex_destroy(&coordinator->mutex);
    free(coordinator);
}

Transaction *txn_begin(TransactionCoordinator *coordinator, const char *txn_id) {
    if (!coordinator || !txn_id) return NULL;
    
    pthread_mutex_lock(&coordinator->mutex);
    
    // Check if transaction already exists
    if (find_transaction(coordinator, txn_id)) {
        pthread_mutex_unlock(&coordinator->mutex);
        log_message_json("ERROR", "txn_coordinator", txn_id, "Transaction already exists", -1);
        return NULL;
    }
    
    // Check capacity
    if (coordinator->active_count >= MAX_ACTIVE_TRANSACTIONS) {
        pthread_mutex_unlock(&coordinator->mutex);
        log_message_json("ERROR", "txn_coordinator", txn_id, "Too many active transactions", -1);
        return NULL;
    }
    
    // Create new transaction
    Transaction *txn = malloc(sizeof(Transaction));
    if (!txn) {
        pthread_mutex_unlock(&coordinator->mutex);
        return NULL;
    }
    
    strncpy(txn->transaction_id, txn_id, MAX_TRANSACTION_ID_LEN - 1);
    txn->transaction_id[MAX_TRANSACTION_ID_LEN - 1] = '\0';
    txn->state = TXN_INIT;
    txn->participant_count = 0;
    txn->start_time = time(NULL);
    txn->prepare_timeout = txn->start_time + DEFAULT_PREPARE_TIMEOUT;
    txn->commit_timeout = txn->start_time + DEFAULT_COMMIT_TIMEOUT;
    
    memset(txn->participants, 0, sizeof(txn->participants));
    
    // Add to active list
    coordinator->active_transactions[coordinator->active_count++] = txn;
    
    log_transaction_state(coordinator, txn, "BEGIN");
    
    pthread_mutex_unlock(&coordinator->mutex);
    
    log_message_json("INFO", "txn_coordinator", txn_id, "Transaction started", -1);
    return txn;
}

int txn_register_participant(Transaction *txn,
                           const char *name,
                           void *context,
                           int (*prepare)(void *context, const char *txn_id),
                           int (*commit)(void *context, const char *txn_id),
                           int (*abort)(void *context, const char *txn_id)) {
    
    if (!txn || !name || !prepare || !commit || !abort) return -1;
    
    if (txn->participant_count >= MAX_PARTICIPANTS) {
        log_message_json("ERROR", "txn_coordinator", txn->transaction_id, 
                        "Too many participants", -1);
        return -1;
    }
    
    Participant *p = &txn->participants[txn->participant_count];
    strncpy(p->name, name, MAX_PARTICIPANT_NAME_LEN - 1);
    p->name[MAX_PARTICIPANT_NAME_LEN - 1] = '\0';
    p->state = PARTICIPANT_INIT;
    p->context = context;
    p->prepare = prepare;
    p->commit = commit;
    p->abort = abort;
    
    txn->participant_count++;
    
    log_message_json("INFO", "txn_coordinator", txn->transaction_id, 
                    "Participant registered", -1);
    return 0;
}

int txn_commit(TransactionCoordinator *coordinator, Transaction *txn) {
    if (!coordinator || !txn) return -1;
    
    pthread_mutex_lock(&coordinator->mutex);
    
    const char *txn_id = txn->transaction_id;
    
    // Phase 1: PREPARE
    txn->state = TXN_PREPARING;
    log_transaction_state(coordinator, txn, "PREPARE_START");
    
    log_message_json("INFO", "txn_coordinator", txn_id, "Starting PREPARE phase", -1);
    
    bool all_prepared = true;
    for (size_t i = 0; i < txn->participant_count; i++) {
        Participant *p = &txn->participants[i];
        
        log_message_json("INFO", "txn_coordinator", txn_id, "Preparing participant", -1);
        
        int result = p->prepare(p->context, txn_id);
        if (result == 0) {
            p->state = PARTICIPANT_PREPARED;
            log_message_json("INFO", "txn_coordinator", txn_id, "Participant prepared", -1);
        } else {
            p->state = PARTICIPANT_FAILED;
            all_prepared = false;
            log_message_json("ERROR", "txn_coordinator", txn_id, "Participant prepare failed", -1);
            break;  // Fail fast
        }
    }
    
    if (all_prepared) {
        txn->state = TXN_PREPARED;
        log_transaction_state(coordinator, txn, "PREPARED");
        
        // Phase 2: COMMIT
        txn->state = TXN_COMMITTING;
        log_transaction_state(coordinator, txn, "COMMIT_START");
        
        log_message_json("INFO", "txn_coordinator", txn_id, "Starting COMMIT phase", -1);
        
        bool commit_success = true;
        for (size_t i = 0; i < txn->participant_count; i++) {
            Participant *p = &txn->participants[i];
            
            if (p->state == PARTICIPANT_PREPARED) {
                int result = p->commit(p->context, txn_id);
                if (result == 0) {
                    p->state = PARTICIPANT_COMMITTED;
                    log_message_json("INFO", "txn_coordinator", txn_id, "Participant committed", -1);
                } else {
                    p->state = PARTICIPANT_FAILED;
                    commit_success = false;
                    log_message_json("ERROR", "txn_coordinator", txn_id, "Participant commit failed", -1);
                    // Note: In real 2PC, this is a serious problem requiring manual intervention
                }
            }
        }
        
        if (commit_success) {
            txn->state = TXN_COMMITTED;
            log_transaction_state(coordinator, txn, "COMMITTED");
            log_message_json("INFO", "txn_coordinator", txn_id, "Transaction committed successfully", -1);
            
            remove_transaction(coordinator, txn_id);
            pthread_mutex_unlock(&coordinator->mutex);
            return 0;
        }
    }
    
    // ABORT path
    txn->state = TXN_ABORTING;
    log_transaction_state(coordinator, txn, "ABORT_START");
    
    log_message_json("WARN", "txn_coordinator", txn_id, "Aborting transaction", -1);
    
    for (size_t i = 0; i < txn->participant_count; i++) {
        Participant *p = &txn->participants[i];
        
        if (p->state == PARTICIPANT_PREPARED || p->state == PARTICIPANT_FAILED) {
            p->abort(p->context, txn_id);
            p->state = PARTICIPANT_ABORTED;
            log_message_json("INFO", "txn_coordinator", txn_id, "Participant aborted", -1);
        }
    }
    
    txn->state = TXN_ABORTED;
    log_transaction_state(coordinator, txn, "ABORTED");
    
    remove_transaction(coordinator, txn_id);
    pthread_mutex_unlock(&coordinator->mutex);
    
    log_message_json("INFO", "txn_coordinator", txn_id, "Transaction aborted", -1);
    return -1;
}

void txn_abort(TransactionCoordinator *coordinator, Transaction *txn) {
    if (!coordinator || !txn) return;
    
    pthread_mutex_lock(&coordinator->mutex);
    
    const char *txn_id = txn->transaction_id;
    
    txn->state = TXN_ABORTING;
    log_transaction_state(coordinator, txn, "ABORT_EXPLICIT");
    
    log_message_json("INFO", "txn_coordinator", txn_id, "Explicitly aborting transaction", -1);
    
    for (size_t i = 0; i < txn->participant_count; i++) {
        Participant *p = &txn->participants[i];
        p->abort(p->context, txn_id);
        p->state = PARTICIPANT_ABORTED;
    }
    
    txn->state = TXN_ABORTED;
    log_transaction_state(coordinator, txn, "ABORTED");
    
    remove_transaction(coordinator, txn_id);
    pthread_mutex_unlock(&coordinator->mutex);
}

Transaction *txn_get_by_id(TransactionCoordinator *coordinator, const char *txn_id) {
    if (!coordinator || !txn_id) return NULL;
    
    pthread_mutex_lock(&coordinator->mutex);
    Transaction *txn = find_transaction(coordinator, txn_id);
    pthread_mutex_unlock(&coordinator->mutex);
    
    return txn;
}

int txn_recover(TransactionCoordinator *coordinator) {
    if (!coordinator) return 0;
    
    // In a real implementation, this would:
    // 1. Read transaction log
    // 2. Find incomplete transactions
    // 3. Query participants for their state
    // 4. Decide whether to commit or abort each transaction
    
    log_message_json("INFO", "txn_coordinator", NULL, "Recovery not implemented", -1);
    return 0;
}

const char *txn_state_to_string(TransactionState state) {
    if (state >= 0 && state < sizeof(txn_state_strings)/sizeof(txn_state_strings[0])) {
        return txn_state_strings[state];
    }
    return "UNKNOWN";
}

const char *participant_state_to_string(ParticipantState state) {
    if (state >= 0 && state < sizeof(participant_state_strings)/sizeof(participant_state_strings[0])) {
        return participant_state_strings[state];
    }
    return "UNKNOWN";
}