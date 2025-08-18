#pragma once

#include <stddef.h>

// Simple counters for observability
void metrics_init(void);
void metrics_inc_total(void);
void metrics_inc_approved(void);
void metrics_inc_declined(void);
void metrics_inc_server_busy(void);
void metrics_inc_risk_declined(void);
void metrics_inc_2pc_committed(void);
void metrics_inc_2pc_aborted(void);
void metrics_inc_cb_short_circuit(void);
void metrics_inc_reversal_enqueued(void);
void metrics_inc_reversal_succeeded(void);
void metrics_inc_reversal_failed(void);

// Snapshot counters into provided pointers (can be NULL to skip)
void metrics_snapshot(unsigned long *total,
                      unsigned long *approved,
                      unsigned long *declined,
                      unsigned long *server_busy);

// Optional: get risk declined count (not included in snapshot to keep API stable)
unsigned long metrics_get_risk_declined(void);
unsigned long metrics_get_2pc_committed(void);
unsigned long metrics_get_2pc_aborted(void);
unsigned long metrics_get_cb_short_circuit(void);
unsigned long metrics_get_reversal_enqueued(void);
unsigned long metrics_get_reversal_succeeded(void);
unsigned long metrics_get_reversal_failed(void);
