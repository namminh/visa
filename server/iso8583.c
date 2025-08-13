#include "iso8583.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// naive JSON "key":"value" or numeric extractor (local copy)
static int jget(const char *buf, const char *key, char *out, size_t outsz) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(buf, pattern);
    if (!p) return -1;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return -1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '"') {
        p++;
        const char *q = strchr(p, '"');
        if (!q) return -1;
        size_t len = (size_t)(q - p);
        if (len >= outsz) len = outsz - 1;
        memcpy(out, p, len);
        out[len] = '\0';
        return 0;
    }
    const char *q = p;
    while (*q && *q != ',' && *q != '}' && !isspace((unsigned char)*q)) q++;
    size_t len = (size_t)(q - p);
    if (len == 0) return -1;
    if (len >= outsz) len = outsz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

int iso_parse_request_line(const char *line, IsoRequest *out, char *err_reason, size_t errsz) {
    if (!line || !out) return -1;
    memset(out, 0, sizeof(*out));
    // defaults
    out->type = ISO_MSG_AUTH;
    // required fields
    if (jget(line, "pan", out->pan, sizeof(out->pan)) != 0) {
        if (err_reason && errsz) snprintf(err_reason, errsz, "missing_pan");
        return -1;
    }
    if (jget(line, "amount", out->amount_text, sizeof(out->amount_text)) != 0) {
        if (err_reason && errsz) snprintf(err_reason, errsz, "missing_amount");
        return -1;
    }
    // optional
    (void)jget(line, "currency", out->currency, sizeof(out->currency));
    (void)jget(line, "request_id", out->request_id, sizeof(out->request_id));
    char type_buf[32] = {0};
    if (jget(line, "type", type_buf, sizeof(type_buf)) == 0) {
        if (strcmp(type_buf, "AUTH") == 0) out->type = ISO_MSG_AUTH;
        else if (strcmp(type_buf, "CAPTURE") == 0) out->type = ISO_MSG_CAPTURE;
        else if (strcmp(type_buf, "REFUND") == 0) out->type = ISO_MSG_REFUND;
        else if (strcmp(type_buf, "REVERSAL") == 0) out->type = ISO_MSG_REVERSAL;
    }
    return 0;
}

