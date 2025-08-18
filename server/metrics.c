#include "metrics.h"
#include <stdint.h>

// Use GCC built-ins for atomic increments (portable enough for this project)
static volatile unsigned long g_total = 0;
static volatile unsigned long g_approved = 0;
static volatile unsigned long g_declined = 0;
static volatile unsigned long g_server_busy = 0;
static volatile unsigned long g_risk_declined = 0;
static volatile unsigned long g_2pc_committed = 0;
static volatile unsigned long g_2pc_aborted = 0;
static volatile unsigned long g_cb_short_circuit = 0;
static volatile unsigned long g_rev_enq = 0;
static volatile unsigned long g_rev_ok = 0;
static volatile unsigned long g_rev_fail = 0;

void metrics_init(void) {
    g_total = g_approved = g_declined = g_server_busy = g_risk_declined = 0;
    g_2pc_committed = g_2pc_aborted = g_cb_short_circuit = 0;
    g_rev_enq = g_rev_ok = g_rev_fail = 0;
}

void metrics_inc_total(void) { __sync_fetch_and_add(&g_total, 1); }
void metrics_inc_approved(void) { __sync_fetch_and_add(&g_approved, 1); }
void metrics_inc_declined(void) { __sync_fetch_and_add(&g_declined, 1); }
void metrics_inc_server_busy(void) { __sync_fetch_and_add(&g_server_busy, 1); }
void metrics_inc_risk_declined(void) { __sync_fetch_and_add(&g_risk_declined, 1); }
void metrics_inc_2pc_committed(void) { __sync_fetch_and_add(&g_2pc_committed, 1); }
void metrics_inc_2pc_aborted(void) { __sync_fetch_and_add(&g_2pc_aborted, 1); }
void metrics_inc_cb_short_circuit(void) { __sync_fetch_and_add(&g_cb_short_circuit, 1); }
void metrics_inc_reversal_enqueued(void) { __sync_fetch_and_add(&g_rev_enq, 1); }
void metrics_inc_reversal_succeeded(void) { __sync_fetch_and_add(&g_rev_ok, 1); }
void metrics_inc_reversal_failed(void) { __sync_fetch_and_add(&g_rev_fail, 1); }

void metrics_snapshot(unsigned long *total,
                      unsigned long *approved,
                      unsigned long *declined,
                      unsigned long *server_busy) {
    if (total) *total = g_total;
    if (approved) *approved = g_approved;
    if (declined) *declined = g_declined;
    if (server_busy) *server_busy = g_server_busy;
}

unsigned long metrics_get_risk_declined(void) { return g_risk_declined; }
unsigned long metrics_get_2pc_committed(void) { return g_2pc_committed; }
unsigned long metrics_get_2pc_aborted(void) { return g_2pc_aborted; }
unsigned long metrics_get_cb_short_circuit(void) { return g_cb_short_circuit; }
unsigned long metrics_get_reversal_enqueued(void) { return g_rev_enq; }
unsigned long metrics_get_reversal_succeeded(void) { return g_rev_ok; }
unsigned long metrics_get_reversal_failed(void) { return g_rev_fail; }
