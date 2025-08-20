#!/usr/bin/env bash

# Simple 2PC Integration Test
# Tests basic Two-Phase Commit functionality
# Compatible with existing test infrastructure

set -euo pipefail

PORT="${1:-9090}"
DB_URI="${DB_URI:-}"

if [[ -z "$DB_URI" ]]; then
  echo "Error: DB_URI required" >&2
  exit 1
fi

# Simple test functions
test_2pc_basic() {
  local payload='{"pan":"4111111111111111","amount":"10.00","request_id":"2pc_basic_001"}'
  local response
  response=$(printf "%s\n" "$payload" | nc 127.0.0.1 "$PORT" 2>/dev/null || echo "ERROR")
  
  if echo "$response" | grep -q '"status":"APPROVED"'; then
    # Check database
    local count
    count=$(psql "$DB_URI" -At -c "SELECT COUNT(*) FROM transactions WHERE request_id='2pc_basic_001';" 2>/dev/null || echo "0")
    if [[ "$count" == "1" ]]; then
      echo "2PC Basic: PASS"
      return 0
    fi
  fi
  echo "2PC Basic: FAIL"
  return 1
}

test_2pc_rollback() {
  local payload='{"pan":"4111111111111112","amount":"10.00","request_id":"2pc_rollback_001"}'  # Invalid Luhn
  local response
  response=$(printf "%s\n" "$payload" | nc 127.0.0.1 "$PORT" 2>/dev/null || echo "ERROR")
  
  if echo "$response" | grep -q '"status":"DECLINED"'; then
    # Check no record in database
    local count
    count=$(psql "$DB_URI" -At -c "SELECT COUNT(*) FROM transactions WHERE request_id='2pc_rollback_001';" 2>/dev/null || echo "0")
    if [[ "$count" == "0" ]]; then
      echo "2PC Rollback: PASS"
      return 0
    fi
  fi
  echo "2PC Rollback: FAIL"
  return 1
}

test_2pc_idempotent() {
  local payload='{"pan":"4111111111111111","amount":"15.00","request_id":"2pc_idem_001"}'
  
  # Send twice
  local response1 response2
  response1=$(printf "%s\n" "$payload" | nc 127.0.0.1 "$PORT" 2>/dev/null || echo "ERROR")
  response2=$(printf "%s\n" "$payload" | nc 127.0.0.1 "$PORT" 2>/dev/null || echo "ERROR")
  
  if echo "$response1" | grep -q '"status":"APPROVED"' && echo "$response2" | grep -q '"status":"APPROVED"'; then
    # Check only one record
    local count
    count=$(psql "$DB_URI" -At -c "SELECT COUNT(*) FROM transactions WHERE request_id='2pc_idem_001';" 2>/dev/null || echo "0")
    if [[ "$count" == "1" ]]; then
      echo "2PC Idempotent: PASS"
      return 0
    fi
  fi
  echo "2PC Idempotent: FAIL"
  return 1
}

# Check server is running
if ! printf 'GET /healthz\r\n' | nc -w 1 127.0.0.1 "$PORT" 2>/dev/null | grep -q '^OK'; then
  echo "Server not ready"
  exit 1
fi

echo "Running 2PC Tests..."

# Run tests
test_2pc_basic
test_2pc_rollback  
test_2pc_idempotent

echo "2PC Tests completed"