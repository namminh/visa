#include "log.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

static void fmt_timestamp(char *buf, size_t n) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
    size_t len = strlen(buf);
    if (len + 4 < n) {
        // append .mmm
        int ms = (int)(ts.tv_nsec / 1000000);
        snprintf(buf + len, n - len, ".%03d", ms);
    }
}

void log_init(void) {
    // Simple stderr-based logger (suitable for tailing/redirecting)
    // No-op init for now
}

void log_message(const char *level, const char *msg) {
    char ts[32];
    fmt_timestamp(ts, sizeof(ts));
    fprintf(stderr, "%s %s %s\n", ts, level ? level : "INFO", msg ? msg : "");
}

void log_message_json(const char *level,
                      const char *event,
                      const char *request_id,
                      const char *status,
                      long latency_us) {
    char ts[32];
    fmt_timestamp(ts, sizeof(ts));
    fprintf(stderr, "{\"ts\":\"%s\",\"lvl\":\"%s\",\"event\":\"%s\"",
            ts,
            level ? level : "INFO",
            event ? event : "event");
    if (request_id && request_id[0]) {
        fprintf(stderr, ",\"request_id\":\"%s\"", request_id);
    }
    if (status && status[0]) {
        fprintf(stderr, ",\"status\":\"%s\"", status);
    }
    if (latency_us >= 0) {
        fprintf(stderr, ",\"latency_us\":%ld", latency_us);
    }
    fprintf(stderr, "}\n");
}

void log_close(void) {
    fflush(stderr);
}
