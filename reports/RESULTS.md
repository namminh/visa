# Benchmark Results (Template)

This document summarizes performance runs for mini‑visa using `scripts/bench.sh`.

## How to Run

```
export DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa"
./scripts/bench.sh 9090 "2,4,8" "256,1024,4096" "50" "200" reports/results.csv
```

- The script restarts the server for each `(THREADS, QUEUE_CAP)` combination.
- For each combo it runs `loadgen` on a matrix of `(conns, reqs)` and appends to a CSV file.
- It also computes `reject_rate` from `/metrics` deltas: `server_busy_delta / total_delta`.

## CSV Columns

```
threads,queue_cap,conns,reqs,rps,p50_us,p95_us,p99_us,reject_rate
```

## Example (paste your latest results)

```
threads,queue_cap,conns,reqs,rps,p50_us,p95_us,p99_us,reject_rate
2,256,50,200,1234.56,800,2200,4800,0.012
2,1024,50,200,1500.21,700,2000,4200,0.008
4,1024,50,200,2100.78,650,1800,3800,0.004
8,2048,50,200,2500.90,600,1600,3400,0.002
```

## Notes & Observations

- Describe any plateau points or sharp increases in `reject_rate`.
- Note the configuration that meets your p95 latency objective under target RPS.
- Capture any anomalies seen in `server.err` (timeouts, DB errors, breaker events).

