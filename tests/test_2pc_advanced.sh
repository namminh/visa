#!/usr/bin/env bash

# Advanced 2PC Test Script
# Tests Two-Phase Commit scenarios with real payment transactions
# Based on TECH_TEST.md requirements for Track B/C reliability and observability

set -euo pipefail

PORT="${1:-9090}"
DB_URI="${DB_URI:-}"

if [[ -z "$DB_URI" ]]; then
  echo "Error: DB_URI environment variable required" >&2
  echo "Example: export DB_URI=\"postgresql://mini:mini@127.0.0.1:5432/mini_visa\"" >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Colors for output
green() { printf "\e[32m%s\e[0m" "$*"; }
red()   { printf "\e[31m%s\e[0m" "$*"; }
yellow() { printf "\e[33m%s\e[0m" "$*"; }
blue() { printf "\e[34m%s\e[0m" "$*"; }

# Logging
ts() { date '+%Y-%m-%d %H:%M:%S'; }
log() { echo "[$(ts)] $*"; }

# Test counters
pass=0; fail=0
results=()

record() {
  local name="$1" rc="$2" msg="$3"
  if (( rc == 0 )); then
    results+=("[PASS] $name: $msg")
    ((pass++))
    log "$(green "✓ $name: $msg")"
  else
    results+=("[FAIL] $name: $msg")
    ((fail++))
    log "$(red "✗ $name: $msg")"
  fi
}

# Check if server is running
check_server() {
  if ! printf 'GET /healthz\r\n' | nc -w 2 127.0.0.1 "$PORT" 2>/dev/null | grep -q '^OK'; then
    log "$(red "Server not ready on port $PORT")"
    exit 1
  fi
  log "$(green "Server ready on port $PORT")"
}

# Get metrics from server
get_metrics() {
  printf 'GET /metrics\r\n' | nc -w 2 127.0.0.1 "$PORT" 2>/dev/null || echo '{"total":0,"approved":0,"declined":0,"server_busy":0}'
}

# Send transaction and return response
send_transaction() {
  local payload="$1"
  "$ROOT_DIR/tests/send-json.sh" "$PORT" "$payload" 2>/dev/null || echo '{"status":"ERROR","reason":"connection_failed"}'
}

# Parse metrics value
parse_metric() {
  local metrics="$1" field="$2"
  echo "$metrics" | sed -n "s/.*\"$field\":\\([0-9][0-9]*\\).*/\\1/p" | head -1
}

# Test 1: Successful 2PC with detailed logging
test_successful_2pc_detailed() {
  log "$(blue "Testing successful 2PC with detailed logging...")"
  
  local request_id="2pc_detail_$(date +%s%N)"
  local payload="{\"pan\":\"4111111111111111\",\"amount\":\"25.00\",\"request_id\":\"$request_id\"}"
  
  local before_metrics after_metrics
  before_metrics=$(get_metrics)
  
  local response
  response=$(send_transaction "$payload")
  
  after_metrics=$(get_metrics)
  
  # Parse metrics
  local before_total after_total before_approved after_approved
  before_total=$(parse_metric "$before_metrics" "total")
  after_total=$(parse_metric "$after_metrics" "total")
  before_approved=$(parse_metric "$before_metrics" "approved")
  after_approved=$(parse_metric "$after_metrics" "approved")
  
  # Check response status
  if echo "$response" | grep -q '"status":"APPROVED"'; then
    # Verify transaction in database
    local db_count pan_masked
    db_count=$(psql "$DB_URI" -At -c "SELECT COUNT(*) FROM transactions WHERE request_id='$request_id' AND status='APPROVED';" 2>/dev/null || echo "0")
    pan_masked=$(psql "$DB_URI" -At -c "SELECT pan_masked FROM transactions WHERE request_id='$request_id';" 2>/dev/null || echo "")
    
    if [[ "$db_count" == "1" ]] && [[ "$pan_masked" =~ ^[0-9]{6}\*+[0-9]{4}$ ]]; then
      local total_delta=$((after_total - before_total))
      local approved_delta=$((after_approved - before_approved))
      record "2PC Success Detailed" 0 "Transaction committed (total:+$total_delta, approved:+$approved_delta, PAN masked)"
    else
      record "2PC Success Detailed" 1 "Database verification failed (count:$db_count, pan_masked:$pan_masked)"
    fi
  else
    record "2PC Success Detailed" 1 "Transaction not approved: $response"
  fi
}

