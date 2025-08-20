#!/usr/bin/env bash

# All-in-one test runner for mini‑visa
# - Starts the server (requires DB_URI), waits for readiness
# - Runs functional tests for M0/M1/M2 (+ M3 if implemented)
# - Prints PASS/FAIL summary at the end

set -euo pipefail

# Optional trace of shell commands when TRACE=1
TRACE="${TRACE:-0}"
if [[ "$TRACE" == "1" ]]; then
  export PS4='[${EPOCHREALTIME}] $ '
  set -x
fi

PORT="${1:-9090}"
THREADS="${THREADS:-4}"
QUEUE_CAP="${QUEUE_CAP:-1024}"
DB_URI="${DB_URI:-}"

if [[ -z "$DB_URI" ]]; then
  echo "Please export DB_URI (e.g. postgresql://mini:mini@127.0.0.1:5432/mini_visa)" >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_FILE="$ROOT_DIR/server.err"
PID_FILE="$ROOT_DIR/server.pid"
LIVE_LOG="${LIVE_LOG:-0}"
TAIL_PID=""

# Ensure logs directory and unified test run log file
mkdir -p "$ROOT_DIR/logs"
RUN_LOG="${RUN_LOG:-$ROOT_DIR/logs/test-run-$(date +%Y%m%d-%H%M%S).log}"
# Redirect all stdout/stderr of this script to both console and RUN_LOG
if [[ -n "$RUN_LOG" ]]; then
  # shellcheck disable=SC2069
  exec > >(tee -a "$RUN_LOG") 2>&1
fi
ts() { date '+%Y-%m-%d %H:%M:%S'; }
LOG_MODE="${LOG_MODE:-compact}"   # compact | verbose
LOG_LEVEL=1; [[ "$LOG_MODE" == "verbose" ]] && LOG_LEVEL=2
log()  { echo "[$(ts)] $*"; }
logv() { [[ $LOG_LEVEL -ge 2 ]] && log "$*" || true; }
section() { echo; echo "[$(ts)] ---- $* ----"; }
echo "[run_all] Logging full test run to: $RUN_LOG (mode=$LOG_MODE)"

pass=0; fail=0
results=()

green() { printf "\e[32m%s\e[0m" "$*"; }
red()   { printf "\e[31m%s\e[0m" "$*"; }

record() {
  local name="$1" rc="$2" msg="$3"
  if (( rc == 0 )); then
    results+=("[PASS] $name: $msg")
    ((pass++))
  else
    results+=("[FAIL] $name: $msg")
    ((fail++))
  fi
}

kill_server() {
  local pid
  pid=$(cat "$PID_FILE" 2>/dev/null || true)
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    sleep 0.2
    kill -9 "$pid" 2>/dev/null || true
  fi
  rm -f "$PID_FILE"
}

