#!/usr/bin/env bash

# Run the mini‑visa server. Set DB_URI and optionally PORT via env vars.

set -euo pipefail

if [[ -z "${DB_URI:-}" ]]; then
    echo "Please set DB_URI to a valid PostgreSQL connection string." >&2
    exit 1
fi

# Default port if not specified
PORT="${PORT:-9090}"

echo "Starting server on port ${PORT} with DB_URI=${DB_URI}"

# Build if necessary
make -C "$(dirname "$0")/.." server

# Run the server binary
"$(dirname "$0")/../build/server"