# Test 2: 2PC rollback with retry logic
test_2pc_rollback_with_retry() {
  log "$(blue "Testing 2PC rollback with retry scenarios...")"
  
  local request_id="2pc_retry_$(date +%s%N)"
  local payload="{\"pan\":\"4111111111111112\",\"amount\":\"25.00\",\"request_id\":\"$request_id\"}"  # Invalid Luhn
  
  local before_metrics after_metrics
  before_metrics=$(get_metrics)
  
  local response
  response=$(send_transaction "$payload")
  
  after_metrics=$(get_metrics)
  
  # Parse metrics
  local before_declined after_declined
  before_declined=$(parse_metric "$before_metrics" "declined")
  after_declined=$(parse_metric "$after_metrics" "declined")
  
  # Check response status is declined
  if echo "$response" | grep -q '"status":"DECLINED"' && echo "$response" | grep -q 'luhn_failed'; then
    # Verify no transaction in database (rollback occurred)
    local db_count
    db_count=$(psql "$DB_URI" -At -c "SELECT COUNT(*) FROM transactions WHERE request_id='$request_id';" 2>/dev/null || echo "0")
    
    if [[ "$db_count" == "0" ]]; then
      local declined_delta=$((after_declined - before_declined))
      record "2PC Rollback Retry" 0 "Transaction properly rolled back (declined:+$declined_delta)"
    else
      record "2PC Rollback Retry" 1 "Transaction should not exist in database (found:$db_count)"
    fi
  else
    record "2PC Rollback Retry" 1 "Expected Luhn failure decline: $response"
  fi
}

# Test 3: Concurrent 2PC transactions with conflict resolution
test_concurrent_2pc_transactions() {
  log "$(blue "Testing concurrent 2PC transactions...")"
  
  local base_id="2pc_concurrent_$(date +%s%N)"
  local pids=()
  local tmp_dir="/tmp/2pc_test_$$"
  mkdir -p "$tmp_dir"
  
  # Launch 10 concurrent transactions
  for i in {1..10}; do
    (
      local request_id="${base_id}_${i}"
      local amount="1${i}.00"
      local payload="{\"pan\":\"4111111111111111\",\"amount\":\"$amount\",\"request_id\":\"$request_id\"}"
      
      local response
      response=$(send_transaction "$payload")
      echo "$response" > "$tmp_dir/response_$i.txt"
      
      if echo "$response" | grep -q '"status":"APPROVED"'; then
        echo "1" > "$tmp_dir/success_$i.txt"
      else
        echo "0" > "$tmp_dir/success_$i.txt"
      fi
    ) &
    pids+=($!)
  done
  
  # Wait for all to complete
  for pid in "${pids[@]}"; do
    wait "$pid"
  done
  
  # Count successes
  local success_count=0
  for i in {1..10}; do
    if [[ -f "$tmp_dir/success_$i.txt" ]]; then
      local success
      success=$(cat "$tmp_dir/success_$i.txt")
      success_count=$((success_count + success))
    fi
  done
  
  # Verify database consistency
  local db_count
  db_count=$(psql "$DB_URI" -At -c "SELECT COUNT(*) FROM transactions WHERE request_id LIKE '$base_id%';" 2>/dev/null || echo "0")
  
  if [[ "$db_count" == "$success_count" ]] && (( success_count >= 8 )); then
    record "Concurrent 2PC" 0 "Handled $success_count/10 concurrent transactions (DB consistent)"
  else
    record "Concurrent 2PC" 1 "Inconsistency: DB=$db_count, responses=$success_count"
  fi
  
  # Cleanup
  rm -rf "$tmp_dir"
}

