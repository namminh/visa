#!/bin/bash

echo "🧪 Integration Tests for 2-Phase Commit"
echo "======================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

TOTAL_TESTS=0
PASSED_TESTS=0

run_test() {
    local test_name="$1"
    local test_command="$2"
    local expected_result="$3"
    
    echo -e "\n${YELLOW}Test: $test_name${NC}"
    echo "Command: $test_command"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    result=$(eval "$test_command" 2>&1)
    exit_code=$?
    
    if [[ $exit_code -eq $expected_result ]]; then
        echo -e "${GREEN}✓ PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAILED${NC}"
        echo "Expected exit code: $expected_result, Got: $exit_code"
        echo "Output: $result"
    fi
}

start_server() {
    echo "🚀 Starting server..."
    ./build/server > logs/test_server.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    if kill -0 $SERVER_PID 2>/dev/null; then
        echo "✅ Server started (PID: $SERVER_PID)"
        return 0
    else
        echo "❌ Failed to start server"
        return 1
    fi
}

stop_server() {
    if [[ -n "$SERVER_PID" ]]; then
        echo "🛑 Stopping server (PID: $SERVER_PID)..."
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
        echo "✅ Server stopped"
    fi
}

send_request() {
    local request="$1"
    echo "$request" | timeout 5 nc localhost 9090 2>/dev/null
}

cleanup() {
    stop_server
    rm -f logs/test_server.log
}

trap cleanup EXIT

echo "📦 Building project..."
make clean && make
if [[ $? -ne 0 ]]; then
    echo "❌ Build failed"
    exit 1
fi

if ! start_server; then
    echo "❌ Cannot start server for integration tests"
    exit 1
fi

sleep 1

run_test "Health Endpoint" \
    'send_request "GET /healthz" | grep -q "OK"' \
    0

run_test "Metrics Endpoint" \
    'send_request "GET /metrics" | grep -q "total"' \
    0

run_test "Valid Transaction" \
    'send_request "{\"pan\":\"4532123456789012\",\"amount\":\"100.00\",\"request_id\":\"test_001\"}" | grep -q "APPROVED"' \
    0

run_test "Invalid PAN (Luhn Fail)" \
    'send_request "{\"pan\":\"1234567890123456\",\"amount\":\"100.00\",\"request_id\":\"test_002\"}" | grep -q "DECLINED"' \
    0

run_test "Transaction Logs Created" \
    'test -f logs/transactions.log' \
    0

echo -e "\n📊 Test Summary"
echo "==============="
echo -e "Total tests: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$((TOTAL_TESTS - PASSED_TESTS))${NC}"

if [[ $PASSED_TESTS -eq $TOTAL_TESTS ]]; then
    echo -e "\n${GREEN}🎉 All integration tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ Some tests failed${NC}"
    exit 1
fi