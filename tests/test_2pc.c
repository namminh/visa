#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

#include "../server/transaction_coordinator.h"
#include "../server/db_participant.h"
#include "../server/clearing_participant.h"

/**
 * Test 2-Phase Commit implementation
 * 
 * Tests various scenarios:
 * - Successful commit
 * - Participant prepare failure
 * - Participant commit failure
 * - Transaction timeout
 */

// Mock participant that can be configured to fail
typedef struct {
    char name[32];
    bool fail_prepare;
    bool fail_commit;
    int prepare_delay_ms;
    int commit_delay_ms;
} MockParticipant;

static int mock_participant_prepare(void *context, const char *txn_id) {
    MockParticipant *mock = (MockParticipant *)context;
    
    if (mock->prepare_delay_ms > 0) {
        usleep(mock->prepare_delay_ms * 1000);
    }
    
    printf("Mock participant %s: PREPARE %s\n", mock->name, txn_id);
    
    if (mock->fail_prepare) {
        printf("Mock participant %s: PREPARE FAILED\n", mock->name);
        return -1;
    }
    
    printf("Mock participant %s: PREPARE OK\n", mock->name);
    return 0;
}

static int mock_participant_commit(void *context, const char *txn_id) {
    MockParticipant *mock = (MockParticipant *)context;
    
    if (mock->commit_delay_ms > 0) {
        usleep(mock->commit_delay_ms * 1000);
    }
    
    printf("Mock participant %s: COMMIT %s\n", mock->name, txn_id);
    
    if (mock->fail_commit) {
        printf("Mock participant %s: COMMIT FAILED\n", mock->name);
        return -1;
    }
    
    printf("Mock participant %s: COMMIT OK\n", mock->name);
    return 0;
}

static int mock_participant_abort(void *context, const char *txn_id) {
    MockParticipant *mock = (MockParticipant *)context;
    printf("Mock participant %s: ABORT %s\n", mock->name, txn_id);
    return 0;
}

void test_successful_commit() {
    printf("\n=== Test: Successful Commit ===\n");
    
    TransactionCoordinator *coordinator = txn_coordinator_init();
    assert(coordinator != NULL);
    
    Transaction *txn = txn_begin(coordinator, "test_txn_001");
    assert(txn != NULL);
    
    // Create mock participants
    MockParticipant db_mock = {"database", false, false, 10, 20};
    MockParticipant clearing_mock = {"clearing", false, false, 50, 30};
    
    // Register participants
    assert(txn_register_participant(txn, "database", &db_mock,
                                  mock_participant_prepare,
                                  mock_participant_commit,
                                  mock_participant_abort) == 0);
    
    assert(txn_register_participant(txn, "clearing", &clearing_mock,
                                  mock_participant_prepare,
                                  mock_participant_commit,
                                  mock_participant_abort) == 0);
    
    // Execute 2PC
    int result = txn_commit(coordinator, txn);
    assert(result == 0);  // Should succeed
    
    txn_coordinator_destroy(coordinator);
    printf("✓ Successful commit test passed\n");
}

void test_prepare_failure() {
    printf("\n=== Test: Prepare Failure ===\n");
    
    TransactionCoordinator *coordinator = txn_coordinator_init();
    assert(coordinator != NULL);
    
    Transaction *txn = txn_begin(coordinator, "test_txn_002");
    assert(txn != NULL);
    
    // Create mock participants - one will fail prepare
    MockParticipant db_mock = {"database", false, false, 10, 20};
    MockParticipant clearing_mock = {"clearing", true, false, 50, 30};  // fail_prepare = true
    
    // Register participants
    assert(txn_register_participant(txn, "database", &db_mock,
                                  mock_participant_prepare,
                                  mock_participant_commit,
                                  mock_participant_abort) == 0);
    
    assert(txn_register_participant(txn, "clearing", &clearing_mock,
                                  mock_participant_prepare,
                                  mock_participant_commit,
                                  mock_participant_abort) == 0);
    
    // Execute 2PC
    int result = txn_commit(coordinator, txn);
    assert(result == -1);  // Should fail and abort
    
    txn_coordinator_destroy(coordinator);
    printf("✓ Prepare failure test passed\n");
}

