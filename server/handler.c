/*
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

// Very naive JSON field extractor: finds "key":"value" or numeric value
static int parse_field(const char *buf, const char *key, char *out, size_t outsz) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(buf, pattern);
    if (!p) return -1;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return -1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '"') {
        p++;
        const char *q = strchr(p, '"');
        if (!q) return -1;
        size_t len = (size_t)(q - p);
        if (len >= outsz) len = outsz - 1;
        memcpy(out, p, len);
        out[len] = '\0';
        return 0;
    }
    // number as plain text until delimiter
    const char *q = p;
    while (*q && *q != ',' && *q != '}' && !isspace((unsigned char)*q)) q++;
    size_t len = (size_t)(q - p);
    if (len == 0) return -1;
    if (len >= outsz) len = outsz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

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
    // 1) Set simple read/write timeouts to avoid hanging forever (keep-alive friendly)
    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char buf[8192];
    size_t used = 0;

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

        // Process complete lines (newline-delimited framing)
        char *start = buf;
        for (;;) {
            char *nl = memchr(start, '\n', (buf + used) - start);
            if (!nl) break;
            *nl = '\0';
            // Skip empty lines
            const char *line = start;
            while (*line && isspace((unsigned char)*line)) line++;
            if (*line == '\0') {
                start = nl + 1;
                continue;
            }

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
                char m[256];
                int mlen = snprintf(m, sizeof(m),
                                    "{\"total\":%lu,\"approved\":%lu,\"declined\":%lu,\"server_busy\":%lu}\n",
                                    t,a,d,b);
                if (mlen > 0 && (size_t)mlen < sizeof(m)) (void)write_all(fd, m, (size_t)mlen);
                log_message_json("INFO", "metrics", NULL, "SNAPSHOT", -1);
                start = nl + 1;
                continue;
            }

            // Process one JSON request in 'line'
            struct timeval t0, t1; gettimeofday(&t0, NULL);
            metrics_inc_total();
            char pan[64] = {0}, amount[64] = {0};
            char request_id[128] = {0};
            if (parse_field(line, "pan", pan, sizeof(pan)) != 0 ||
                parse_field(line, "amount", amount, sizeof(amount)) != 0) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"bad_request\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("WARN", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }
            (void)parse_field(line, "request_id", request_id, sizeof(request_id));

            if (!luhn_check(pan)) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"luhn_failed\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("WARN", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }
            double amt = atof(amount);
            if (!(amt > 0.0) || amt > 10000.0) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"amount_invalid\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("WARN", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }

            char masked[64];
            mask_pan(pan, masked, sizeof(masked));
            DBConnection *dbc = db_thread_get(ctx->db);
            int is_dup = 0;
            char db_status[32] = {0};
            if (db_insert_or_get_by_reqid(dbc, request_id, masked, amount, "APPROVED", &is_dup, db_status, sizeof(db_status)) != 0) {
                const char *resp = "{\"status\":\"DECLINED\",\"reason\":\"db_error\"}\n";
                (void)write_all(fd, resp, strlen(resp));
                metrics_inc_declined();
                log_message_json("ERROR", "tx", request_id, "DECLINED", -1);
                start = nl + 1;
                continue;
            }
            const char *ok_body = is_dup
                ? "{\"status\":\"APPROVED\",\"idempotent\":true}\n"
                : "{\"status\":\"APPROVED\"}\n";
            (void)write_all(fd, ok_body, strlen(ok_body));
            metrics_inc_approved();
            gettimeofday(&t1, NULL);
            long latency_us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
            log_message_json("INFO", "tx", request_id, "APPROVED", latency_us);

            start = nl + 1;
        }
        // Move remaining partial data to the front
        size_t remain = (buf + used) - start;
        if (remain > 0 && start != buf) memmove(buf, start, remain);
        used = remain;
        if (used == sizeof(buf) - 1) {
            // Too long line; reset buffer to avoid overflow
            used = 0;
        }
    }

    if (fd >= 0) close(fd);
    free(ctx);
}