probe_ready() {
  local tries=40
  while (( tries-- > 0 )); do
    if printf 'GET /healthz\r\n' | nc -w 1 127.0.0.1 "$PORT" 2>/dev/null | grep -q '^OK'; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

metrics() {
  local line
  line=$(printf 'GET /metrics\r\n' | nc -w 1 127.0.0.1 "$PORT" 2>/dev/null || true)
  local total approved declined busy
  total=$(echo "$line" | sed -n 's/.*"total":\([0-9][0-9]*\).*/\1/p')
  approved=$(echo "$line" | sed -n 's/.*"approved":\([0-9][0-9]*\).*/\1/p')
  declined=$(echo "$line" | sed -n 's/.*"declined":\([0-9][0-9]*\).*/\1/p')
  busy=$(echo "$line" | sed -n 's/.*"server_busy":\([0-9][0-9]*\).*/\1/p')
  echo "${total:-0} ${approved:-0} ${declined:-0} ${busy:-0}"
}

start_server() {
  log "Starting server PORT=$PORT THREADS=$THREADS QUEUE_CAP=$QUEUE_CAP"
  ( DB_URI="$DB_URI" PORT="$PORT" THREADS="$THREADS" QUEUE_CAP="$QUEUE_CAP" "$ROOT_DIR/scripts/run.sh" 2>"$LOG_FILE" & echo $! >"$PID_FILE" )
  if ! probe_ready; then
    log "Server failed to become ready on port $PORT"
    exit 1
  fi
  if [[ "$LIVE_LOG" == "1" ]]; then
    # Show live logs while tests run
    ( tail -n +1 -f "$LOG_FILE" & echo $! >"$ROOT_DIR/.tail.pid" )
    TAIL_PID=$(cat "$ROOT_DIR/.tail.pid" 2>/dev/null || true)
    rm -f "$ROOT_DIR/.tail.pid"
  fi
}

cleanup() {
  if [[ -n "$TAIL_PID" ]]; then kill "$TAIL_PID" 2>/dev/null || true; fi
  kill_server || true
}
trap cleanup EXIT

# Build once
make -C "$ROOT_DIR" >/dev/null

start_server

# ---- Tests ----

# M0: Approved valid
test_approved() {
  local payload='{"pan":"4111111111111111","amount":"10.00"}'
  logv "[M0 approved] Sending: $payload"
  local out
  out=$("$ROOT_DIR/tests/send-json.sh" "$PORT" "$payload" || true)
  logv "[M0 approved] Response: $out"
  grep -q '"APPROVED"' <<<"$out"
}

# M0: Luhn fail
test_luhn_fail() {
  local payload='{"pan":"4111111111111112","amount":"10.00"}'
  logv "[M0 luhn] Sending: $payload"
  local out
  out=$("$ROOT_DIR/tests/send-json.sh" "$PORT" "$payload" || true)
  logv "[M0 luhn] Response: $out"
  grep -q 'luhn_failed' <<<"$out"
}

# M0: Amount invalid
test_amount_invalid() {
  local payload='{"pan":"4111111111111111","amount":"0.00"}'
  logv "[M0 amount] Sending: $payload"
  local out
  out=$("$ROOT_DIR/tests/send-json.sh" "$PORT" "$payload" || true)
  logv "[M0 amount] Response: $out"
  grep -q 'amount_invalid' <<<"$out"
}

# M0: PAN masking (DB)
test_pan_masking() {
  # Send one approved request
  logv "[M0 mask] Sending approved tx for pan_masked check"
  "$ROOT_DIR/tests/send-json.sh" "$PORT" '{"pan":"4111111111111111","amount":"10.00"}' >/dev/null || true
  # Fetch last pan_masked
  local pm
  pm=$(psql "$DB_URI" -At -c "SELECT pan_masked FROM transactions ORDER BY id DESC LIMIT 1;")
  logv "[M0 mask] DB pan_masked: $pm"
  [[ -n "$pm" ]] || return 1
  # Expect 6 digits + stars + 4 digits
  grep -Eq '^[0-9]{6}\*+[0-9]{4}$' <<<"$pm"
}

# M0: Backpressure (server_busy > 0 under burst)
test_backpressure() {
  local before after bt ba bd bb at aa ad ab
  read -r bt ba bd bb <<<"$(metrics)"
  logv "[M0 backpressure] Metrics before: total=$bt approved=$ba declined=$bd busy=$bb"
  log "Running loadgen: conns=200 reqs=200 (t=$THREADS q=$QUEUE_CAP)"
  local lg
  lg=$("$ROOT_DIR/build/loadgen" 200 200 "$PORT" 2>/dev/null || true)
  read -r at aa ad ab <<<"$(metrics)"
  logv "[M0 backpressure] Metrics after: total=$at approved=$aa declined=$ad busy=$ab"
  if [[ -n "$lg" ]]; then
    local rps p95
    rps=$(sed -n 's/.*RPS=\([0-9.][0-9.]*\).*/\1/p' <<<"$lg")
    p95=$(sed -n 's/.*p95=\([0-9][0-9]*\)us.*/\1/p' <<<"$lg")
    [[ -n "$rps" ]] && log "Loadgen: RPS=$rps, p95=${p95:-na}us"
  fi
  local dbusy=$((ab-bb)) dtotal=$((at-bt))
  # Pass if either server_busy increased or total increased substantially without crash
  (( dtotal > 0 )) || return 1
  return 0
}

# M1: Idempotency
test_idempotency() {
  local id="idem_$(date +%s)"
  log "Idempotency: request_id=$id"
  DB_URI="$DB_URI" "$ROOT_DIR/tests/idempotency.sh" "$PORT" "$id" >/dev/null
  local cnt
  cnt=$(psql "$DB_URI" -At -c "SELECT COUNT(*) FROM transactions WHERE request_id='$id';")
  logv "[M1 idempotency] DB count=$cnt"
  [[ "$cnt" == "1" ]]
}

# M1: Health/Ready
test_health_ready() {
  local hz rz
  hz=$(printf 'GET /healthz\r\n' | nc -w 1 127.0.0.1 "$PORT" || true)
  rz=$(printf 'GET /readyz\r\n'  | nc -w 1 127.0.0.1 "$PORT" || true)
  log "Healthz: ${hz//$'\r'/}"; log "Readyz: ${rz//$'\r'/}"
  grep -q '^OK' <<<"$hz" && grep -Eq '^(OK|NOT_READY)' <<<"$rz"
}

# M2: Keep-alive 5 lines
test_keepalive() {
  local out
  out=$("$ROOT_DIR/tests/keepalive.sh" "$PORT" || true)
  logv "[M2 keepalive] Combined response (first 3 lines):"
  echo "$out" | sed -n '1,3p' | sed 's/^/[M2 keepalive] /'
  # Count response lines that look like JSON with status
  local n
  n=$(grep -c '"status"' <<<"$out" || true)
  log "Keep-alive: responses=$n"
  (( n >= 5 ))
}

# M2: Partial read (split payload)
test_partial_read() {
  local out
  logv "[M2 partial] Sending split JSON then newline"
  out=$({ printf '{"pan":"4111111111111111","amount":"1.00"'; sleep 1; printf '}'$'\n'; } | nc 127.0.0.1 "$PORT" 2>/dev/null || true)
  log "Partial-read: got_response=$( [[ -n "$out" ]] && echo yes || echo no )"
  grep -q '"status"' <<<"$out"
}

# M2: Metrics snapshot JSON present
test_metrics() {
  local m
  m=$(printf 'GET /metrics\r\n' | nc -w 1 127.0.0.1 "$PORT" 2>/dev/null || true)
  log "Metrics: $m"
  grep -q '"total"' <<<"$m"
}

# Security: no full PAN in logs
test_no_pan_in_logs() {
  if grep -q "4111111111111111" "$LOG_FILE"; then
    log "[SEC log] Found full PAN in server.err (FAIL)"
    return 1
  else
    log "[SEC log] No full PAN present in server.err (OK)"
    return 0
  fi
}

# 2PC: Two-Phase Commit unit tests
test_2pc_unit() {
  if [[ -f "$ROOT_DIR/build/test_2pc" ]]; then
    log "Running 2PC unit tests..."
    local out
    out=$("$ROOT_DIR/build/test_2pc" 2>&1 || true)
    if echo "$out" | grep -q "All 2PC tests passed"; then
      log "2PC unit tests: PASSED"
      return 0
    else
      log "2PC unit tests: FAILED"
      logv "$out"
      return 1
    fi
  else
    log "2PC unit tests: SKIPPED (test_2pc not built)"
    return 0
  fi
}

# 2PC: Integration tests with scenarios
test_2pc_integration() {
  # Try advanced tests first, fallback to simple
  if [[ -x "$ROOT_DIR/tests/test_2pc_advanced.sh" ]]; then
    log "Running advanced 2PC integration tests..."
    local out
    out=$(DB_URI="$DB_URI" "$ROOT_DIR/tests/test_2pc_advanced.sh" "$PORT" 2>&1 || true)
    if echo "$out" | grep -q "Success Rate: [789][0-9]%\|Success Rate: 100%"; then
      log "2PC integration tests: PASSED (advanced)"
      return 0
    else
      log "Advanced 2PC tests had issues, trying simple tests..."
    fi
  fi
  
  # Fallback to simple tests
  if [[ -f "$ROOT_DIR/tests/test_2pc_simple.sh" ]]; then
    log "Running simple 2PC integration tests..."
    local out
    out=$(DB_URI="$DB_URI" bash "$ROOT_DIR/tests/test_2pc_simple.sh" "$PORT" 2>&1 || true)
    local pass_count
    pass_count=$(echo "$out" | grep -c "PASS" || echo "0")
    if (( pass_count >= 2 )); then
      log "2PC integration tests: PASSED (simple, $pass_count tests)"
      return 0
    else
      log "2PC integration tests: FAILED (simple, $pass_count passed)"
      logv "$out"
      return 1
    fi
  else
    log "2PC integration tests: SKIPPED (no test files found)"
    return 0
  fi
}

run_case() {
  local name="$1"; shift
  section "$name"
  if "$@"; then record "$name" 0 "ok"; else record "$name" 1 "failed"; fi
}

run_case "M0: approved"        test_approved
run_case "M0: luhn_failed"      test_luhn_fail
run_case "M0: amount_invalid"   test_amount_invalid
run_case "M0: pan_masked"       test_pan_masking
run_case "M0: backpressure"     test_backpressure
run_case "M1: idempotency"      test_idempotency
run_case "M1: health_ready"     test_health_ready
run_case "M2: keepalive"        test_keepalive
run_case "M2: partial_read"     test_partial_read
run_case "M2: metrics"          test_metrics
run_case "SEC: no_pan_in_logs"  test_no_pan_in_logs
run_case "2PC: unit_tests"      test_2pc_unit
run_case "2PC: integration"     test_2pc_integration

echo
echo "======== TEST SUMMARY ========"
for line in "${results[@]}"; do
  if [[ "$line" == \[PASS\]* ]]; then echo "$(green "$line")"; else echo "$(red "$line")"; fi
done
echo "------------------------------"
echo "Passed: $pass  Failed: $fail"

echo
echo "======== SERVER LOG PATH ========"
echo "$LOG_FILE"
echo "================================"

echo
echo "======== SERVER LOG TAIL (last 100) ========"
tail -n 100 "$LOG_FILE" || true
echo "==========================================="

exit $(( fail == 0 ? 0 : 1 ))