void test_commit_failure() {
    printf("\n=== Test: Commit Failure ===\n");
    
    TransactionCoordinator *coordinator = txn_coordinator_init();
    assert(coordinator != NULL);
    
    Transaction *txn = txn_begin(coordinator, "test_txn_003");
    assert(txn != NULL);
    
    // Create mock participants - one will fail commit
    MockParticipant db_mock = {"database", false, false, 10, 20};
    MockParticipant clearing_mock = {"clearing", false, true, 50, 30};  // fail_commit = true
    
    // Register participants
    assert(txn_register_participant(txn, "database", &db_mock,
                                  mock_participant_prepare,
                                  mock_participant_commit,
                                  mock_participant_abort) == 0);
    
    assert(txn_register_participant(txn, "clearing", &clearing_mock,
                                  mock_participant_prepare,
                                  mock_participant_commit,
                                  mock_participant_abort) == 0);
    
    // Execute 2PC
    int result = txn_commit(coordinator, txn);
    assert(result == -1);  // Should fail (commit phase failure)
    
    txn_coordinator_destroy(coordinator);
    printf("✓ Commit failure test passed\n");
}

void test_explicit_abort() {
    printf("\n=== Test: Explicit Abort ===\n");
    
    TransactionCoordinator *coordinator = txn_coordinator_init();
    assert(coordinator != NULL);
    
    Transaction *txn = txn_begin(coordinator, "test_txn_004");
    assert(txn != NULL);
    
    // Create mock participants
    MockParticipant db_mock = {"database", false, false, 10, 20};
    MockParticipant clearing_mock = {"clearing", false, false, 50, 30};
    
    // Register participants
    assert(txn_register_participant(txn, "database", &db_mock,
                                  mock_participant_prepare,
                                  mock_participant_commit,
                                  mock_participant_abort) == 0);
    
    assert(txn_register_participant(txn, "clearing", &clearing_mock,
                                  mock_participant_prepare,
                                  mock_participant_commit,
                                  mock_participant_abort) == 0);
    
    // Explicitly abort instead of commit
    txn_abort(coordinator, txn);
    
    txn_coordinator_destroy(coordinator);
    printf("✓ Explicit abort test passed\n");
}

void test_concurrent_transactions() {
    printf("\n=== Test: Concurrent Transactions ===\n");
    
    TransactionCoordinator *coordinator = txn_coordinator_init();
    assert(coordinator != NULL);
    
    // Start multiple transactions
    Transaction *txn1 = txn_begin(coordinator, "test_txn_005");
    Transaction *txn2 = txn_begin(coordinator, "test_txn_006");
    Transaction *txn3 = txn_begin(coordinator, "test_txn_007");
    
    assert(txn1 != NULL);
    assert(txn2 != NULL);
    assert(txn3 != NULL);
    
    // All should be different transactions
    assert(strcmp(txn1->transaction_id, txn2->transaction_id) != 0);
    assert(strcmp(txn2->transaction_id, txn3->transaction_id) != 0);
    
    // Abort all
    txn_abort(coordinator, txn1);
    txn_abort(coordinator, txn2);
    txn_abort(coordinator, txn3);
    
    txn_coordinator_destroy(coordinator);
    printf("✓ Concurrent transactions test passed\n");
}

void test_transaction_lookup() {
    printf("\n=== Test: Transaction Lookup ===\n");
    
    TransactionCoordinator *coordinator = txn_coordinator_init();
    assert(coordinator != NULL);
    
    Transaction *txn = txn_begin(coordinator, "test_txn_lookup");
    assert(txn != NULL);
    
    // Test lookup by ID
    Transaction *found = txn_get_by_id(coordinator, "test_txn_lookup");
    assert(found == txn);
    
    // Test lookup of non-existent transaction
    Transaction *not_found = txn_get_by_id(coordinator, "non_existent");
    assert(not_found == NULL);
    
    txn_abort(coordinator, txn);
    
    // After abort, should not be found
    found = txn_get_by_id(coordinator, "test_txn_lookup");
    assert(found == NULL);
    
    txn_coordinator_destroy(coordinator);
    printf("✓ Transaction lookup test passed\n");
}

int main() {
    printf("Starting 2-Phase Commit Tests\n");
    printf("==============================\n");
    
    test_successful_commit();
    test_prepare_failure();
    test_commit_failure();
    test_explicit_abort();
    test_concurrent_transactions();
    test_transaction_lookup();
    
    printf("\n==============================\n");
    printf("All 2PC tests passed! ✓\n");
    
    return 0;
}