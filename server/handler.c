/* [ANCHOR:HANDLER_OVERVIEW]
 * Connection handler: reads one request, validates, writes one response.
 *
 * Processing flow (single request per TCP connection):
 * 1) Set socket timeouts to avoid hanging forever on slow/broken clients.
 * 2) Read JSON payload into a buffer.
 * 3) Parse minimal fields (pan, amount) with a tiny extractor (not a full JSON parser).
 * 4) Validate: PAN must pass Luhn; amount must be > 0 and <= 10000.
 * 5) Mask PAN (first 6 + last 4), then INSERT into DB using a per-thread connection.
 * 6) Write JSON response: {"status":"APPROVED"} or DECLINED with a reason.
 * 7) Close the socket and free the context.
 *
 * Notes:
 * - The tiny parser keeps the demo simple; replace with a real JSON library for production.
 * - db_thread_get() gives each worker thread its own PG connection for better concurrency.
 */
#include "handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "metrics.h"
#include "iso8583.h"
#include "risk.h"
#include "ledger.h"
#include "version.h"
#include "reversal.h"
#include "transaction_coordinator.h"
#include "db_participant.h"
#include "clearing_participant.h"
#include "db.h"

/*
 * Simple connection handler stub.
 *
 * In a full implementation you would read from the client_fd, parse
 * JSON or another protocol, validate fields (e.g. amount, PAN), mask
 * the PAN, begin a DB transaction, insert the row, commit or rollback
 * as appropriate, and write a response to the client. You would also
 * handle timeouts, retries, and logging. This stub simply logs a
 * message and closes the socket.
 */
// Luhn checksum for card number validity (returns 1 if valid, 0 otherwise)
static int luhn_check(const char *digits) {
    int sum = 0, alt = 0;
    size_t len = strlen(digits);
    for (ssize_t i = (ssize_t)len - 1; i >= 0; --i) {
        if (!isdigit((unsigned char)digits[i])) return 0;
        int d = digits[i] - '0';
        if (alt) {
            d *= 2;
            if (d > 9) d -= 9;
        }
        sum += d;
        alt = !alt;
    }
    return (sum % 10) == 0;
}

// Mask PAN: keep first 6 and last 4 digits, replace middle with '*'
static void mask_pan(const char *pan, char *out, size_t outsz) {
    size_t n = strlen(pan);
    if (outsz == 0) return;
    if (n <= 10) {
        // too short, copy as is
        snprintf(out, outsz, "%s", pan);
        return;
    }
    size_t prefix = 6, suffix = 4;
    if (prefix + suffix >= n) {
        snprintf(out, outsz, "%s", pan);
        return;
    }
    size_t stars = n - prefix - suffix;
    if (outsz < n + 1) {
        // truncate if needed
        snprintf(out, outsz, "%s", pan);
        return;
    }
    memcpy(out, pan, prefix);
    memset(out + prefix, '*', stars);
    memcpy(out + prefix + stars, pan + n - suffix, suffix);
    out[n] = '\0';
}

// Note: JSON field extraction moved to iso8583.c for normalization

// [ANCHOR:HANDLER_WRITE_ALL]
// Write the whole buffer, handling partial writes and EINTR
static int write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { continue; }
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

