#pragma once

#include <stddef.h>

// Simple counters for observability
void metrics_init(void);
void metrics_inc_total(void);
void metrics_inc_approved(void);
void metrics_inc_declined(void);
void metrics_inc_server_busy(void);
void metrics_inc_risk_declined(void);

// Snapshot counters into provided pointers (can be NULL to skip)
void metrics_snapshot(unsigned long *total,
                      unsigned long *approved,
                      unsigned long *declined,
                      unsigned long *server_busy);

// Optional: get risk declined count (not included in snapshot to keep API stable)
unsigned long metrics_get_risk_declined(void);
