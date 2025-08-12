#pragma once

/**
 * Initialize logging. In a real implementation you might
 * open a file or set up a ring buffer. For now this is a stub.
 */
void log_init(void);

/**
 * Log a message at the given severity level. Stubs do nothing.
 *
 * @param level Severity level (e.g. "INFO", "ERROR")
 * @param msg The message to log
 */
void log_message(const char *level, const char *msg);

/**
 * Log a single JSON line with structured fields for transactions/events.
 * Fields: ts, lvl, event, request_id (optional), status (optional), latency_us (>=0 means present).
 */
void log_message_json(const char *level,
                      const char *event,
                      const char *request_id,
                      const char *status,
                      long latency_us);

/**
 * Flush and clean up logging resources.
 */
void log_close(void);
