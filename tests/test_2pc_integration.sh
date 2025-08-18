#!/bin/bash

echo "🧪 Integration Tests for 2-Phase Commit"
echo "======================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test results
TOTAL_TESTS=0
PASSED_TESTS=0

# Function to run a test
run_test() {
    local test_name="$1"
    local test_command="$2"
    local expected_result="$3"
    
    echo -e "\n${YELLOW}Test: $test_name${NC}"
    echo "Command: $test_command"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Run the test
    result=$(eval "$test_command" 2>&1)
    exit_code=$?
    
    # Check result
    if [[ $exit_code -eq $expected_result ]]; then
        echo -e "${GREEN}✓ PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAILED${NC}"
        echo "Expected exit code: $expected_result, Got: $exit_code"
        echo "Output: $result"
    fi
}

# Function to start server in background
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

# Function to stop server
stop_server() {
    if [[ -n "$SERVER_PID" ]]; then
        echo "🛑 Stopping server (PID: $SERVER_PID)..."
        kill $SERVER_PID
        wait $SERVER_PID 2>/dev/null
        echo "✅ Server stopped"
    fi
}

# Function to send request to server
send_request() {
    local request="$1"
    echo "$request" | nc localhost 9090 2>/dev/null
}

# Cleanup function
cleanup() {
    stop_server
    rm -f logs/test_server.log
}

# Set trap for cleanup
trap cleanup EXIT

echo "📦 Building project..."
make clean && make
if [[ $? -ne 0 ]]; then
    echo "❌ Build failed"
    exit 1
fi

# Test 1: Basic server functionality
run_test "Server Health Check" \
    'echo "GET /healthz" | nc localhost 9090 | grep -q "OK" && echo $?' \
    0

# Start server for integration tests
if ! start_server; then
    echo "❌ Cannot start server for integration tests"
    exit 1
fi

# Wait for server to be ready
sleep 1

# Test 2: Health endpoint
run_test "Health Endpoint" \
    'send_request "GET /healthz" | grep -q "OK"' \
    0

# Test 3: Metrics endpoint
run_test "Metrics Endpoint" \
    'send_request "GET /metrics" | grep -q "total"' \
    0

# Test 4: Valid transaction (should use 2PC)
run_test "Valid Transaction" \
    'send_request "{\"pan\":\"4532123456789012\",\"amount\":\"100.00\",\"request_id\":\"test_001\"}" | grep -q "APPROVED"' \
    0

# Test 5: Invalid PAN (Luhn check fails)
run_test "Invalid PAN (Luhn Fail)" \
    'send_request "{\"pan\":\"1234567890123456\",\"amount\":\"100.00\",\"request_id\":\"test_002\"}" | grep -q "DECLINED"' \
    0

# Test 6: Invalid Amount
run_test "Invalid Amount" \
    'send_request "{\"pan\":\"4532123456789012\",\"amount\":\"99999.00\",\"request_id\":\"test_003\"}" | grep -q "DECLINED"' \
    0

# Test 7: Duplicate request_id (Idempotency)
run_test "Idempotent Request" \
    'send_request "{\"pan\":\"4532123456789012\",\"amount\":\"100.00\",\"request_id\":\"test_001\"}" | grep -q "idempotent"' \
    0

# Test 8: Malformed JSON
run_test "Malformed JSON" \
    'send_request "{invalid json}" | grep -q "DECLINED"' \
    0

# Test 9: Concurrent requests
run_test "Concurrent Requests" \
    'for i in {1..5}; do send_request "{\"pan\":\"4532123456789012\",\"amount\":\"50.00\",\"request_id\":\"concurrent_$i\"}" & done; wait; echo "done"' \
    0

# Test 10: Large burst of requests
run_test "Load Test (50 requests)" \
    'for i in {1..50}; do send_request "{\"pan\":\"4532123456789012\",\"amount\":\"10.00\",\"request_id\":\"load_$i\"}" > /dev/null & done; wait; echo "done"' \
    0

# Test 11: Check transaction logs
run_test "Transaction Logs Created" \
    'test -f logs/transactions.log' \
    0

# Test 12: Verify 2PC logging
run_test "2PC Logs Present" \
    'grep -q "PREPARE\|COMMIT" logs/transactions.log' \
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