#pragma once

#include "iso8583.h"

typedef struct RiskDecision {
    int allow;           // 1 = allow, 0 = decline
    char reason[64];     // reason code if declined
} RiskDecision;

void risk_init(void);

// Evaluate a request with simple rules (stub). Always allow for now.
void risk_evaluate(const IsoRequest *req, RiskDecision *out);

