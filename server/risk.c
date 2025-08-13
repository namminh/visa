#include "risk.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

// Simple velocity limiter per PAN within a sliding window
typedef struct VelEntry {
    char pan[64];
    time_t window_start;
    int count;
} VelEntry;

#define VEL_CAP 1024
static VelEntry g_vel[VEL_CAP];
static int g_risk_enabled = 1;
static int g_vel_limit = 20;           // default max requests per window
static int g_vel_window_sec = 60;      // default window duration

void risk_init(void) {
    const char *en = getenv("RISK_ENABLED");
    if (en) g_risk_enabled = atoi(en) != 0;
    const char *lim = getenv("RISK_VEL_LIMIT");
    if (lim) { int v = atoi(lim); if (v > 0) g_vel_limit = v; }
    const char *win = getenv("RISK_VEL_WINDOW_SEC");
    if (win) { int v = atoi(win); if (v > 0) g_vel_window_sec = v; }
    memset(g_vel, 0, sizeof(g_vel));
}

static VelEntry *vel_get(const char *pan) {
    // linear probe (small cap); replace oldest window if needed
    time_t now = time(NULL);
    int free_idx = -1;
    time_t oldest = now;
    int oldest_idx = -1;
    for (int i = 0; i < VEL_CAP; ++i) {
        if (g_vel[i].pan[0] == '\0') { if (free_idx < 0) free_idx = i; continue; }
        if (strcmp(g_vel[i].pan, pan) == 0) return &g_vel[i];
        if (g_vel[i].window_start < oldest) { oldest = g_vel[i].window_start; oldest_idx = i; }
    }
    int idx = (free_idx >= 0) ? free_idx : oldest_idx;
    if (idx < 0) idx = 0;
    memset(&g_vel[idx], 0, sizeof(g_vel[idx]));
    snprintf(g_vel[idx].pan, sizeof(g_vel[idx].pan), "%s", pan);
    g_vel[idx].window_start = now;
    g_vel[idx].count = 0;
    return &g_vel[idx];
}

void risk_evaluate(const IsoRequest *req, RiskDecision *out) {
    if (!out) return;
    out->allow = 1;
    out->reason[0] = '\0';
    if (!g_risk_enabled || !req) return;
    time_t now = time(NULL);
    VelEntry *e = vel_get(req->pan);
    if (now - e->window_start >= g_vel_window_sec) {
        e->window_start = now;
        e->count = 0;
    }
    e->count++;
    if (e->count > g_vel_limit) {
        out->allow = 0;
        snprintf(out->reason, sizeof(out->reason), "%s", "risk_velocity");
    }
}
