#include "metrics.h"
#include <stdint.h>

// Use GCC built-ins for atomic increments (portable enough for this project)
static volatile unsigned long g_total = 0;
static volatile unsigned long g_approved = 0;
static volatile unsigned long g_declined = 0;
static volatile unsigned long g_server_busy = 0;

void metrics_init(void) {
    g_total = g_approved = g_declined = g_server_busy = 0;
}

void metrics_inc_total(void) { __sync_fetch_and_add(&g_total, 1); }
void metrics_inc_approved(void) { __sync_fetch_and_add(&g_approved, 1); }
void metrics_inc_declined(void) { __sync_fetch_and_add(&g_declined, 1); }
void metrics_inc_server_busy(void) { __sync_fetch_and_add(&g_server_busy, 1); }

void metrics_snapshot(unsigned long *total,
                      unsigned long *approved,
                      unsigned long *declined,
                      unsigned long *server_busy) {
    if (total) *total = g_total;
    if (approved) *approved = g_approved;
    if (declined) *declined = g_declined;
    if (server_busy) *server_busy = g_server_busy;
}

