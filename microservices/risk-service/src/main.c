/*
 * RISK SERVICE - Risk evaluation and fraud detection microservice
 * Port: 8081
 * Purpose: Evaluate transaction risk, apply business rules, detect fraud
 * 
 * API Endpoints:
 * - POST /risk/evaluate
 * - PUT /risk/rules/{rule_id}
 * - GET /risk/rules
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
    int max_amount;
    int velocity_limit;
    int velocity_window_sec;
    char db_uri[512];
} RiskConfig;

// Risk rule structure
typedef struct {
    int id;
    char name[128];
    char type[32];  // AMOUNT_LIMIT, VELOCITY, BLACKLIST, etc.
    int enabled;
    char parameters[512]; // JSON parameters
    time_t created_at;
    time_t updated_at;
} RiskRule;

// Velocity tracking entry
typedef struct {
    char pan[20];
    time_t window_start;
    int count;
    time_t last_seen;
} VelocityEntry;

// Global state
static RiskConfig g_config = {0};
static pthread_mutex_t g_velocity_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_metrics_lock = PTHREAD_MUTEX_INITIALIZER;

#define MAX_VELOCITY_ENTRIES 10000
static VelocityEntry g_velocity_table[MAX_VELOCITY_ENTRIES];

// Metrics
static long g_total_evaluations = 0;
static long g_approved_evaluations = 0;
static long g_declined_evaluations = 0;
static long g_velocity_blocks = 0;
static long g_amount_blocks = 0;
static long g_blacklist_blocks = 0;

// Simple hash function for PAN
static unsigned int hash_pan(const char *pan) {
    unsigned int hash = 0;
    for (int i = 0; pan[i] != '\0'; i++) {
        hash = hash * 31 + (unsigned char)pan[i];
    }
    return hash % MAX_VELOCITY_ENTRIES;
}

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

// Check velocity limit for PAN
static int check_velocity_limit(const char *pan) {
    time_t now = time(NULL);
    unsigned int index = hash_pan(pan);
    
    pthread_mutex_lock(&g_velocity_lock);
    
    VelocityEntry *entry = &g_velocity_table[index];
    
    // If slot is empty or different PAN, initialize
    if (entry->pan[0] == '\0' || strcmp(entry->pan, pan) != 0) {
        strncpy(entry->pan, pan, sizeof(entry->pan) - 1);
        entry->window_start = now;
        entry->count = 1;
        entry->last_seen = now;
        pthread_mutex_unlock(&g_velocity_lock);
        return 0; // Allow
    }
    
    // Check if window has expired
    if (now - entry->window_start >= g_config.velocity_window_sec) {
        entry->window_start = now;
        entry->count = 1;
        entry->last_seen = now;
        pthread_mutex_unlock(&g_velocity_lock);
        return 0; // Allow
    }
    
    // Increment count
    entry->count++;
    entry->last_seen = now;
    
    int allow = (entry->count <= g_config.velocity_limit);
    
    if (!allow) {
        pthread_mutex_lock(&g_metrics_lock);
        g_velocity_blocks++;
        pthread_mutex_unlock(&g_metrics_lock);
    }
    
    pthread_mutex_unlock(&g_velocity_lock);
    return allow ? 0 : -1;
}

// Check amount limit
static int check_amount_limit(double amount) {
    if (amount > g_config.max_amount) {
        pthread_mutex_lock(&g_metrics_lock);
        g_amount_blocks++;
        pthread_mutex_unlock(&g_metrics_lock);
        return -1;
    }
    return 0;
}

// Simple blacklist check (BIN-based)
static int check_blacklist(const char *pan) {
    // Extract first 6 digits (BIN)
    if (strlen(pan) >= 6) {
        char bin[7];
        strncpy(bin, pan, 6);
        bin[6] = '\0';
        
        // Hardcoded blacklist for demo
        const char *blacklisted_bins[] = {
            "999999",  // Test BIN
            "000000",  // Invalid BIN
            "123456",  // Demo blacklist
            NULL
        };
        
        for (int i = 0; blacklisted_bins[i] != NULL; i++) {
            if (strcmp(bin, blacklisted_bins[i]) == 0) {
                pthread_mutex_lock(&g_metrics_lock);
                g_blacklist_blocks++;
                pthread_mutex_unlock(&g_metrics_lock);
                return -1;
            }
        }
    }
    return 0;
}

// Evaluate transaction risk
static cJSON* evaluate_risk(cJSON *request) {
    cJSON *response = cJSON_CreateObject();
    
    // Extract request fields
    cJSON *pan_field = cJSON_GetObjectItem(request, "pan");
    cJSON *amount_field = cJSON_GetObjectItem(request, "amount");
    cJSON *currency_field = cJSON_GetObjectItem(request, "currency");
    cJSON *merchant_field = cJSON_GetObjectItem(request, "merchant_id");
    
    if (!cJSON_IsString(pan_field) || !cJSON_IsString(amount_field)) {
        cJSON_AddBoolToObject(response, "allow", cJSON_False);
        cJSON_AddStringToObject(response, "reason", "missing_fields");
        return response;
    }
    
    const char *pan = pan_field->valuestring;
    double amount = atof(amount_field->valuestring);
    
    char masked_pan[32];
    mask_pan(pan, masked_pan, sizeof(masked_pan));
    
    // Update metrics
    pthread_mutex_lock(&g_metrics_lock);
    g_total_evaluations++;
    pthread_mutex_unlock(&g_metrics_lock);
    
    // Risk checks
    
    // 1. Amount limit check
    if (check_amount_limit(amount) != 0) {
        cJSON_AddBoolToObject(response, "allow", cJSON_False);
        cJSON_AddStringToObject(response, "reason", "amount_limit_exceeded");
        cJSON_AddStringToObject(response, "masked_pan", masked_pan);
        
        pthread_mutex_lock(&g_metrics_lock);
        g_declined_evaluations++;
        pthread_mutex_unlock(&g_metrics_lock);
        
        printf("Risk declined: amount_limit_exceeded for %s amount=%.2f\\n", masked_pan, amount);
        return response;
    }
    
    // 2. Blacklist check
    if (check_blacklist(pan) != 0) {
        cJSON_AddBoolToObject(response, "allow", cJSON_False);
        cJSON_AddStringToObject(response, "reason", "blacklisted_pan");
        cJSON_AddStringToObject(response, "masked_pan", masked_pan);
        
        pthread_mutex_lock(&g_metrics_lock);
        g_declined_evaluations++;
        pthread_mutex_unlock(&g_metrics_lock);
        
        printf("Risk declined: blacklisted_pan for %s\\n", masked_pan);
        return response;
    }
    
    // 3. Velocity check
    if (check_velocity_limit(pan) != 0) {
        cJSON_AddBoolToObject(response, "allow", cJSON_False);
        cJSON_AddStringToObject(response, "reason", "velocity_limit_exceeded");
        cJSON_AddStringToObject(response, "masked_pan", masked_pan);
        
        pthread_mutex_lock(&g_metrics_lock);
        g_declined_evaluations++;
        pthread_mutex_unlock(&g_metrics_lock);
        
        printf("Risk declined: velocity_limit_exceeded for %s\\n", masked_pan);
        return response;
    }
    
    // All checks passed
    cJSON_AddBoolToObject(response, "allow", cJSON_True);
    cJSON_AddStringToObject(response, "reason", "approved");
    cJSON_AddStringToObject(response, "masked_pan", masked_pan);
    cJSON_AddNumberToObject(response, "risk_score", 0.1); // Low risk score
    
    pthread_mutex_lock(&g_metrics_lock);
    g_approved_evaluations++;
    pthread_mutex_unlock(&g_metrics_lock);
    
    printf("Risk approved for %s amount=%.2f\\n", masked_pan, amount);
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
    if (strcmp(method, "POST") == 0 && strcmp(path, "/risk/evaluate") == 0) {
        // Extract JSON body from HTTP request
        char *body_start = strstr(buffer, "\\r\\n\\r\\n");
        if (body_start) {
            body_start += 4;
            
            cJSON *request_json = cJSON_Parse(body_start);
            if (request_json) {
                json_response = evaluate_risk(request_json);
                cJSON_Delete(request_json);
            } else {
                json_response = cJSON_CreateObject();
                cJSON_AddBoolToObject(json_response, "allow", cJSON_False);
                cJSON_AddStringToObject(json_response, "reason", "invalid_json");
                status_code = 400;
                status_text = "Bad Request";
            }
        } else {
            json_response = cJSON_CreateObject();
            cJSON_AddBoolToObject(json_response, "allow", cJSON_False);
            cJSON_AddStringToObject(json_response, "reason", "no_body");
            status_code = 400;
            status_text = "Bad Request";
        }
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        json_response = cJSON_CreateObject();
        cJSON_AddStringToObject(json_response, "status", "healthy");
        cJSON_AddStringToObject(json_response, "service", "risk-service");
        cJSON_AddNumberToObject(json_response, "timestamp", time(NULL));
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/metrics") == 0) {
        pthread_mutex_lock(&g_metrics_lock);
        json_response = cJSON_CreateObject();
        cJSON_AddNumberToObject(json_response, "total_evaluations", g_total_evaluations);
        cJSON_AddNumberToObject(json_response, "approved_evaluations", g_approved_evaluations);
        cJSON_AddNumberToObject(json_response, "declined_evaluations", g_declined_evaluations);
        cJSON_AddNumberToObject(json_response, "velocity_blocks", g_velocity_blocks);
        cJSON_AddNumberToObject(json_response, "amount_blocks", g_amount_blocks);
        cJSON_AddNumberToObject(json_response, "blacklist_blocks", g_blacklist_blocks);
        cJSON_AddNumberToObject(json_response, "timestamp", time(NULL));
        
        // Calculate approval rate
        if (g_total_evaluations > 0) {
            double approval_rate = (double)g_approved_evaluations / g_total_evaluations * 100.0;
            cJSON_AddNumberToObject(json_response, "approval_rate_percent", approval_rate);
        } else {
            cJSON_AddNumberToObject(json_response, "approval_rate_percent", 0.0);
        }
        
        pthread_mutex_unlock(&g_metrics_lock);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/risk/rules") == 0) {
        // Return current risk configuration
        json_response = cJSON_CreateObject();
        cJSON_AddNumberToObject(json_response, "max_amount", g_config.max_amount);
        cJSON_AddNumberToObject(json_response, "velocity_limit", g_config.velocity_limit);
        cJSON_AddNumberToObject(json_response, "velocity_window_sec", g_config.velocity_window_sec);
        
        cJSON *rules_array = cJSON_CreateArray();
        
        // Add hardcoded rules for demo
        cJSON *amount_rule = cJSON_CreateObject();
        cJSON_AddNumberToObject(amount_rule, "id", 1);
        cJSON_AddStringToObject(amount_rule, "name", "Amount Limit");
        cJSON_AddStringToObject(amount_rule, "type", "AMOUNT_LIMIT");
        cJSON_AddBoolToObject(amount_rule, "enabled", cJSON_True);
        cJSON_AddItemToArray(rules_array, amount_rule);
        
        cJSON *velocity_rule = cJSON_CreateObject();
        cJSON_AddNumberToObject(velocity_rule, "id", 2);
        cJSON_AddStringToObject(velocity_rule, "name", "Velocity Limit");
        cJSON_AddStringToObject(velocity_rule, "type", "VELOCITY");
        cJSON_AddBoolToObject(velocity_rule, "enabled", cJSON_True);
        cJSON_AddItemToArray(rules_array, velocity_rule);
        
        cJSON *blacklist_rule = cJSON_CreateObject();
        cJSON_AddNumberToObject(blacklist_rule, "id", 3);
        cJSON_AddStringToObject(blacklist_rule, "name", "BIN Blacklist");
        cJSON_AddStringToObject(blacklist_rule, "type", "BLACKLIST");
        cJSON_AddBoolToObject(blacklist_rule, "enabled", cJSON_True);
        cJSON_AddItemToArray(rules_array, blacklist_rule);
        
        cJSON_AddItemToObject(json_response, "rules", rules_array);
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
    g_config.port = getenv("PORT") ? atoi(getenv("PORT")) : 8081;
    g_config.max_amount = getenv("RISK_MAX_AMOUNT") ? atoi(getenv("RISK_MAX_AMOUNT")) : 10000;
    g_config.velocity_limit = getenv("RISK_VELOCITY_LIMIT") ? atoi(getenv("RISK_VELOCITY_LIMIT")) : 20;
    g_config.velocity_window_sec = getenv("RISK_VELOCITY_WINDOW") ? atoi(getenv("RISK_VELOCITY_WINDOW")) : 60;
    strncpy(g_config.db_uri,
            getenv("DB_URI") ? getenv("DB_URI") : "postgresql://localhost:5432/risk_db",
            sizeof(g_config.db_uri) - 1);
}

int main() {
    printf("Starting Risk Service...\\n");
    
    // Load configuration
    load_config();
    
    // Initialize velocity table
    memset(g_velocity_table, 0, sizeof(g_velocity_table));
    
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
    
    printf("Risk Service listening on port %d\\n", g_config.port);
    printf("Configuration:\\n");
    printf("  Max Amount: %d\\n", g_config.max_amount);
    printf("  Velocity Limit: %d transactions per %d seconds\\n", 
           g_config.velocity_limit, g_config.velocity_window_sec);
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        // Handle request
        handle_request(client_socket);
    }
    
    // Cleanup
    close(server_socket);
    return 0;
}