#!/usr/bin/env bash

set -euo pipefail

PORT="${1:-9090}"

{
  printf '{"pan":"4111111111111111","amount":"1.00"}\n'
  printf '{"pan":"4111111111111111","amount":"2.00"}\n'
  printf '{"pan":"4111111111111111","amount":"3.00"}\n'
  printf '{"pan":"4111111111111111","amount":"4.00"}\n'
  printf '{"pan":"4111111111111111","amount":"5.00"}\n'
  sleep 1
} | nc 127.0.0.1 "$PORT"

echo

