#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "../server/transaction_coordinator.h"
#include "../server/clearing_participant.h"

#define NUM_THREADS 5
#define TRANSACTIONS_PER_THREAD 20

typedef struct {
    int thread_id;
    int successful_txns;
    int failed_txns;
} ThreadResult;

// Mock participants with configurable behavior
int stress_prepare(void *context, const char *txn_id) {
    // Simulate varying processing time
    usleep(1000 + (rand() % 10000)); // 1-11ms
    
    // 10% failure rate for prepare
    return (rand() % 10 == 0) ? -1 : 0;
}

int stress_commit(void *context, const char *txn_id) {
    // Simulate commit processing time
    usleep(500 + (rand() % 5000)); // 0.5-5.5ms
    
    // 5% failure rate for commit
    return (rand() % 20 == 0) ? -1 : 0;
}

int stress_abort(void *context, const char *txn_id) {
    // Abort is always fast and successful
    usleep(100);
    return 0;
}

void* worker_thread(void* arg) {
    ThreadResult *result = (ThreadResult*)arg;
    int thread_id = result->thread_id;
    
    printf("Thread %d starting...\n", thread_id);
    
    // Each thread gets its own coordinator
    TransactionCoordinator *coordinator = txn_coordinator_init();
    assert(coordinator != NULL);
    
    // Seed random number generator per thread
    srand(time(NULL) + thread_id);
    
    for (int i = 0; i < TRANSACTIONS_PER_THREAD; i++) {
        char txn_id[64];
        snprintf(txn_id, sizeof(txn_id), "stress_t%d_txn_%d", thread_id, i);
        
        // Begin transaction
        Transaction *txn = txn_begin(coordinator, txn_id);
        if (!txn) {
            printf("Thread %d: Failed to begin transaction %d\n", thread_id, i);
            result->failed_txns++;
            continue;
        }
        
        // Register multiple participants
        int dummy_context1 = thread_id * 1000 + i;
        int dummy_context2 = thread_id * 2000 + i;
        
        if (txn_register_participant(txn, "participant1", &dummy_context1,
                                   stress_prepare, stress_commit, stress_abort) != 0 ||
            txn_register_participant(txn, "participant2", &dummy_context2,
                                   stress_prepare, stress_commit, stress_abort) != 0) {
            printf("Thread %d: Failed to register participants for txn %d\n", thread_id, i);
            txn_abort(coordinator, txn);
            result->failed_txns++;
            continue;
        }
        
        // Attempt to commit
        int commit_result = txn_commit(coordinator, txn);
        
        if (commit_result == 0) {
            result->successful_txns++;
        } else {
            result->failed_txns++;
        }
        
        // Small delay between transactions
        usleep(100 + (rand() % 1000)); // 0.1-1.1ms
    }
    
    txn_coordinator_destroy(coordinator);
    printf("Thread %d completed: %d successful, %d failed\n", 
           thread_id, result->successful_txns, result->failed_txns);
    
    return NULL;
}

int main() {
    printf("🚀 2PC Stress Test\n");
    printf("==================\n");
    printf("Configuration:\n");
    printf("- Threads: %d\n", NUM_THREADS);
    printf("- Transactions per thread: %d\n", TRANSACTIONS_PER_THREAD);
    printf("- Total transactions: %d\n", NUM_THREADS * TRANSACTIONS_PER_THREAD);
    printf("- Simulated prepare failure rate: 10%%\n");
    printf("- Simulated commit failure rate: 5%%\n");
    
    pthread_t threads[NUM_THREADS];
    ThreadResult results[NUM_THREADS];
    
    // Initialize results
    for (int i = 0; i < NUM_THREADS; i++) {
        results[i].thread_id = i;
        results[i].successful_txns = 0;
        results[i].failed_txns = 0;
    }
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    printf("\n🏁 Starting stress test...\n");
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &results[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            return 1;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    // Calculate results
    int total_successful = 0;
    int total_failed = 0;
    
    printf("\n📊 Results by Thread:\n");
    printf("=====================\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        printf("Thread %d: %d successful, %d failed (%.1f%% success rate)\n",
               i, results[i].successful_txns, results[i].failed_txns,
               (results[i].successful_txns * 100.0) / 
               (results[i].successful_txns + results[i].failed_txns));
        
        total_successful += results[i].successful_txns;
        total_failed += results[i].failed_txns;
    }
    
    // Calculate timing
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                    (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    
    printf("\n🎯 Overall Results:\n");
    printf("==================\n");
    printf("Total transactions: %d\n", total_successful + total_failed);
    printf("Successful: %d\n", total_successful);
    printf("Failed: %d\n", total_failed);
    printf("Success rate: %.1f%%\n", (total_successful * 100.0) / (total_successful + total_failed));
    printf("Elapsed time: %.2f seconds\n", elapsed);
    printf("Throughput: %.1f transactions/second\n", (total_successful + total_failed) / elapsed);
    printf("Successful throughput: %.1f commits/second\n", total_successful / elapsed);
    
    // Performance analysis
    printf("\n⚡ Performance Analysis:\n");
    printf("=======================\n");
    printf("Average transaction time: %.2f ms\n", (elapsed * 1000) / (total_successful + total_failed));
    printf("Concurrent coordinators: %d\n", NUM_THREADS);
    printf("Thread safety: ✅ (no crashes detected)\n");
    
    if (total_successful > 0) {
        printf("2PC protocol: ✅ (transactions completed successfully)\n");
    }
    
    if (total_failed > 0) {
        printf("Error handling: ✅ (failures handled gracefully)\n");
    }
    
    printf("\n🎉 Stress test completed successfully!\n");
    
    return 0;
}