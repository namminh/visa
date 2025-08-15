#!/usr/bin/env bash

# Benchmark matrix for Mini‑Visa
# - Varies THREADS and QUEUE_CAP
# - Starts server per configuration, runs loadgen, captures RPS/p95 and /metrics
# - Writes CSV results for easy comparison

set -euo pipefail

PORT="${PORT:-9090}"
DB_URI="${DB_URI:-}"
CONNS="${CONNS:-50}"
REQS="${REQS:-200}"
THREADS_SET_CSV="${THREADS_SET:-1,2,4,8}"
QUEUE_CAP_SET_CSV="${QUEUE_CAP_SET:-1,32,1024}"
ROUNDS="${ROUNDS:-1}"
OUT="${CSV_OUT:-logs/bench-matrix-$(date +%Y%m%d-%H%M%S).csv}"

if [[ -z "$DB_URI" ]]; then
  echo "Please export DB_URI (e.g. postgresql://mini:mini@127.0.0.1:5432/mini_visa)" >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT_DIR/logs"

threads_arr=()
IFS=',' read -r -a threads_arr <<< "$THREADS_SET_CSV"
queue_arr=()
IFS=',' read -r -a queue_arr <<< "$QUEUE_CAP_SET_CSV"

echo "Writing results to: $OUT"
if [[ ! -f "$OUT" ]]; then
  echo "timestamp,threads,queue_cap,conns,reqs,port,rps,p95_us,total,approved,declined,server_busy" > "$OUT"
fi

build() {
  make -C "$ROOT_DIR" >/dev/null
}

start_server() {
  local threads="$1" qcap="$2"
  echo "-- Starting server THREADS=$threads QUEUE_CAP=$qcap PORT=$PORT"
  DB_URI="$DB_URI" THREADS="$threads" QUEUE_CAP="$qcap" PORT="$PORT" \
    "$ROOT_DIR/scripts/run.sh" 2>"$ROOT_DIR/server.err" & echo $! >"$ROOT_DIR/server.pid"
  for i in {1..50}; do
    if printf 'GET /healthz\r\n' | nc -w 1 127.0.0.1 "$PORT" 2>/dev/null | grep -q '^OK'; then
      return 0
    fi
    sleep 0.2
  done
  echo "Server failed to become ready on port $PORT" >&2
  return 1
}

stop_server() {
  local pid
  pid=$(cat "$ROOT_DIR/server.pid" 2>/dev/null || true)
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    sleep 0.2
    kill -9 "$pid" 2>/dev/null || true
  fi
  rm -f "$ROOT_DIR/server.pid"
}

metrics() {
  local line
  line=$(printf 'GET /metrics\r\n' | nc -w 1 127.0.0.1 "$PORT" 2>/dev/null || true)
  local total approved declined busy
  total=$(echo "$line" | sed -n 's/.*"total":\([0-9][0-9]*\).*/\1/p')
  approved=$(echo "$line" | sed -n 's/.*"approved":\([0-9][0-9]*\).*/\1/p')
  declined=$(echo "$line" | sed -n 's/.*"declined":\([0-9][0-9]*\).*/\1/p')
  busy=$(echo "$line" | sed -n 's/.*"server_busy":\([0-9][0-9]*\).*/\1/p')
  echo "${total:-0},${approved:-0},${declined:-0},${busy:-0}"
}

trap 'stop_server || true' EXIT

build

for t in "${threads_arr[@]}"; do
  for q in "${queue_arr[@]}"; do
    start_server "$t" "$q"
    rps_acc=0
    p95_acc=0
    for ((r=1; r<=ROUNDS; r++)); do
      out=$("$ROOT_DIR/build/loadgen" "$CONNS" "$REQS" "$PORT" 2>/dev/null || true)
      rps=$(sed -n 's/.*RPS=\([0-9.][0-9.]*\).*/\1/p' <<<"$out")
      p95=$(sed -n 's/.*p95=\([0-9][0-9]*\)us.*/\1/p' <<<"$out")
      rps_acc=$(awk -v a="$rps_acc" -v b="${rps:-0}" 'BEGIN{printf "%.3f", a + b}')
      p95_acc=$(( p95_acc + ${p95:-0} ))
    done
    # averages
    rps_avg=$(awk -v sum="$rps_acc" -v n="$ROUNDS" 'BEGIN{ if (n==0) n=1; printf "%.3f", sum/n }')
    if [[ "$ROUNDS" -gt 0 ]]; then p95_avg=$(( p95_acc / ROUNDS )); else p95_avg=$p95_acc; fi
    IFS=',' read -r total approved declined busy <<< "$(metrics)"
    echo "$(date +%F\ %T),$t,$q,$CONNS,$REQS,$PORT,$rps_avg,$p95_avg,$total,$approved,$declined,$busy" | tee -a "$OUT"
    stop_server
  done
done

echo "Done. CSV: $OUT"

