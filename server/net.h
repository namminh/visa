#pragma once

#include <stddef.h>
struct Config;
struct ThreadPool;
struct DBConnection;

/**
 * Start the network listener on the configured port and dispatch incoming
 * connections to the thread pool. This function blocks until the server
 * terminates or an error occurs.
 *
 * @param cfg The server configuration (provides listen_port)
 * @param pool A pointer to the thread pool to dispatch work to
 * @return 0 on success, non‑zero on error
 */
int net_server_run(const struct Config *cfg, struct ThreadPool *pool, struct DBConnection *dbc);
