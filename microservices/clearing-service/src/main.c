/*
 * CLEARING SERVICE - External network simulation microservice  
 * Port: 8082
 * Purpose: Simulate external card network operations (2PC participant)
 * 
 * API Endpoints:
 * - POST /clearing/prepare
 * - POST /clearing/commit  
 * - POST /clearing/abort
 * - GET /clearing/status/{txn_id}
 * - GET /health, /metrics
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <cjson/cJSON.h>

// Configuration
typedef struct {
    int port;
    int simulate_failures;      // For testing - percentage of failures to simulate
    int prepare_timeout_sec;
    int commit_timeout_sec;
    char db_uri[512];
} ClearingConfig;

// Transaction states in clearing system
typedef enum {
    TXN_STATE_UNKNOWN = 0,
    TXN_STATE_PREPARED,
    TXN_STATE_COMMITTED, 
    TXN_STATE_ABORTED,
    TXN_STATE_TIMEOUT
} TransactionState;

// Transaction record in clearing system
typedef struct {
    char txn_id[128];
    char pan[20];
    char amount[16];
    char currency[4];
    char merchant_id[64];
    TransactionState state;
    time_t created_at;
    time_t updated_at;
    char error_message[256];
    int retry_count;
} ClearingTransaction;

// Global state
static ClearingConfig g_config = {0};
static pthread_mutex_t g_transactions_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_metrics_lock = PTHREAD_MUTEX_INITIALIZER;

#define MAX_TRANSACTIONS 10000
static ClearingTransaction g_transactions[MAX_TRANSACTIONS];
static int g_transaction_count = 0;

// Metrics
static long g_total_prepare_requests = 0;
static long g_successful_prepares = 0;
static long g_failed_prepares = 0;
static long g_total_commit_requests = 0;
static long g_successful_commits = 0;
static long g_failed_commits = 0;
static long g_total_abort_requests = 0;

// Mask PAN for logging
static void mask_pan(const char *pan, char *masked, size_t max_len) {
    size_t len = strlen(pan);
    if (len <= 10 || max_len < len + 1) {
        strncpy(masked, pan, max_len - 1);
        masked[max_len - 1] = '\0';
        return;
    }
    
    memcpy(masked, pan, 6);
    for (size_t i = 6; i < len - 4; i++) {
        masked[i] = '*';
    }
    memcpy(masked + len - 4, pan + len - 4, 4);
    masked[len] = '\0';
}

// Find transaction by ID
static ClearingTransaction* find_transaction(const char *txn_id) {
    for (int i = 0; i < g_transaction_count; i++) {
        if (strcmp(g_transactions[i].txn_id, txn_id) == 0) {
            return &g_transactions[i];
        }
    }
    return NULL;
}

// Add new transaction
static ClearingTransaction* add_transaction(const char *txn_id) {
    if (g_transaction_count >= MAX_TRANSACTIONS) {
        return NULL; // Table full
    }
    
    ClearingTransaction *txn = &g_transactions[g_transaction_count++];
    strncpy(txn->txn_id, txn_id, sizeof(txn->txn_id) - 1);
    txn->state = TXN_STATE_UNKNOWN;
    txn->created_at = time(NULL);
    txn->updated_at = txn->created_at;
    txn->error_message[0] = '\0';
    txn->retry_count = 0;
    
    return txn;
}

// Simulate external network delay
static void simulate_network_delay() {
    // Random delay between 50-200ms to simulate network latency
    int delay_ms = 50 + (rand() % 150);
    usleep(delay_ms * 1000);
}

// Simulate random failures for testing
static int should_simulate_failure() {
    if (g_config.simulate_failures <= 0) return 0;
    return (rand() % 100) < g_config.simulate_failures;
}

// Prepare transaction (Phase 1 of 2PC)
static cJSON* handle_prepare(cJSON *request) {
    cJSON *response = cJSON_CreateObject();
    
    // Extract request fields
    cJSON *txn_id_field = cJSON_GetObjectItem(request, "txn_id");
    cJSON *pan_field = cJSON_GetObjectItem(request, "pan");
    cJSON *amount_field = cJSON_GetObjectItem(request, "amount");
    cJSON *currency_field = cJSON_GetObjectItem(request, "currency");
    cJSON *merchant_field = cJSON_GetObjectItem(request, "merchant_id");
    
    if (!cJSON_IsString(txn_id_field) || !cJSON_IsString(pan_field) || !cJSON_IsString(amount_field)) {
        cJSON_AddBoolToObject(response, "ok", cJSON_False);
        cJSON_AddStringToObject(response, "error", "missing_required_fields");
        return response;
    }
    
    const char *txn_id = txn_id_field->valuestring;
    const char *pan = pan_field->valuestring;
    const char *amount = amount_field->valuestring;
    const char *currency = cJSON_IsString(currency_field) ? currency_field->valuestring : "USD";
    const char *merchant_id = cJSON_IsString(merchant_field) ? merchant_field->valuestring : "UNKNOWN";
    
    char masked_pan[32];
    mask_pan(pan, masked_pan, sizeof(masked_pan));
    
    // Update metrics
    pthread_mutex_lock(&g_metrics_lock);
    g_total_prepare_requests++;
    pthread_mutex_unlock(&g_metrics_lock);
    
    printf("Clearing prepare: txn_id=%s pan=%s amount=%s\\n", txn_id, masked_pan, amount);
    
    // Simulate network delay
    simulate_network_delay();
    
    // Check for simulated failure
    if (should_simulate_failure()) {
        cJSON_AddBoolToObject(response, "ok", cJSON_False);
        cJSON_AddStringToObject(response, "error", "network_timeout");
        
        pthread_mutex_lock(&g_metrics_lock);
        g_failed_prepares++;
        pthread_mutex_unlock(&g_metrics_lock);
        
        printf("Clearing prepare failed (simulated): txn_id=%s\\n", txn_id);
        return response;
    }
    
    pthread_mutex_lock(&g_transactions_lock);
    
    // Check if transaction already exists (idempotency)
    ClearingTransaction *txn = find_transaction(txn_id);
    if (txn) {
        if (txn->state == TXN_STATE_PREPARED) {
            // Already prepared, return success (idempotent)
            cJSON_AddBoolToObject(response, "ok", cJSON_True);
            cJSON_AddStringToObject(response, "status", "already_prepared");
        } else {
            cJSON_AddBoolToObject(response, "ok", cJSON_False);
            cJSON_AddStringToObject(response, "error", "transaction_in_invalid_state");
        }
    } else {
        // New transaction - add to table
        txn = add_transaction(txn_id);
        if (!txn) {
            cJSON_AddBoolToObject(response, "ok", cJSON_False);
            cJSON_AddStringToObject(response, "error", "transaction_table_full");
        } else {
            // Store transaction details
            strncpy(txn->pan, pan, sizeof(txn->pan) - 1);
            strncpy(txn->amount, amount, sizeof(txn->amount) - 1);
            strncpy(txn->currency, currency, sizeof(txn->currency) - 1);
            strncpy(txn->merchant_id, merchant_id, sizeof(txn->merchant_id) - 1);
            txn->state = TXN_STATE_PREPARED;
            txn->updated_at = time(NULL);
            
            cJSON_AddBoolToObject(response, "ok", cJSON_True);
            cJSON_AddStringToObject(response, "status", "prepared");
            
            pthread_mutex_lock(&g_metrics_lock);
            g_successful_prepares++;
            pthread_mutex_unlock(&g_metrics_lock);
            
            printf("Clearing prepare success: txn_id=%s\\n", txn_id);
        }
    }
    
    pthread_mutex_unlock(&g_transactions_lock);
    return response;
}

// Commit transaction (Phase 2 of 2PC)
static cJSON* handle_commit(cJSON *request) {
    cJSON *response = cJSON_CreateObject();
    
    // Extract transaction ID
    cJSON *txn_id_field = cJSON_GetObjectItem(request, "txn_id");
    if (!cJSON_IsString(txn_id_field)) {
        cJSON_AddBoolToObject(response, "ok", cJSON_False);
        cJSON_AddStringToObject(response, "error", "missing_txn_id");
        return response;
    }
    
    const char *txn_id = txn_id_field->valuestring;
    
    // Update metrics
    pthread_mutex_lock(&g_metrics_lock);
    g_total_commit_requests++;
    pthread_mutex_unlock(&g_metrics_lock);
    
    printf("Clearing commit: txn_id=%s\\n", txn_id);
    
    // Simulate network delay
    simulate_network_delay();
    
    // Check for simulated failure
    if (should_simulate_failure()) {
        cJSON_AddBoolToObject(response, "ok", cJSON_False);
        cJSON_AddStringToObject(response, "error", "commit_failed");
        
        pthread_mutex_lock(&g_metrics_lock);
        g_failed_commits++;
        pthread_mutex_unlock(&g_metrics_lock);
        
        printf("Clearing commit failed (simulated): txn_id=%s\\n", txn_id);
        return response;
    }
    
    pthread_mutex_lock(&g_transactions_lock);
    
    ClearingTransaction *txn = find_transaction(txn_id);
    if (!txn) {
        cJSON_AddBoolToObject(response, "ok", cJSON_False);
        cJSON_AddStringToObject(response, "error", "transaction_not_found");
    } else if (txn->state == TXN_STATE_COMMITTED) {
        // Already committed (idempotent)
        cJSON_AddBoolToObject(response, "ok", cJSON_True);
        cJSON_AddStringToObject(response, "status", "already_committed");
    } else if (txn->state != TXN_STATE_PREPARED) {
        cJSON_AddBoolToObject(response, "ok", cJSON_False);
        cJSON_AddStringToObject(response, "error", "transaction_not_prepared");
    } else {
        // Commit the transaction
        txn->state = TXN_STATE_COMMITTED;
        txn->updated_at = time(NULL);
        
        cJSON_AddBoolToObject(response, "ok", cJSON_True);
        cJSON_AddStringToObject(response, "status", "committed");
        
        pthread_mutex_lock(&g_metrics_lock);
        g_successful_commits++;
        pthread_mutex_unlock(&g_metrics_lock);
        
        printf("Clearing commit success: txn_id=%s\\n", txn_id);
    }
    
    pthread_mutex_unlock(&g_transactions_lock);
    return response;
}

// Abort transaction
static cJSON* handle_abort(cJSON *request) {
    cJSON *response = cJSON_CreateObject();
    
    // Extract transaction ID
    cJSON *txn_id_field = cJSON_GetObjectItem(request, "txn_id");
    if (!cJSON_IsString(txn_id_field)) {
        cJSON_AddBoolToObject(response, "ok", cJSON_False);
        cJSON_AddStringToObject(response, "error", "missing_txn_id");
        return response;
    }
    
    const char *txn_id = txn_id_field->valuestring;
    
    // Update metrics
    pthread_mutex_lock(&g_metrics_lock);
    g_total_abort_requests++;
    pthread_mutex_unlock(&g_metrics_lock);
    
    printf("Clearing abort: txn_id=%s\\n", txn_id);
    
    pthread_mutex_lock(&g_transactions_lock);
    
    ClearingTransaction *txn = find_transaction(txn_id);
    if (!txn) {
        cJSON_AddBoolToObject(response, "ok", cJSON_True);
        cJSON_AddStringToObject(response, "status", "not_found_ok");
    } else {
        txn->state = TXN_STATE_ABORTED;
        txn->updated_at = time(NULL);
        
        cJSON_AddBoolToObject(response, "ok", cJSON_True);
        cJSON_AddStringToObject(response, "status", "aborted");
        
        printf("Clearing abort success: txn_id=%s\\n", txn_id);
    }
    
    pthread_mutex_unlock(&g_transactions_lock);
    return response;
}

// Get transaction status
static cJSON* handle_status(const char *txn_id) {
    cJSON *response = cJSON_CreateObject();
    
    pthread_mutex_lock(&g_transactions_lock);
    
    ClearingTransaction *txn = find_transaction(txn_id);
    if (!txn) {
        cJSON_AddStringToObject(response, "status", "NOT_FOUND");
    } else {
        const char *state_str = "UNKNOWN";
        switch (txn->state) {
            case TXN_STATE_PREPARED: state_str = "PREPARED"; break;
            case TXN_STATE_COMMITTED: state_str = "COMMITTED"; break;
            case TXN_STATE_ABORTED: state_str = "ABORTED"; break;
            case TXN_STATE_TIMEOUT: state_str = "TIMEOUT"; break;
            default: state_str = "UNKNOWN"; break;
        }
        
        cJSON_AddStringToObject(response, "txn_id", txn->txn_id);
        cJSON_AddStringToObject(response, "status", state_str);
        cJSON_AddNumberToObject(response, "created_at", txn->created_at);
        cJSON_AddNumberToObject(response, "updated_at", txn->updated_at);
        
        char masked_pan[32];
        mask_pan(txn->pan, masked_pan, sizeof(masked_pan));
        cJSON_AddStringToObject(response, "masked_pan", masked_pan);
        cJSON_AddStringToObject(response, "amount", txn->amount);
        cJSON_AddStringToObject(response, "currency", txn->currency);
        
        if (txn->error_message[0] != '\0') {
            cJSON_AddStringToObject(response, "error", txn->error_message);
        }
    }
    
    pthread_mutex_unlock(&g_transactions_lock);
    return response;
}

// Handle HTTP request
static void handle_request(int client_socket) {
    char buffer[4096] = {0};
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    // Parse HTTP request line
    char method[16], path[256], version[16];
    sscanf(buffer, "%15s %255s %15s", method, path, version);
    
    cJSON *json_response = NULL;
    int status_code = 200;
    const char *status_text = "OK";
    
    // Route handling
    if (strcmp(method, "POST") == 0 && strcmp(path, "/clearing/prepare") == 0) {
        char *body_start = strstr(buffer, "\\r\\n\\r\\n");
        if (body_start) {
            body_start += 4;
            cJSON *request_json = cJSON_Parse(body_start);
            if (request_json) {
                json_response = handle_prepare(request_json);
                cJSON_Delete(request_json);
            } else {
                json_response = cJSON_CreateObject();
                cJSON_AddBoolToObject(json_response, "ok", cJSON_False);
                cJSON_AddStringToObject(json_response, "error", "invalid_json");
                status_code = 400;
                status_text = "Bad Request";
            }
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/clearing/commit") == 0) {
        char *body_start = strstr(buffer, "\\r\\n\\r\\n");
        if (body_start) {
            body_start += 4;
            cJSON *request_json = cJSON_Parse(body_start);
            if (request_json) {
                json_response = handle_commit(request_json);
                cJSON_Delete(request_json);
            } else {
                json_response = cJSON_CreateObject();
                cJSON_AddBoolToObject(json_response, "ok", cJSON_False);
                cJSON_AddStringToObject(json_response, "error", "invalid_json");
                status_code = 400;
                status_text = "Bad Request";
            }
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/clearing/abort") == 0) {
        char *body_start = strstr(buffer, "\\r\\n\\r\\n");
        if (body_start) {
            body_start += 4;
            cJSON *request_json = cJSON_Parse(body_start);
            if (request_json) {
                json_response = handle_abort(request_json);
                cJSON_Delete(request_json);
            } else {
                json_response = cJSON_CreateObject();
                cJSON_AddBoolToObject(json_response, "ok", cJSON_False);
                cJSON_AddStringToObject(json_response, "error", "invalid_json");
                status_code = 400;
                status_text = "Bad Request";
            }
        }
    } else if (strcmp(method, "GET") == 0 && strncmp(path, "/clearing/status/", 17) == 0) {
        const char *txn_id = path + 17;
        json_response = handle_status(txn_id);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        json_response = cJSON_CreateObject();
        cJSON_AddStringToObject(json_response, "status", "healthy");
        cJSON_AddStringToObject(json_response, "service", "clearing-service");
        cJSON_AddNumberToObject(json_response, "timestamp", time(NULL));
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/metrics") == 0) {
        pthread_mutex_lock(&g_metrics_lock);
        json_response = cJSON_CreateObject();
        cJSON_AddNumberToObject(json_response, "total_prepare_requests", g_total_prepare_requests);
        cJSON_AddNumberToObject(json_response, "successful_prepares", g_successful_prepares);
        cJSON_AddNumberToObject(json_response, "failed_prepares", g_failed_prepares);
        cJSON_AddNumberToObject(json_response, "total_commit_requests", g_total_commit_requests);
        cJSON_AddNumberToObject(json_response, "successful_commits", g_successful_commits);
        cJSON_AddNumberToObject(json_response, "failed_commits", g_failed_commits);
        cJSON_AddNumberToObject(json_response, "total_abort_requests", g_total_abort_requests);
        
        // Calculate success rates
        if (g_total_prepare_requests > 0) {
            double prepare_success_rate = (double)g_successful_prepares / g_total_prepare_requests * 100.0;
            cJSON_AddNumberToObject(json_response, "prepare_success_rate_percent", prepare_success_rate);
        }
        if (g_total_commit_requests > 0) {
            double commit_success_rate = (double)g_successful_commits / g_total_commit_requests * 100.0;
            cJSON_AddNumberToObject(json_response, "commit_success_rate_percent", commit_success_rate);
        }
        
        pthread_mutex_lock(&g_transactions_lock);
        cJSON_AddNumberToObject(json_response, "active_transactions", g_transaction_count);
        pthread_mutex_unlock(&g_transactions_lock);
        
        cJSON_AddNumberToObject(json_response, "timestamp", time(NULL));
        pthread_mutex_unlock(&g_metrics_lock);
    } else {
        json_response = cJSON_CreateObject();
        cJSON_AddStringToObject(json_response, "error", "not_found");
        status_code = 404;
        status_text = "Not Found";
    }
    
    // Send HTTP response
    if (json_response) {
        char *json_string = cJSON_Print(json_response);
        if (json_string) {
            char http_response[8192];
            int response_len = snprintf(http_response, sizeof(http_response),
                "HTTP/1.1 %d %s\\r\\n"
                "Content-Type: application/json\\r\\n"
                "Content-Length: %zu\\r\\n"
                "Connection: close\\r\\n"
                "\\r\\n"
                "%s",
                status_code, status_text, strlen(json_string), json_string);
            
            write(client_socket, http_response, response_len);
            free(json_string);
        }
        cJSON_Delete(json_response);
    }
    
    close(client_socket);
}

// Load configuration
static void load_config() {
    g_config.port = getenv("PORT") ? atoi(getenv("PORT")) : 8082;
    g_config.simulate_failures = getenv("CLEARING_SIMULATE_FAILURES") ? atoi(getenv("CLEARING_SIMULATE_FAILURES")) : 0;
    g_config.prepare_timeout_sec = getenv("CLEARING_PREPARE_TIMEOUT") ? atoi(getenv("CLEARING_PREPARE_TIMEOUT")) : 30;
    g_config.commit_timeout_sec = getenv("CLEARING_COMMIT_TIMEOUT") ? atoi(getenv("CLEARING_COMMIT_TIMEOUT")) : 30;
    strncpy(g_config.db_uri,
            getenv("DB_URI") ? getenv("DB_URI") : "postgresql://localhost:5432/clearing_db",
            sizeof(g_config.db_uri) - 1);
}

int main() {
    printf("Starting Clearing Service...\\n");
    
    // Initialize random seed for simulated failures
    srand(time(NULL));
    
    // Load configuration
    load_config();
    
    // Initialize transaction table
    memset(g_transactions, 0, sizeof(g_transactions));
    
    // Create server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind to address
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(g_config.port);
    
    if (bind(server_socket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_socket);
        return 1;
    }
    
    // Start listening
    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
        return 1;
    }
    
    printf("Clearing Service listening on port %d\\n", g_config.port);
    printf("Configuration:\\n");
    printf("  Simulate Failures: %d%%\\n", g_config.simulate_failures);
    printf("  Prepare Timeout: %d seconds\\n", g_config.prepare_timeout_sec);
    printf("  Commit Timeout: %d seconds\\n", g_config.commit_timeout_sec);
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        handle_request(client_socket);
    }
    
    close(server_socket);
    return 0;
}