void handler_job(void *arg) {
    HandlerContext *ctx = (HandlerContext *)arg;
    if (!ctx) return;
    int fd = ctx->client_fd;
    
    // Initialize 2PC coordinator for this handler
    static __thread TransactionCoordinator *coordinator = NULL;
    if (!coordinator) {
        coordinator = txn_coordinator_init();
        if (!coordinator) {
            log_message_json("ERROR", "handler", NULL, "Failed to init 2PC coordinator", -1);
            close(fd);
            free(ctx);
            return;
        }
    }
    // [ANCHOR:HANDLER_TIMEOUTS]
    // 1) Set simple read/write timeouts to avoid hanging forever (keep-alive friendly)
    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    // [ANCHOR:HANDLER_BUFFER]
    // VN: Bộ đệm đọc theo dòng (newline-delimited). 8192 là giới hạn để chống DoS dòng quá dài.
    char buf[8192];
    size_t used = 0;

    // [ANCHOR:HANDLER_READ_LOOP]
    // Minimal HTTP request state for secure endpoint(s)
    int http_collecting = 0;           // inside HTTP header collection
    int http_is_secure_ping = 0;       // target path is /secure/ping
    int http_is_secure_tx = 0;         // target path is /secure/tx (POST JSON)
    char http_authz[256]; http_authz[0] = '\0';
    long http_content_length = -1;     // Content-Length for POST
    long http_body_remaining = 0;      // remaining bytes to read for body
    size_t http_body_used = 0;         // collected body bytes
    char http_body[8192];              // body buffer cap

    for (;;) {
        // Read more data
        ssize_t n = read(fd, buf + used, sizeof(buf) - 1 - used);
        if (n < 0) {
            if (errno == EINTR) continue;
            // timeout or error: close connection
            break;
        }
        if (n == 0) {
            // client closed
            break;
        }
        used += (size_t)n;
        buf[used] = '\0';

        // [ANCHOR:HANDLER_FRAMING]
        // Process complete lines (newline-delimited framing)
        char *start = buf;
        for (;;) {
            char *nl = memchr(start, '\n', (buf + used) - start);
            if (!nl) break;
            *nl = '\0';
            // Skip empty lines unless we are collecting HTTP headers
            const char *line = start;
            while (*line && isspace((unsigned char)*line)) line++;
            if (*line == '\0') {
                if (http_collecting) {
                    // End of headers: validate Authorization
                    int authorized = 0;
                    if (ctx->api_token && *ctx->api_token) {
                        const char *p = NULL;
                        // Expect: Authorization: Bearer <token>
                        if (http_authz[0] != '\0' && (p = strstr(http_authz, "Bearer ")) != NULL) {
                            p += 7; // after 'Bearer '
                            while (*p == ' ') p++;
                            if (strcmp(p, ctx->api_token) == 0) authorized = 1;
                        }
                    }
                    if (authorized && http_is_secure_ping) {
                        const char *body = "{\"ok\":true}\n";
                        char resp[256];
                        int blen = (int)strlen(body);
                        int m = snprintf(resp, sizeof(resp),
                                         "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                                         blen, body);
                        if (m > 0 && (size_t)m < sizeof(resp)) (void)write_all(fd, resp, (size_t)m);
                    } else if (!authorized && (http_is_secure_ping || http_is_secure_tx)) {
                        const char *body = "{\"error\":\"unauthorized\"}\n";
                        char resp[256];
                        int blen = (int)strlen(body);
                        int m = snprintf(resp, sizeof(resp),
                                         "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                                         blen, body);
                        if (m > 0 && (size_t)m < sizeof(resp)) (void)write_all(fd, resp, (size_t)m);
                    } else if (authorized && http_is_secure_tx) {
                        // Prepare to read body
                        if (http_content_length >= 0 && http_content_length <= (long)sizeof(http_body)) {
                            http_body_remaining = http_content_length; // begin body mode
                        } else {
                            const char *body = "{\"error\":\"bad_content_length\"}\n";
                            char resp[256];
                            int blen = (int)strlen(body);
                            int m = snprintf(resp, sizeof(resp),
                                             "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                                             blen, body);
                            if (m > 0 && (size_t)m < sizeof(resp)) (void)write_all(fd, resp, (size_t)m);
                            http_is_secure_tx = 0; // abort
                        }
                    }
                    http_collecting = 0; http_is_secure_ping = 0; http_authz[0] = '\0';
                }
                start = nl + 1;
                continue;
            }

            // Minimal HTTP secure endpoint parsing before other routes
            if (strncmp(line, "GET ", 4) == 0 && strstr(line, "HTTP/1.1") != NULL) {
                const char *p = line + 4;
                char path[128] = {0}; size_t i = 0;
                while (*p && *p != ' ' && i + 1 < sizeof(path)) path[i++] = *p++;
                path[i] = '\0';
                if (strcmp(path, "/secure/ping") == 0) {
                    http_collecting = 1; http_is_secure_ping = 1; http_is_secure_tx = 0; http_authz[0] = '\0'; http_content_length = -1;
                    start = nl + 1;
                    continue;
                }
            }
            if (strncmp(line, "POST ", 5) == 0 && strstr(line, "HTTP/1.1") != NULL) {
                const char *p = line + 5;
                char path[128] = {0}; size_t i = 0;
                while (*p && *p != ' ' && i + 1 < sizeof(path)) path[i++] = *p++;
                path[i] = '\0';
                if (strcmp(path, "/secure/tx") == 0) {
                    http_collecting = 1; http_is_secure_ping = 0; http_is_secure_tx = 1; http_authz[0] = '\0';
                    http_content_length = -1; http_body_remaining = 0; http_body_used = 0;
                    start = nl + 1;
                    continue;
                }
            }
            if (http_collecting) {
                // Header line parsing; look for Authorization
                if (strncasecmp(line, "Authorization:", 14) == 0) {
                    const char *v = line + 14;
                    while (*v == ' ' || *v == '\t') v++;
                    snprintf(http_authz, sizeof(http_authz), "%s", v);
                } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
                    const char *v = line + 15;
                    while (*v == ' ' || *v == '\t') v++;
                    http_content_length = strtol(v, NULL, 10);
                }
                start = nl + 1;
                continue;
            }

            // [ANCHOR:HANDLER_HEALTH_READY_METRICS]
            // Health/Ready/Metrics simple GET handling per line
            if (strncmp(line, "GET /healthz", 12) == 0) {
                const char *ok = "OK\n";
                (void)write_all(fd, ok, strlen(ok));
                log_message_json("INFO", "healthz", NULL, "OK", -1);
                start = nl + 1;
                continue;
            }
            if (strncmp(line, "GET /readyz", 11) == 0) {
                DBConnection *dbc_ready = db_thread_get(ctx->db);
                const int ready = db_is_ready(dbc_ready);
                const char *r = ready ? "OK\n" : "NOT_READY\n";
                (void)write_all(fd, r, strlen(r));
                log_message_json("INFO", "readyz", NULL, ready ? "OK" : "NOT_READY", -1);
                start = nl + 1;
                continue;
            }
            if (strncmp(line, "GET /metrics", 12) == 0) {
                unsigned long t=0,a=0,d=0,b=0; metrics_snapshot(&t,&a,&d,&b);
                unsigned long rd = metrics_get_risk_declined();
                unsigned long cmt = metrics_get_2pc_committed();
                unsigned long abt = metrics_get_2pc_aborted();
                unsigned long cbsc = metrics_get_cb_short_circuit();
                unsigned long renq = metrics_get_reversal_enqueued();
                unsigned long rokn = metrics_get_reversal_succeeded();
                unsigned long rfail = metrics_get_reversal_failed();
                char m[640];
                int mlen = snprintf(m, sizeof(m),
                                    "{\"total\":%lu,\"approved\":%lu,\"declined\":%lu,\"server_busy\":%lu,\"risk_declined\":%lu,\"twopc_committed\":%lu,\"twopc_aborted\":%lu,\"clearing_cb_short_circuit\":%lu,\"reversal_enqueued\":%lu,\"reversal_succeeded\":%lu,\"reversal_failed\":%lu}\n",
                                    t,a,d,b,rd,cmt,abt,cbsc,renq,rokn,rfail);
                if (mlen > 0 && (size_t)mlen < sizeof(m)) (void)write_all(fd, m, (size_t)mlen);
                log_message_json("INFO", "metrics", NULL, "SNAPSHOT", -1);
                start = nl + 1;
                continue;
            }
            if (strncmp(line, "GET /tx?", 8) == 0) {
                // Very simple query parser: expect request_id in query string
                const char *q = strstr(line, "request_id=");
                char rid[128] = {0};
                if (q) {
                    q += strlen("request_id=");
                    size_t i = 0;
                    while (*q && *q != ' ' && *q != '\r' && *q != '\n' && *q != '&' && i + 1 < sizeof(rid)) {
                        rid[i++] = *q++;
                    }
                    rid[i] = '\0';
                }

                if (rid[0] == '\0') {
                    const char *resp = "{\"error\":\"missing_request_id\"}\n";
                    (void)write_all(fd, resp, strlen(resp));
                    start = nl + 1;
                    continue;
                }

                DBConnection *dbc_q = db_thread_get(ctx->db);
                char json[256];
                if (db_get_tx_by_request_id(dbc_q, rid, json, sizeof(json)) == 0) {
                    (void)write_all(fd, json, strlen(json));
                } else {
                    const char *nf = "{\"status\":\"NOT_FOUND\"}\n";
                    (void)write_all(fd, nf, strlen(nf));
                }
                start = nl + 1;
                continue;
            }
            if (strncmp(line, "GET /version", 12) == 0) {
                char body[128];
                int n = snprintf(body, sizeof(body),
                                 "{\"version\":\"%s\",\"schema\":%d}\n",
                                 MINI_VISA_VERSION, MINI_VISA_SCHEMA_VERSION);
                if (n > 0 && (size_t)n < sizeof(body)) (void)write_all(fd, body, (size_t)n);
                log_message_json("INFO", "version", NULL, MINI_VISA_VERSION, -1);
                start = nl + 1;
                continue;
            }

            // [ANCHOR:HANDLER_PARSE_VALIDATE] Process one JSON request in 'line'
            struct timeval t0, t1; gettimeofday(&t0, NULL);
            metrics_inc_total();
            IsoRequest req;
            char perr[64] = {0};
            if (iso_parse_request_line(line, &req, perr, sizeof(perr)) != 0) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"bad_request\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("WARN", "tx", NULL, "DECLINED", -1);
                start = nl + 1;
                continue;
            }
            const char *request_id = req.request_id;

            // [ANCHOR:HANDLER_LUHN]
            if (!luhn_check(req.pan)) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"luhn_failed\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                metrics_inc_risk_declined();
                log_message_json("WARN", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }
            // [ANCHOR:HANDLER_AMOUNT]
            double amt = atof(req.amount_text);
            if (!(amt > 0.0) || amt > 10000.0) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"amount_invalid\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("WARN", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }

            // [ANCHOR:HANDLER_RISK] Risk engine (stub): currently always allow; placeholder for future rules
            RiskDecision rdec; risk_evaluate(&req, &rdec);
            if (!rdec.allow) {
                char body[128];
                int n = snprintf(body, sizeof(body),
                                 "{\"status\":\"DECLINED\",\"reason\":\"%s\"}\n",
                                 rdec.reason[0] ? rdec.reason : "risk_decline");
                if (n > 0 && (size_t)n < sizeof(body)) (void)write_all(fd, body, (size_t)n);
                metrics_inc_declined();
                log_message_json("WARN", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }

            char masked[64];
            mask_pan(req.pan, masked, sizeof(masked));
            
            // [ANCHOR:HANDLER_2PC] Use 2-Phase Commit for distributed transaction
            
            // Generate unique transaction ID
            char txn_id[MAX_TRANSACTION_ID_LEN];
            snprintf(txn_id, sizeof(txn_id), "visa_%s_%ld", request_id, time(NULL));
            
            // Begin distributed transaction
            Transaction *txn = txn_begin(coordinator, txn_id);
            if (!txn) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"txn_init_failed\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("ERROR", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }
            
            // Initialize participants
            DBConnection *dbc = db_thread_get(ctx->db);
            DBParticipantContext *db_ctx = db_participant_init(dbc);
            ClearingParticipantContext *clearing_ctx = clearing_participant_init(NULL, 30);
            
            if (!db_ctx || !clearing_ctx) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"participant_init_failed\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("ERROR", "tx", request_id, "DECLINED", -1);
                
                if (db_ctx) db_participant_destroy(db_ctx);
                if (clearing_ctx) clearing_participant_destroy(clearing_ctx);
                txn_abort(coordinator, txn);
                start = nl + 1;
                continue;
            }
            
            // Register participants in transaction
            if (txn_register_participant(txn, "database", db_ctx,
                                       db_participant_prepare,
                                       db_participant_commit,
                                       db_participant_abort) != 0 ||
                txn_register_participant(txn, "clearing", clearing_ctx,
                                       clearing_participant_prepare,
                                       clearing_participant_commit,
                                       clearing_participant_abort) != 0) {
                
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"participant_registration_failed\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("ERROR", "tx", request_id, "DECLINED", -1);
                
                db_participant_destroy(db_ctx);
                clearing_participant_destroy(clearing_ctx);
                txn_abort(coordinator, txn);
                start = nl + 1;
                continue;
            }
            
            // Prepare transaction data
            if (db_participant_begin(db_ctx, txn_id) != 0) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"db_begin_failed\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("ERROR", "tx", request_id, "DECLINED", -1);
                
                db_participant_destroy(db_ctx);
                clearing_participant_destroy(clearing_ctx);
                txn_abort(coordinator, txn);
                start = nl + 1;
                continue;
            }
            
            if (clearing_participant_set_transaction(clearing_ctx, txn_id, masked, 
                                                    req.amount_text, "MERCHANT001") != 0) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"clearing_setup_failed\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("ERROR", "tx", request_id, "DECLINED", -1);
                
                db_participant_destroy(db_ctx);
                clearing_participant_destroy(clearing_ctx);
                txn_abort(coordinator, txn);
                start = nl + 1;
                continue;
            }
            
            // Execute database operations within transaction
            int is_dup = 0;
            char db_status[32] = {0};
            if (db_participant_insert_transaction(db_ctx, request_id, masked, 
                                                req.amount_text, "APPROVED", 
                                                &is_dup, db_status, sizeof(db_status)) != 0) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"db_error\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("ERROR", "tx", request_id, "DECLINED", -1);
                
                db_participant_destroy(db_ctx);
                clearing_participant_destroy(clearing_ctx);
                txn_abort(coordinator, txn);
                start = nl + 1;
                continue;
            }
            
            // Execute 2-Phase Commit
            int commit_result = txn_commit(coordinator, txn);
            
            // Cleanup participants
            db_participant_destroy(db_ctx);
            clearing_participant_destroy(clearing_ctx);
            
            if (commit_result == 0) {
                // Success
                const char *ok_body = is_dup
                    ? "{\"status\":\"APPROVED\",\"idempotent\":true,\"txn_id\":\"%s\"}\n"
                    : "{\"status\":\"APPROVED\",\"txn_id\":\"%s\"}\n";
                char response[256];
                snprintf(response, sizeof(response), ok_body, txn_id);
                (void)write_all(fd, response, strlen(response));
                metrics_inc_approved();
            } else {
                // 2PC failed
                // Best-effort enqueue reversal to clear any external holds/charges
                (void)reversal_enqueue(txn_id, masked, req.amount_text, "MERCHANT001");
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"commit_failed\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("ERROR", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }
            // [ANCHOR:HANDLER_LATENCY_LOG] Tính latency và log JSON một dòng
            gettimeofday(&t1, NULL);
            long latency_us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
            log_message_json("INFO", "tx", request_id, "APPROVED", latency_us);

            start = nl + 1;
        }
        // If collecting HTTP POST body, consume raw bytes before normal framing move
        if (http_body_remaining > 0) {
            // 'start' points to the first unprocessed byte
            char *raw_start = start;
            size_t avail = (size_t)((buf + used) - raw_start);
            if (avail > 0) {
                size_t take = avail;
                if ((long)take > http_body_remaining) take = (size_t)http_body_remaining;
                if (http_body_used + take <= sizeof(http_body)) {
                    memcpy(http_body + http_body_used, raw_start, take);
                    http_body_used += take;
                    http_body_remaining -= (long)take;
                } else {
                    // overflow safeguard, drop
                    http_body_remaining = 0;
                }
                raw_start += take;
                // Shift remainder down into buffer
                size_t remain2 = (buf + used) - raw_start;
                if (remain2 > 0 && raw_start != buf) memmove(buf, raw_start, remain2);
                used = remain2;
            }
            if (http_body_remaining == 0 && http_is_secure_tx) {
                // NUL-terminate body
                size_t body_len = http_body_used < sizeof(http_body) ? http_body_used : sizeof(http_body) - 1;
                http_body[body_len] = '\0';

                // Process JSON body with existing transaction pipeline (inline)
                struct timeval t0, t1; gettimeofday(&t0, NULL);
                metrics_inc_total();
                IsoRequest req;
                char perr[64] = {0};
                char body_json[512]; body_json[0] = '\0';
                int http_code = 200; const char *http_reason = "OK";
                if (iso_parse_request_line(http_body, &req, perr, sizeof(perr)) != 0) {
                    snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"bad_request\"}\n");
                    metrics_inc_declined(); http_code = 400; http_reason = "Bad Request";
                } else if (!luhn_check(req.pan)) {
                    snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"luhn_failed\"}\n");
                    metrics_inc_declined(); metrics_inc_risk_declined(); http_code = 400; http_reason = "Bad Request";
                } else {
                    double amt = atof(req.amount_text);
                    if (!(amt > 0.0) || amt > 10000.0) {
                        snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"amount_invalid\"}\n");
                        metrics_inc_declined(); http_code = 400; http_reason = "Bad Request";
                    } else {
                        RiskDecision rdec; risk_evaluate(&req, &rdec);
                        if (!rdec.allow) {
                            snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"%s\"}\n", rdec.reason[0] ? rdec.reason : "risk_decline");
                            metrics_inc_declined(); http_code = 402; http_reason = "Payment Required";
                        } else {
                            char masked[64]; mask_pan(req.pan, masked, sizeof(masked));
                            char txn_id[MAX_TRANSACTION_ID_LEN]; snprintf(txn_id, sizeof(txn_id), "visa_%s_%ld", req.request_id, time(NULL));
                            Transaction *txn = txn_begin(coordinator, txn_id);
                            if (!txn) {
                                snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"txn_init_failed\"}\n");
                                metrics_inc_declined(); http_code = 500; http_reason = "Internal Server Error";
                            } else {
                                DBConnection *dbc = db_thread_get(ctx->db);
                                DBParticipantContext *db_ctx = db_participant_init(dbc);
                                ClearingParticipantContext *clearing_ctx = clearing_participant_init(NULL, 30);
                                if (!db_ctx || !clearing_ctx) {
                                    snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"participant_init_failed\"}\n");
                                    metrics_inc_declined(); http_code = 500; http_reason = "Internal Server Error";
                                    if (db_ctx) db_participant_destroy(db_ctx);
                                    if (clearing_ctx) clearing_participant_destroy(clearing_ctx);
                                    txn_abort(coordinator, txn);
                                } else if (txn_register_participant(txn, "database", db_ctx, db_participant_prepare, db_participant_commit, db_participant_abort) != 0 ||
                                           txn_register_participant(txn, "clearing", clearing_ctx, clearing_participant_prepare, clearing_participant_commit, clearing_participant_abort) != 0) {
                                    snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"participant_registration_failed\"}\n");
                                    metrics_inc_declined(); http_code = 500; http_reason = "Internal Server Error";
                                    db_participant_destroy(db_ctx); clearing_participant_destroy(clearing_ctx); txn_abort(coordinator, txn);
                                } else if (db_participant_begin(db_ctx, txn_id) != 0) {
                                    snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"db_begin_failed\"}\n");
                                    metrics_inc_declined(); http_code = 500; http_reason = "Internal Server Error";
                                    db_participant_destroy(db_ctx); clearing_participant_destroy(clearing_ctx); txn_abort(coordinator, txn);
                                } else if (clearing_participant_set_transaction(clearing_ctx, txn_id, masked, req.amount_text, "MERCHANT001") != 0) {
                                    snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"clearing_setup_failed\"}\n");
                                    metrics_inc_declined(); http_code = 502; http_reason = "Bad Gateway";
                                    db_participant_destroy(db_ctx); clearing_participant_destroy(clearing_ctx); txn_abort(coordinator, txn);
                                } else {
                                    int is_dup = 0; char db_status[32] = {0};
                                    if (db_participant_insert_transaction(db_ctx, req.request_id, masked, req.amount_text, "APPROVED", &is_dup, db_status, sizeof(db_status)) != 0) {
                                        snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"db_error\"}\n");
                                        metrics_inc_declined(); http_code = 500; http_reason = "Internal Server Error";
                                        db_participant_destroy(db_ctx); clearing_participant_destroy(clearing_ctx); txn_abort(coordinator, txn);
                                    } else {
                                        int commit_result = txn_commit(coordinator, txn);
                                        db_participant_destroy(db_ctx); clearing_participant_destroy(clearing_ctx);
                                        if (commit_result == 0) {
                                            if (is_dup) snprintf(body_json, sizeof(body_json), "{\"status\":\"APPROVED\",\"idempotent\":true,\"txn_id\":\"%s\"}\n", txn_id);
                                            else snprintf(body_json, sizeof(body_json), "{\"status\":\"APPROVED\",\"txn_id\":\"%s\"}\n", txn_id);
                                            metrics_inc_approved();
                                        } else {
                                            (void)reversal_enqueue(txn_id, masked, req.amount_text, "MERCHANT001");
                                            snprintf(body_json, sizeof(body_json), "{\"status\":\"DECLINED\",\"reason\":\"commit_failed\"}\n");
                                            metrics_inc_declined(); http_code = 500; http_reason = "Internal Server Error";
                                        }
                                    }
                                }
                            }
                        }
                    }

                }

                // emit HTTP response
                if (body_json[0] != '\0') {
                    size_t bl = strlen(body_json);
                    char hdr[256];
                    int m = snprintf(hdr, sizeof(hdr),
                                     "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                                     http_code, http_reason, bl);
                    if (m > 0 && (size_t)m < sizeof(hdr)) {
                        (void)write_all(fd, hdr, (size_t)m);
                        (void)write_all(fd, body_json, bl);
                    }
                }

                // reset http body state
                http_is_secure_tx = 0; http_body_used = 0; http_content_length = -1; // body_remaining already 0
                continue;
            }

        // [ANCHOR:HANDLER_PARTIAL_REMAINDER]
        // Move remaining partial data to the front (giữ phần chưa hoàn thành ở đầu buffer)
        size_t remain = (buf + used) - start;
        if (remain > 0 && start != buf) memmove(buf, start, remain);
        used = remain;
        if (used == sizeof(buf) - 1) {
            // Too long line; reset buffer to avoid overflow
            used = 0;
        }
    }
    }
    
    // [ANCHOR:HANDLER_CLOSE] Đóng socket và giải phóng context
    if (fd >= 0) close(fd);
    free(ctx);
    
    // Note: coordinator is thread-local and will be cleaned up when thread exits
}
