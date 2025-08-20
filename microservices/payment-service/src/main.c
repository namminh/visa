/*
 * PAYMENT SERVICE - Core orchestration microservice
 * Port: 8080
 * Purpose: Handle payment authorization requests, coordinate with other services
 * 
 * API Endpoints:
 * - POST /payments/authorize
 * - POST /payments/capture  
 * - GET /payments/status/{txn_id}
 * - GET /health, /metrics, /readiness
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>

// Configuration
typedef struct {
    int port;
    char risk_service_url[256];
    char clearing_service_url[256];
    char ledger_service_url[256];
    char db_uri[512];
    int max_threads;
    int queue_capacity;
} PaymentConfig;

// HTTP Response structure
typedef struct {
    char *data;
    size_t size;
} HttpResponse;

// Payment request structure
typedef struct {
    char request_id[128];
    char pan[20];
    char amount[16];
    char currency[4];
    char merchant_id[64];
    time_t timestamp;
} PaymentRequest;

// Global configuration
static PaymentConfig g_config = {0};
static pthread_mutex_t g_metrics_lock = PTHREAD_MUTEX_INITIALIZER;
static long g_total_requests = 0;
static long g_approved_requests = 0;
static long g_declined_requests = 0;

// HTTP Response callback for libcurl
static size_t http_response_callback(void *contents, size_t size, size_t nmemb, HttpResponse *response) {
    size_t total_size = size * nmemb;
    char *new_data = realloc(response->data, response->size + total_size + 1);
    if (!new_data) return 0;
    
    response->data = new_data;
    memcpy(&(response->data[response->size]), contents, total_size);
    response->size += total_size;
    response->data[response->size] = '\0';
    return total_size;
}

// Make HTTP POST request to service
static int http_post_json(const char *url, const char *json_data, HttpResponse *response) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    
    response->data = malloc(1);
    response->size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5 second timeout
    
    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && response_code == 200) ? 0 : -1;
}

// Validate PAN using Luhn algorithm
static int validate_luhn(const char *pan) {
    int sum = 0, alt = 0;
    size_t len = strlen(pan);
    
    for (int i = (int)len - 1; i >= 0; i--) {
        if (pan[i] < '0' || pan[i] > '9') return 0;
        int digit = pan[i] - '0';
        if (alt) {
            digit *= 2;
            if (digit > 9) digit -= 9;
        }
        sum += digit;
        alt = !alt;
    }
    return (sum % 10) == 0;
}

// Mask PAN for logging (show first 6 and last 4 digits)
static void mask_pan(const char *pan, char *masked, size_t max_len) {
    size_t len = strlen(pan);
    if (len <= 10 || max_len < len + 1) {
        strncpy(masked, pan, max_len - 1);
        masked[max_len - 1] = '\0';
        return;
    }
    
    // First 6 digits
    memcpy(masked, pan, 6);
    // Middle asterisks
    for (size_t i = 6; i < len - 4; i++) {
        masked[i] = '*';
    }
    // Last 4 digits
    memcpy(masked + len - 4, pan + len - 4, 4);
    masked[len] = '\0';
}

// Call Risk Service to evaluate transaction
static int call_risk_service(const PaymentRequest *req) {
    cJSON *risk_req = cJSON_CreateObject();
    cJSON_AddStringToObject(risk_req, "pan", req->pan);
    cJSON_AddStringToObject(risk_req, "amount", req->amount);
    cJSON_AddStringToObject(risk_req, "currency", req->currency);
    cJSON_AddStringToObject(risk_req, "merchant_id", req->merchant_id);
    
    char *json_string = cJSON_Print(risk_req);
    cJSON_Delete(risk_req);
    
    if (!json_string) return -1;
    
    HttpResponse response = {0};
    char url[512];
    snprintf(url, sizeof(url), "%s/risk/evaluate", g_config.risk_service_url);
    
    int result = http_post_json(url, json_string, &response);
    free(json_string);
    
    if (result != 0) {
        if (response.data) free(response.data);
        return -1; // Service unavailable
    }
    
    // Parse response
    cJSON *json = cJSON_Parse(response.data);
    int allow = 0;
    if (json) {
        cJSON *allow_field = cJSON_GetObjectItem(json, "allow");
        if (cJSON_IsBool(allow_field)) {
            allow = cJSON_IsTrue(allow_field);
        }
        cJSON_Delete(json);
    }
    
    free(response.data);
    return allow ? 0 : -1;
}

// Call Clearing Service for authorization
static int call_clearing_service(const char *txn_id, const PaymentRequest *req) {
    cJSON *clearing_req = cJSON_CreateObject();
    cJSON_AddStringToObject(clearing_req, "txn_id", txn_id);
    cJSON_AddStringToObject(clearing_req, "pan", req->pan);
    cJSON_AddStringToObject(clearing_req, "amount", req->amount);
    cJSON_AddStringToObject(clearing_req, "currency", req->currency);
    cJSON_AddStringToObject(clearing_req, "merchant_id", req->merchant_id);
    
    char *json_string = cJSON_Print(clearing_req);
    cJSON_Delete(clearing_req);
    
    if (!json_string) return -1;
    
    // Phase 1: Prepare
    HttpResponse response = {0};
    char url[512];
    snprintf(url, sizeof(url), "%s/clearing/prepare", g_config.clearing_service_url);
    
    int result = http_post_json(url, json_string, &response);
    if (result != 0) {
        free(json_string);
        if (response.data) free(response.data);
        return -1;
    }
    
    // Parse prepare response
    cJSON *json = cJSON_Parse(response.data);
    int prepared = 0;
    if (json) {
        cJSON *ok_field = cJSON_GetObjectItem(json, "ok");
        if (cJSON_IsBool(ok_field)) {
            prepared = cJSON_IsTrue(ok_field);
        }
        cJSON_Delete(json);
    }
    free(response.data);
    
    if (!prepared) {
        free(json_string);
        return -1;
    }
    
    // Phase 2: Commit
    snprintf(url, sizeof(url), "%s/clearing/commit", g_config.clearing_service_url);
    response.data = NULL;
    response.size = 0;
    
    result = http_post_json(url, json_string, &response);
    free(json_string);
    
    if (result != 0) {
        if (response.data) free(response.data);
        // TODO: Call abort endpoint
        return -1;
    }
    
    // Parse commit response
    json = cJSON_Parse(response.data);
    int committed = 0;
    if (json) {
        cJSON *ok_field = cJSON_GetObjectItem(json, "ok");
        if (cJSON_IsBool(ok_field)) {
            committed = cJSON_IsTrue(ok_field);
        }
        cJSON_Delete(json);
    }
    free(response.data);
    
    return committed ? 0 : -1;
}

// Process authorization request
static cJSON* process_authorization(const PaymentRequest *req) {
    cJSON *response = cJSON_CreateObject();
    
    // Generate transaction ID
    char txn_id[128];
    snprintf(txn_id, sizeof(txn_id), "visa_%s_%ld", req->request_id, time(NULL));
    
    // Step 1: Validate PAN
    if (!validate_luhn(req->pan)) {
        cJSON_AddStringToObject(response, "status", "DECLINED");
        cJSON_AddStringToObject(response, "reason", "invalid_pan");
        cJSON_AddStringToObject(response, "txn_id", txn_id);
        return response;
    }
    
    // Step 2: Validate amount
    double amount = atof(req->amount);
    if (amount <= 0.0 || amount > 10000.0) {
        cJSON_AddStringToObject(response, "status", "DECLINED");
        cJSON_AddStringToObject(response, "reason", "invalid_amount");
        cJSON_AddStringToObject(response, "txn_id", txn_id);
        return response;
    }
    
    // Step 3: Call Risk Service
    if (call_risk_service(req) != 0) {
        cJSON_AddStringToObject(response, "status", "DECLINED");
        cJSON_AddStringToObject(response, "reason", "risk_declined");
        cJSON_AddStringToObject(response, "txn_id", txn_id);
        return response;
    }
    
    // Step 4: Call Clearing Service
    if (call_clearing_service(txn_id, req) != 0) {
        cJSON_AddStringToObject(response, "status", "DECLINED");
        cJSON_AddStringToObject(response, "reason", "clearing_failed");
        cJSON_AddStringToObject(response, "txn_id", txn_id);
        return response;
    }
    
    // Success
    cJSON_AddStringToObject(response, "status", "APPROVED");
    cJSON_AddStringToObject(response, "txn_id", txn_id);
    cJSON_AddNumberToObject(response, "timestamp", time(NULL));
    
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
    
    // Update metrics
    pthread_mutex_lock(&g_metrics_lock);
    g_total_requests++;
    pthread_mutex_unlock(&g_metrics_lock);
    
    // Parse HTTP request line
    char method[16], path[256], version[16];
    sscanf(buffer, "%15s %255s %15s", method, path, version);
    
    cJSON *json_response = NULL;
    int status_code = 200;
    const char *status_text = "OK";
    
    // Route handling
    if (strcmp(method, "POST") == 0 && strncmp(path, "/payments/authorize", 19) == 0) {
        // Extract JSON body from HTTP request
        char *body_start = strstr(buffer, "\\r\\n\\r\\n");
        if (body_start) {
            body_start += 4; // Skip \\r\\n\\r\\n
            
            cJSON *request_json = cJSON_Parse(body_start);
            if (request_json) {
                PaymentRequest req = {0};
                
                cJSON *request_id = cJSON_GetObjectItem(request_json, "request_id");
                cJSON *pan = cJSON_GetObjectItem(request_json, "pan");
                cJSON *amount = cJSON_GetObjectItem(request_json, "amount");
                cJSON *currency = cJSON_GetObjectItem(request_json, "currency");
                cJSON *merchant_id = cJSON_GetObjectItem(request_json, "merchant_id");
                
                if (cJSON_IsString(request_id)) strncpy(req.request_id, request_id->valuestring, sizeof(req.request_id) - 1);
                if (cJSON_IsString(pan)) strncpy(req.pan, pan->valuestring, sizeof(req.pan) - 1);
                if (cJSON_IsString(amount)) strncpy(req.amount, amount->valuestring, sizeof(req.amount) - 1);
                if (cJSON_IsString(currency)) strncpy(req.currency, currency->valuestring, sizeof(req.currency) - 1);
                if (cJSON_IsString(merchant_id)) strncpy(req.merchant_id, merchant_id->valuestring, sizeof(req.merchant_id) - 1);
                
                req.timestamp = time(NULL);
                
                json_response = process_authorization(&req);
                
                // Update metrics
                pthread_mutex_lock(&g_metrics_lock);
                cJSON *status = cJSON_GetObjectItem(json_response, "status");
                if (cJSON_IsString(status)) {
                    if (strcmp(status->valuestring, "APPROVED") == 0) {
                        g_approved_requests++;
                    } else {
                        g_declined_requests++;
                    }
                }
                pthread_mutex_unlock(&g_metrics_lock);
                
                cJSON_Delete(request_json);
            } else {
                json_response = cJSON_CreateObject();
                cJSON_AddStringToObject(json_response, "status", "DECLINED");
                cJSON_AddStringToObject(json_response, "reason", "invalid_json");
                status_code = 400;
                status_text = "Bad Request";
            }
        } else {
            json_response = cJSON_CreateObject();
            cJSON_AddStringToObject(json_response, "status", "DECLINED");
            cJSON_AddStringToObject(json_response, "reason", "no_body");
            status_code = 400;
            status_text = "Bad Request";
        }
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        json_response = cJSON_CreateObject();
        cJSON_AddStringToObject(json_response, "status", "healthy");
        cJSON_AddNumberToObject(json_response, "timestamp", time(NULL));
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/metrics") == 0) {
        pthread_mutex_lock(&g_metrics_lock);
        json_response = cJSON_CreateObject();
        cJSON_AddNumberToObject(json_response, "total_requests", g_total_requests);
        cJSON_AddNumberToObject(json_response, "approved_requests", g_approved_requests);
        cJSON_AddNumberToObject(json_response, "declined_requests", g_declined_requests);
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

// Load configuration from environment variables
static void load_config() {
    g_config.port = getenv("PORT") ? atoi(getenv("PORT")) : 8080;
    strncpy(g_config.risk_service_url, 
            getenv("RISK_SERVICE_URL") ? getenv("RISK_SERVICE_URL") : "http://localhost:8081",
            sizeof(g_config.risk_service_url) - 1);
    strncpy(g_config.clearing_service_url,
            getenv("CLEARING_SERVICE_URL") ? getenv("CLEARING_SERVICE_URL") : "http://localhost:8082", 
            sizeof(g_config.clearing_service_url) - 1);
    strncpy(g_config.ledger_service_url,
            getenv("LEDGER_SERVICE_URL") ? getenv("LEDGER_SERVICE_URL") : "http://localhost:8083",
            sizeof(g_config.ledger_service_url) - 1);
    strncpy(g_config.db_uri,
            getenv("DB_URI") ? getenv("DB_URI") : "postgresql://localhost:5432/payments",
            sizeof(g_config.db_uri) - 1);
    g_config.max_threads = getenv("MAX_THREADS") ? atoi(getenv("MAX_THREADS")) : 10;
    g_config.queue_capacity = getenv("QUEUE_CAPACITY") ? atoi(getenv("QUEUE_CAPACITY")) : 100;
}

int main() {
    printf("Starting Payment Service...\\n");
    
    // Load configuration
    load_config();
    
    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
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
    
    printf("Payment Service listening on port %d\\n", g_config.port);
    printf("Risk Service URL: %s\\n", g_config.risk_service_url);
    printf("Clearing Service URL: %s\\n", g_config.clearing_service_url);
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        // Handle request (could be threaded for better performance)
        handle_request(client_socket);
    }
    
    // Cleanup
    close(server_socket);
    curl_global_cleanup();
    return 0;
}