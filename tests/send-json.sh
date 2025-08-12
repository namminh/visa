#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <port> [json-payload]" >&2
  exit 1
fi

PORT="$1"
PAYLOAD="${2:-}"

if [[ -z "$PAYLOAD" ]]; then
  PAYLOAD='{"pan":"4111111111111111","amount":"10.00","currency":"USD","merchant":"M1"}'
fi

echo "Sending to 127.0.0.1:${PORT} -> ${PAYLOAD}"
printf "%s" "$PAYLOAD" | nc 127.0.0.1 "$PORT"

