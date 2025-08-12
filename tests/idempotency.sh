#!/usr/bin/env bash

set -euo pipefail

PORT="${1:-9090}"
REQ_ID="${2:-idem_test_$(date +%s)}"

if [[ -z "${DB_URI:-}" ]]; then
  echo "Please export DB_URI for psql verification" >&2
  exit 1
fi

payload() {
  printf '{"pan":"4111111111111111","amount":"10.00","request_id":"%s"}' "$REQ_ID"
}

echo "Sending first request with request_id=$REQ_ID"
payload | nc 127.0.0.1 "$PORT" || true
echo
echo "Sending duplicate request with request_id=$REQ_ID"
payload | nc 127.0.0.1 "$PORT" || true
echo

echo "Verifying only one row exists in DB for request_id=$REQ_ID"
psql "$DB_URI" -At -F ' | ' -c "SELECT COUNT(*) FROM transactions WHERE request_id = '$REQ_ID';"

