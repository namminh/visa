#!/usr/bin/env bash

# Simple stress test script for the mini‑visa server.

set -euo pipefail

CONNS="${CONNS:-100}"
REQS="${REQS:-1000}"
PORT="${PORT:-9090}"

echo "Running stress test: ${CONNS} connections, ${REQS} requests each, port ${PORT}"

# Build client if needed
make -C "$(dirname "$0")/.." client

# Run load generator
"$(dirname "$0")/../build/loadgen" "$CONNS" "$REQS" "$PORT"