# Risk Service Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -D_GNU_SOURCE
LIBS = -lcjson -lpthread

# Source files
SRCDIR = src
BUILDDIR = build
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
TARGET = $(BUILDDIR)/risk-service

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
	docker build -t risk-service:latest .

# Docker run
docker-run:
	docker run -d --name risk-service \
		-p 8081:8081 \
		-e RISK_MAX_AMOUNT=10000 \
		-e RISK_VELOCITY_LIMIT=20 \
		-e RISK_VELOCITY_WINDOW=60 \
		risk-service:latest

# Test the service
test:
	@echo "Testing Risk Service..."
	@curl -X POST http://localhost:8081/risk/evaluate \
		-H "Content-Type: application/json" \
		-d '{"pan":"4111111111111111","amount":"100.00","currency":"USD"}' \
		|| echo "Service not running"

# Help
help:
	@echo "Available targets:"
	@echo "  all          - Build the risk service"
	@echo "  clean        - Clean build artifacts"
	@echo "  deps         - Install dependencies"
	@echo "  docker-build - Build Docker image"
	@echo "  docker-run   - Run Docker container"
	@echo "  test         - Test the service"
	@echo "  help         - Show this help message"

.PHONY: all clean deps docker-build docker-run test help