#pragma once

#include "db.h"

/**
 * Context passed to each connection handler job.
 */
typedef struct HandlerContext {
    int client_fd;        ///< Socket file descriptor
    DBConnection *db;     ///< Shared database connection
    const char *api_token;///< Optional API token for secure endpoints
} HandlerContext;

/**
 * Execute a single connection handler job.
 *
 * @param arg Pointer to HandlerContext (cast from void *)
 */
void handler_job(void *arg);
