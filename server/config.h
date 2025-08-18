#pragma once

/**
 * Configuration structure for the mini‑visa server.
 *
 * This structure holds runtime configuration, such as the TCP
 * listen port and database URI. You can extend it as needed.
 */
typedef struct Config {
    const char *db_uri;  ///< PostgreSQL connection URI
    int listen_port;     ///< TCP port to listen on
    int num_threads;     ///< number of worker threads in the thread pool
    int queue_cap;       ///< max pending jobs in the thread pool
    const char *api_token; ///< optional bearer token for secure endpoints
} Config;

/**
 * Parse configuration from environment variables and/or command‑line arguments.
 *
 * @param cfg Pointer to the Config struct to populate
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non‑zero on error
 */
int config_init(Config *cfg, int argc, char *argv[]);

/**
 * Release resources associated with the configuration, if any.
 */
void config_free(Config *cfg);
