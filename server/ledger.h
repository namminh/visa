#pragma once

#include "iso8583.h"

void ledger_init(void);

// Record an authorization hold (stub). Returns 0 on success.
int ledger_authorize_hold(const IsoRequest *req);

// Other operations (not used yet): capture, refund, reversal
int ledger_capture(const IsoRequest *req);
int ledger_refund(const IsoRequest *req);
int ledger_reversal(const IsoRequest *req);

