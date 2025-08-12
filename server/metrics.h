#pragma once

#include <stddef.h>

// Simple counters for observability
void metrics_init(void);
void metrics_inc_total(void);
void metrics_inc_approved(void);
void metrics_inc_declined(void);
void metrics_inc_server_busy(void);

// Snapshot counters into provided pointers (can be NULL to skip)
void metrics_snapshot(unsigned long *total,
                      unsigned long *approved,
                      unsigned long *declined,
                      unsigned long *server_busy);

