#!/usr/bin/env bash

# Tail the server stderr log and grep for error patterns.
# This script demonstrates how you might monitor logs for specific
# keywords like TIMEOUT, RETRY, or DBERR. Adapt as needed.

set -euo pipefail

# Usage: ./tail-errs.sh <path-to-log>

LOG_FILE="${1:-}"
if [[ -z "$LOG_FILE" ]]; then
    echo "Usage: $0 <log file>" >&2
    exit 1
fi

echo "Tailing errors in $LOG_FILE"

tail -f "$LOG_FILE" | grep --line-buffered -E "TIMEOUT|RETRY|DBERR|ERROR"