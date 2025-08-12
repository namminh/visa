#!/usr/bin/env bash

# Chaos script for mini‑visa. Introduces network delays or pauses
# the server process to simulate faults. Requires root privileges
# for advanced network manipulation.

set -euo pipefail

# Example: kill -STOP and kill -CONT to suspend/resume the server
SERVER_PID="${SERVER_PID:-}"
if [[ -z "$SERVER_PID" ]]; then
    echo "Please set SERVER_PID to the PID of the running server" >&2
    exit 1
fi

echo "Suspending server (PID ${SERVER_PID}) for 5 seconds..."
kill -STOP "$SERVER_PID"
sleep 5
echo "Resuming server"
kill -CONT "$SERVER_PID"

# You can also use `tc qdisc add` here to introduce latency or loss
# For example:
# sudo tc qdisc add dev lo root netem delay 100ms
# And later remove it:
# sudo tc qdisc del dev lo root netem