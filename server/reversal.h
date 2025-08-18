#pragma once

#include <stddef.h>

// Initialize background reversal worker
int reversal_init(void);

// Enqueue a best-effort reversal/void for a transaction
int reversal_enqueue(const char *txn_id,
                     const char *pan_masked,
                     const char *amount,
                     const char *merchant_id);

// Shutdown and drain worker
void reversal_shutdown(void);

