# mini-visa Starter

This repository provides a minimal skeleton for a high‑throughput payment gateway. It is intended as a practice project to help you learn C, POSIX threads, TCP/IP networking, PostgreSQL, shell scripting, and basic operations on Linux.

## Directory Layout

```
mini-visa/
  server/        C sources for the server (thread pool, network I/O, handlers)
  client/        A simple load generator written in C
  db/            SQL schema and seed data
  scripts/       Utility shell scripts for running and maintaining the service
  tests/         Stress and chaos scripts to exercise the server
  Makefile       Top‑level build file
  README.md      This document
```

Most core pieces are implemented and annotated in code comments with VN notes for interview mapping. You can compile the project using `make` (the Makefile already builds both server and load generator).

## Quick Start

1. Install dependencies on a Debian/Ubuntu system:
   ```bash
   sudo apt update && sudo apt install -y build-essential libpq-dev valgrind
   ```
2. Create a PostgreSQL database and user:
   ```bash
   sudo -u postgres psql -c "CREATE DATABASE mini_visa;"
   sudo -u postgres psql -c "CREATE USER mini WITH PASSWORD 'mini';"
   sudo -u postgres psql -d mini_visa -f db/schema.sql
   ```
3. Build the project:
   ```bash
   make
   ```
4. Run the server (example):
   ```bash
   DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa" ./build/server
   ```
   Or via helper script (reads DB_URI, optional PORT):
   ```bash
   DB_URI="postgresql://mini:mini@127.0.0.1:5432/mini_visa" PORT=9090 ./scripts/run.sh 2>server.err & echo $! > server.pid
   ```
5. Use the load generator to send test transactions:
   ```bash
   ./build/loadgen 100 1000 9090
   ```
6. Manual quick test via netcat:
   ```bash
   echo '{"pan":"4111111111111111","amount":"10.00"}' | nc 127.0.0.1 9090
   ```
   Keep-alive with newline framing (send multiple on one connection):
   ```bash
   printf '{"pan":"4111111111111111","amount":"1.00"}\n{"pan":"4111111111111111","amount":"2.00"}\n' | nc 127.0.0.1 9090
   ```
7. Health/Readiness checks (simple GET on the same TCP port):
   ```bash
   printf 'GET /healthz\r\n' | nc 127.0.0.1 9090   # prints OK
   printf 'GET /readyz\r\n'  | nc 127.0.0.1 9090   # OK or NOT_READY (DB)
   printf 'GET /metrics\r\n' | nc 127.0.0.1 9090   # JSON counters snapshot
   ```

Tuning via env vars (as referenced in PHONGVAN_FULL.md):
- THREADS: number of worker threads (default 4)
- QUEUE_CAP: bounded job queue capacity (default 1024)

Example high-load tuning:
```bash
export THREADS=8 QUEUE_CAP=2048
./build/loadgen 50 200 9090
```

## Important Notes

* The `server/` directory contains a minimal but working implementation (accept loop, thread pool, timeouts, basic JSON extraction, Luhn, DB insert). Comments explain trade-offs and next steps.
* Idempotency: if a `request_id` field is provided, the server inserts once and returns the existing result on retries.
* Keep-alive + framing: the server reads newline-delimited JSON and can handle multiple requests per TCP connection until timeout/close.
* Structured logging: one JSON line per request on stderr with fields `ts,lvl,event,request_id,status,latency_us`.
* Metrics: simple counters snapshot via `GET /metrics`.
  - Core: `total, approved, declined, server_busy, risk_declined`
  - 2PC/Clearing/Reversal (smart): `twopc_committed, twopc_aborted, clearing_cb_short_circuit, reversal_enqueued, reversal_succeeded, reversal_failed`
* Use Valgrind and GDB to check for memory leaks and concurrency issues.
* Always test with increasing load to observe behaviour under stress.
* Logs write to stderr with timestamps; you can tail errors with `scripts/tail-errs.sh server.err`.

## Additional Docs

- Quick tour (VN): `CODE_READING_QUICK.md`
- Code reading chuyên sâu (VN): `CODE_READING_CHUYEN_SAU.md`
- Vietnamese quick start: `HUONGDAN.md`
- Vietnamese scenario-based test guide: `TEST_HUONGDAN.md`
- PostgreSQL permissions guide: `DB_PERMISSIONS.md`
- PostgreSQL queries cheat sheet: `DB_QUERIES.md`
- Kid-friendly intro to server/client/scripts: `KIDS_GUIDE.md`
- High-load & multithreading progress: `HIGHLOAD_PROGRESS.md`
- Step-by-step high-load guide: `HIGHLOAD_STEPS.md`
- Interview notes (Vietnamese): `PHONGVAN_NOTES.md`
- Full interview handbook (Vietnamese): `PHONGVAN_FULL.md`
 - Visual, easy-to-learn handbook (VN): `PHONGVAN_VISUAL.md`
 - Ops runbook (VN): `RUNBOOK_PAYMENTS.md`

## Smart runtime env (optional)
- 2PC timeouts: `TWOPC_PREPARE_TIMEOUT`, `TWOPC_COMMIT_TIMEOUT` (seconds)
- Clearing: `CLEARING_TIMEOUT`, `CLEARING_RETRY_MAX`, `CLEARING_CB_WINDOW`, `CLEARING_CB_FAILS`, `CLEARING_CB_OPEN_SECS`
- Reversal worker: `REVERSAL_MAX_ATTEMPTS`, `REVERSAL_BASE_DELAY_MS`
