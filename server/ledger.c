#include "ledger.h"

void ledger_init(void) {
    // In-memory/no-op init for now
}

int ledger_authorize_hold(const IsoRequest *req) {
    (void)req;
    // In a real system, write double-entry pending hold; here we no-op
    return 0;
}

int ledger_capture(const IsoRequest *req) {
    (void)req;
    return 0;
}

int ledger_refund(const IsoRequest *req) {
    (void)req;
    return 0;
}

int ledger_reversal(const IsoRequest *req) {
    (void)req;
    return 0;
}