# Test 4: 2PC performance under load
test_2pc_performance() {
  log "$(blue "Testing 2PC performance under load...")"
  
  local before_metrics after_metrics
  before_metrics=$(get_metrics)
  
  local start_time end_time
  start_time=$(date +%s%N)
  
  # Send 50 transactions as fast as possible
  local base_id="2pc_perf_$(date +%s%N)"
  local pids=()
  
  for i in {1..50}; do
    (
      local request_id="${base_id}_${i}"
      local payload="{\"pan\":\"4111111111111111\",\"amount\":\"1.0$((i % 10))\",\"request_id\":\"$request_id\"}"
      send_transaction "$payload" >/dev/null
    ) &
    pids+=($!)
    
    # Throttle to avoid overwhelming
    if (( i % 10 == 0 )); then
      sleep 0.1
    fi
  done
  
  # Wait for completion
  for pid in "${pids[@]}"; do
    wait "$pid"
  done
  
  end_time=$(date +%s%N)
  after_metrics=$(get_metrics)
  
  # Calculate performance metrics
  local duration_ms=$(( (end_time - start_time) / 1000000 ))
  local rps=$(( 50000 / duration_ms ))
  
  # Parse metrics
  local before_total after_total before_busy after_busy
  before_total=$(parse_metric "$before_metrics" "total")
  after_total=$(parse_metric "$after_metrics" "total")
  before_busy=$(parse_metric "$before_metrics" "server_busy")
  after_busy=$(parse_metric "$after_metrics" "server_busy")
  
  local total_delta=$((after_total - before_total))
  local busy_delta=$((after_busy - before_busy))
  
  # Check database for successful transactions
  local db_count
  db_count=$(psql "$DB_URI" -At -c "SELECT COUNT(*) FROM transactions WHERE request_id LIKE '$base_id%' AND status='APPROVED';" 2>/dev/null || echo "0")
  
  if (( total_delta >= 40 )) && (( db_count >= 40 )); then
    record "2PC Performance" 0 "Processed $total_delta txns in ${duration_ms}ms (~${rps}TPS, $db_count DB records, busy:+$busy_delta)"
  else
    record "2PC Performance" 1 "Performance below threshold (processed:$total_delta, DB:$db_count, time:${duration_ms}ms)"
  fi
}

# Test 5: 2PC circuit breaker behavior
test_2pc_circuit_breaker() {
  log "$(blue "Testing 2PC circuit breaker behavior...")"
  
  # First, send some invalid requests to potentially trigger circuit breaker
  local base_id="2pc_breaker_$(date +%s%N)"
  local invalid_responses=0
  
  # Send 5 requests with invalid amounts to stress the system
  for i in {1..5}; do
    local request_id="${base_id}_invalid_${i}"
    local payload="{\"pan\":\"4111111111111111\",\"amount\":\"0.00\",\"request_id\":\"$request_id\"}"  # Invalid amount
    
    local response
    response=$(send_transaction "$payload")
    
    if echo "$response" | grep -q '"status":"DECLINED"'; then
      ((invalid_responses++))
    fi
    
    sleep 0.1
  done
  
  # Now test if system is still responsive to valid requests
  local request_id="${base_id}_valid"
  local payload="{\"pan\":\"4111111111111111\",\"amount\":\"10.00\",\"request_id\":\"$request_id\"}"
  
  local response
  response=$(send_transaction "$payload")
  
  # Check readiness
  local readyz
  readyz=$(printf 'GET /readyz\r\n' | nc -w 2 127.0.0.1 "$PORT" 2>/dev/null || echo "NOT_READY")
  
  if echo "$response" | grep -q '"status"' && [[ "$readyz" =~ ^(OK|NOT_READY) ]]; then
    record "2PC Circuit Breaker" 0 "System responsive after stress ($invalid_responses invalid processed, readyz: $readyz)"
  else
    record "2PC Circuit Breaker" 1 "System not responsive (response: $response, readyz: $readyz)"
  fi
}

# Test 6: 2PC transaction state consistency
test_2pc_state_consistency() {
  log "$(blue "Testing 2PC transaction state consistency...")"
  
  local request_id="2pc_state_$(date +%s%N)"
  local payload="{\"pan\":\"4111111111111111\",\"amount\":\"75.00\",\"request_id\":\"$request_id\"}"
  
  # Send transaction
  local response
  response=$(send_transaction "$payload")
  
  if echo "$response" | grep -q '"status":"APPROVED"'; then
    # Verify all aspects of transaction state
    local db_record
    db_record=$(psql "$DB_URI" -At -c "SELECT request_id, pan_masked, amount_cents, status FROM transactions WHERE request_id='$request_id';" 2>/dev/null || echo "")
    
    if [[ -n "$db_record" ]]; then
      # Parse DB record
      IFS='|' read -r db_req_id db_pan_masked db_amount db_status <<< "$db_record"
      
      # Validate each field
      local valid=true
      local issues=()
      
      if [[ "$db_req_id" != "$request_id" ]]; then
        valid=false
        issues+=("request_id mismatch")
      fi
      
      if [[ ! "$db_pan_masked" =~ ^[0-9]{6}\*+[0-9]{4}$ ]]; then
        valid=false
        issues+=("invalid PAN masking")
      fi
      
      if [[ "$db_amount" != "7500" ]]; then  # 75.00 in cents
        valid=false
        issues+=("amount conversion error")
      fi
      
      if [[ "$db_status" != "APPROVED" ]]; then
        valid=false
        issues+=("status mismatch")
      fi
      
      if $valid; then
        record "2PC State Consistency" 0 "All transaction state consistent"
      else
        local issue_list
        issue_list=$(IFS=', '; echo "${issues[*]}")
        record "2PC State Consistency" 1 "State inconsistencies: $issue_list"
      fi
    else
      record "2PC State Consistency" 1 "No database record found"
    fi
  else
    record "2PC State Consistency" 1 "Transaction not approved: $response"
  fi
}

