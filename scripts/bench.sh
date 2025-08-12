#!/usr/bin/env bash

# Benchmark script for mini-visa
# - Iterates over THREADS and QUEUE_CAP combinations
# - For each combo, starts the server (requires DB_URI env), probes readiness
# - Runs loadgen for each (CONNS,REQS) pair
# - Captures RPS and latency from loadgen, and reject_rate from server /metrics deltas
# - Appends CSV rows to the output file
#
# Usage:
#   DB_URI=postgresql://... ./scripts/bench.sh <PORT> \
#     [THREADS_LIST] [QUEUE_CAP_LIST] [CONNS_LIST] [REQS_LIST] [OUT_CSV]
#
# Defaults:
#   THREADS_LIST="2,4,8"  QUEUE_CAP_LIST="256,1024,4096"  CONNS_LIST="50"  REQS_LIST="200"  OUT_CSV="reports/results.csv"

set -euo pipefail

PORT="${1:-}"; [[ -z "$PORT" ]] && { echo "Usage: DB_URI=... $0 <PORT> [THREADS_LIST] [QUEUE_CAP_LIST] [CONNS_LIST] [REQS_LIST] [OUT_CSV]" >&2; exit 1; }
TLIST="${2:-2,4,8}"
QLIST="${3:-256,1024,4096}"
CLIST="${4:-50}"
RLIST="${5:-200}"
OUT="${6:-reports/results.csv}"

[[ -z "${DB_URI:-}" ]] && { echo "DB_URI env is required to (re)start server" >&2; exit 1; }

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_DIR="$ROOT_DIR/logs"
REPORT_DIR="$(dirname "$OUT")"
mkdir -p "$LOG_DIR" "$REPORT_DIR"

# Build binaries if needed
make -C "$ROOT_DIR" >/dev/null

write_header_if_needed() {
  if [[ ! -s "$OUT" ]]; then
    echo "threads,queue_cap,conns,reqs,rps,p50_us,p95_us,p99_us,reject_rate" >"$OUT"
  fi
}

kill_server() {
  local pid="$1"
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    sleep 0.3
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  fi
}

probe_ready() {
  local port="$1"; local tries=30
  while (( tries-- > 0 )); do
    if printf 'GET /healthz\r\n' | nc -w 1 127.0.0.1 "$port" 2>/dev/null | grep -q '^OK'; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

metrics_snapshot() {
  local port="$1"
  local line
  line=$(printf 'GET /metrics\r\n' | nc -w 1 127.0.0.1 "$port" 2>/dev/null || true)
  # Expected: {"total":T,"approved":A,"declined":D,"server_busy":B}
  local total approved declined busy
  total=$(echo "$line" | sed -n 's/.*"total":\([0-9][0-9]*\).*/\1/p')
  approved=$(echo "$line" | sed -n 's/.*"approved":\([0-9][0-9]*\).*/\1/p')
  declined=$(echo "$line" | sed -n 's/.*"declined":\([0-9][0-9]*\).*/\1/p')
  busy=$(echo "$line" | sed -n 's/.*"server_busy":\([0-9][0-9]*\).*/\1/p')
  echo "${total:-0} ${approved:-0} ${declined:-0} ${busy:-0}"
}

run_one() {
  local conns="$1" reqs="$2" port="$3"
  local before after
  before=$(metrics_snapshot "$port")
  local out
  out=$("$ROOT_DIR/build/loadgen" "$conns" "$reqs" "$port")
  after=$(metrics_snapshot "$port")
  # Parse loadgen output
  # sent_ok=NN, sent_err=NN, wall=X.XXXs, RPS=Y.YY, p50=Uus, p95=Uus, p99=Uus
  local rps p50 p95 p99
  rps=$(echo "$out" | sed -n 's/.*RPS=\([0-9.][0-9.]*\).*/\1/p')
  p50=$(echo "$out" | sed -n 's/.*p50=\([0-9][0-9]*\)us.*/\1/p')
  p95=$(echo "$out" | sed -n 's/.*p95=\([0-9][0-9]*\)us.*/\1/p')
  p99=$(echo "$out" | sed -n 's/.*p99=\([0-9][0-9]*\)us.*/\1/p')
  # Compute reject_rate from metrics deltas
  local bt ba bd bb at aa ad ab
  read -r bt ba bd bb <<<"$before"
  read -r at aa ad ab <<<"$after"
  local dt=$((at-bt)) dbusy=$((ab-bb))
  local reject_rate="0.000"
  if (( dt > 0 )); then
    # printf with 3 decimals
    reject_rate=$(awk -v b="$dbusy" -v t="$dt" 'BEGIN{ printf "%.3f", (t>0?b/t:0) }')
  fi
  echo "$rps" "$p50" "$p95" "$p99" "$reject_rate"
}

write_header_if_needed

IFS=',' read -r -a T_ARR <<<"$TLIST"
IFS=',' read -r -a Q_ARR <<<"$QLIST"
IFS=',' read -r -a C_ARR <<<"$CLIST"
IFS=',' read -r -a R_ARR <<<"$RLIST"

srv_pid=""
trap 'kill_server "$srv_pid" || true' EXIT

for t in "${T_ARR[@]}"; do
  for q in "${Q_ARR[@]}"; do
    # Restart server for this combo
    kill_server "$srv_pid" || true
    log_file="$LOG_DIR/server-t${t}-q${q}.err"
    echo "Starting server: THREADS=$t QUEUE_CAP=$q PORT=$PORT" >&2
    ( DB_URI="$DB_URI" PORT="$PORT" THREADS="$t" QUEUE_CAP="$q" "$ROOT_DIR/build/server" 2>"$log_file" & echo $! >"$LOG_DIR/server.pid" )
    srv_pid=$(cat "$LOG_DIR/server.pid")
    if ! probe_ready "$PORT"; then
      echo "Server failed to become ready on port $PORT for t=$t q=$q" >&2
      exit 1
    fi
    # For each load point, run and record
    for c in "${C_ARR[@]}"; do
      for r in "${R_ARR[@]}"; do
        echo "Running loadgen: conns=$c reqs=$r (t=$t q=$q)" >&2
        read -r rps p50 p95 p99 rej <<<"$(run_one "$c" "$r" "$PORT")"
        echo "$t,$q,$c,$r,$rps,$p50,$p95,$p99,$rej" >>"$OUT"
      done
    done
  done
done

echo "Benchmark complete. Results at $OUT" >&2

