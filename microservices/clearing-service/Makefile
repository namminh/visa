# Clearing Service Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -D_GNU_SOURCE
LIBS = -lcjson -lpthread

# Source files
SRCDIR = src
BUILDDIR = build
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
TARGET = $(BUILDDIR)/clearing-service

# Default target
all: $(TARGET)

# Create build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Compile source files to object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link object files to create executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBS) -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILDDIR)

# Install dependencies (Ubuntu/Debian)
deps:
	sudo apt-get update
	sudo apt-get install -y libcjson-dev

# Docker build
docker-build:
	docker build -t clearing-service:latest .

# Docker run
docker-run:
	docker run -d --name clearing-service \
		-p 8082:8082 \
		-e CLEARING_SIMULATE_FAILURES=5 \
		-e CLEARING_PREPARE_TIMEOUT=30 \
		-e CLEARING_COMMIT_TIMEOUT=30 \
		clearing-service:latest

# Test the service  
test:
	@echo "Testing Clearing Service..."
	@curl -X POST http://localhost:8082/clearing/prepare \
		-H "Content-Type: application/json" \
		-d '{"txn_id":"test_123","pan":"4111111111111111","amount":"100.00"}' \
		|| echo "Service not running"

# Help
help:
	@echo "Available targets:"
	@echo "  all          - Build the clearing service"
	@echo "  clean        - Clean build artifacts"
	@echo "  deps         - Install dependencies" 
	@echo "  docker-build - Build Docker image"
	@echo "  docker-run   - Run Docker container"
	@echo "  test         - Test the service"
	@echo "  help         - Show this help message"

.PHONY: all clean deps docker-build docker-run test help