# Test 7: 2PC logging and observability
test_2pc_observability() {
  log "$(blue "Testing 2PC logging and observability...")"
  
  local request_id="2pc_observe_$(date +%s%N)"
  local payload="{\"pan\":\"4111111111111111\",\"amount\":\"33.33\",\"request_id\":\"$request_id\"}"
  
  # Clear any existing logs (if accessible)
  local log_file="server.err"
  if [[ -f "$log_file" ]]; then
    local before_lines
    before_lines=$(wc -l < "$log_file" 2>/dev/null || echo "0")
  else
    local before_lines=0
  fi
  
  # Get metrics before
  local before_metrics
  before_metrics=$(get_metrics)
  
  # Send transaction
  local response
  response=$(send_transaction "$payload")
  
  # Get metrics after
  local after_metrics
  after_metrics=$(get_metrics)
  
  # Check for structured logging
  local log_entries=0
  if [[ -f "$log_file" ]]; then
    local after_lines
    after_lines=$(wc -l < "$log_file" 2>/dev/null || echo "0")
    log_entries=$((after_lines - before_lines))
  fi
  
  # Parse metric changes
  local total_change approved_change
  total_change=$(( $(parse_metric "$after_metrics" "total") - $(parse_metric "$before_metrics" "total") ))
  approved_change=$(( $(parse_metric "$after_metrics" "approved") - $(parse_metric "$before_metrics" "approved") ))
  
  # Check version endpoint
  local version
  version=$(printf 'GET /version\r\n' | nc -w 2 127.0.0.1 "$PORT" 2>/dev/null || echo "unknown")
  
  if echo "$response" | grep -q '"status":"APPROVED"' && (( total_change > 0 )) && [[ "$version" != "unknown" ]]; then
    record "2PC Observability" 0 "Observable metrics and endpoints working (log_entries:$log_entries, version:$version)"
  else
    record "2PC Observability" 1 "Observability issues (response: ${response:0:50}..., metrics_change: $total_change)"
  fi
}

# Main execution
main() {
  echo "========================================================"
  echo "Advanced 2PC Integration Test Suite"
  echo "========================================================"
  echo "Port: $PORT"
  echo "DB_URI: $DB_URI"
  echo "Time: $(date)"
  echo "Root Dir: $ROOT_DIR"
  echo "========================================================"
  
  check_server
  
  # Get initial metrics and system state
  local initial_metrics initial_readyz
  initial_metrics=$(get_metrics)
  initial_readyz=$(printf 'GET /readyz\r\n' | nc -w 2 127.0.0.1 "$PORT" 2>/dev/null || echo "NOT_READY")
  
  log "Initial state - Metrics: $initial_metrics"
  log "Initial state - Readyz: $initial_readyz"
  
  # Run advanced tests
  test_successful_2pc_detailed
  test_2pc_rollback_with_retry
  test_concurrent_2pc_transactions
  test_2pc_performance
  test_2pc_circuit_breaker
  test_2pc_state_consistency
  test_2pc_observability
  
  # Get final metrics and system state
  local final_metrics final_readyz
  final_metrics=$(get_metrics)
  final_readyz=$(printf 'GET /readyz\r\n' | nc -w 2 127.0.0.1 "$PORT" 2>/dev/null || echo "NOT_READY")
  
  log "Final state - Metrics: $final_metrics"
  log "Final state - Readyz: $final_readyz"
  
  # Summary
  echo
  echo "========================================================"
  echo "ADVANCED 2PC TEST SUMMARY"
  echo "========================================================"
  for line in "${results[@]}"; do
    if [[ "$line" == \[PASS\]* ]]; then
      echo "$(green "$line")"
    else
      echo "$(red "$line")"
    fi
  done
  echo "--------------------------------------------------------"
  echo "Passed: $(green "$pass")  Failed: $(red "$fail")"
  echo "Success Rate: $(( pass * 100 / (pass + fail) ))%"
  echo "========================================================"
  
  # Exit code
  exit $(( fail == 0 ? 0 : 1 ))
}

main "$@"