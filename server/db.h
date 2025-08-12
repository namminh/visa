#pragma once

#include <libpq-fe.h>

typedef struct DBConnection DBConnection;

/**
 * Connect to PostgreSQL using the provided URI.
 *
 * @param uri Connection string
 * @return Pointer to DBConnection or NULL on error
 */
DBConnection *db_connect(const char *uri);

/**
 * Insert a new transaction into the database.
 *
 * This is a simplified interface. In a real implementation you
 * might pass a structured transaction type. Here we simply pass
 * strings and amounts.
 *
 * @param dbc Database connection
 * @param pan_masked Masked PAN (primary account number)
 * @param amount Transaction amount as text
 * @param status Transaction status (e.g. "OK" or "ERROR")
 * @return 0 on success, non‑zero on error
 */
int db_insert_transaction(DBConnection *dbc, const char *pan_masked, const char *amount, const char *status);

/**
 * Close the database connection and free resources.
 *
 * @param dbc Database connection
 */
void db_disconnect(DBConnection *dbc);

/**
 * Get a thread-local DB connection based on a bootstrap connection.
 * The first time this is called in a thread, a new connection will be
 * created using the same URI as the bootstrap. Subsequent calls return
 * the same connection for that thread.
 */
DBConnection *db_thread_get(DBConnection *bootstrap);

/**
 * Idempotent insert based on request_id. If request_id already exists,
 * no new row is created and the existing status is returned via out_status.
 * If request_id is NULL/empty, behaves like a normal insert.
 *
 * @param dbc Database connection
 * @param request_id Optional unique request id
 * @param pan_masked Masked PAN
 * @param amount Amount as text
 * @param status Desired status to set on new insert
 * @param out_is_dup Optional; set to 1 if duplicate id encountered, else 0
 * @param out_status Buffer to receive resulting status (existing or inserted)
 * @param out_status_sz Size of out_status buffer
 * @return 0 on success, non-zero on error
 */
int db_insert_or_get_by_reqid(DBConnection *dbc,
                              const char *request_id,
                              const char *pan_masked,
                              const char *amount,
                              const char *status,
                              int *out_is_dup,
                              char *out_status,
                              size_t out_status_sz);

/**
 * Check if the DB connection is ready (CONNECTION_OK).
 */
int db_is_ready(DBConnection *dbc);
