#pragma once

#include <stddef.h>

// Minimal internal representation of an authorization-like request
typedef enum {
    ISO_MSG_AUTH = 0,
    ISO_MSG_CAPTURE = 1,
    ISO_MSG_REFUND = 2,
    ISO_MSG_REVERSAL = 3
} IsoMsgType;

typedef struct IsoRequest {
    char pan[64];
    char amount_text[64];
    char currency[8];
    char request_id[128];
    IsoMsgType type; // derived from processing code or JSON field `type`
} IsoRequest;

// Parse one newline-delimited JSON line into IsoRequest (subset fields only).
// Returns 0 on success, non-zero on parse/validation error. On error, err_reason may be set.
int iso_parse_request_line(const char *line, IsoRequest *out, char *err_reason, size_t errsz);

