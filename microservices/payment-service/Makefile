# Payment Service Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -D_GNU_SOURCE
LIBS = -lcjson -lcurl -lpthread

# Source files
SRCDIR = src
BUILDDIR = build
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
TARGET = $(BUILDDIR)/payment-service

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
	sudo apt-get install -y libcjson-dev libcurl4-openssl-dev

# Docker build
docker-build:
	docker build -t payment-service:latest .

# Docker run
docker-run:
	docker run -d --name payment-service \
		-p 8080:8080 \
		-e RISK_SERVICE_URL=http://risk-service:8081 \
		-e CLEARING_SERVICE_URL=http://clearing-service:8082 \
		-e LEDGER_SERVICE_URL=http://ledger-service:8083 \
		payment-service:latest

# Help
help:
	@echo "Available targets:"
	@echo "  all          - Build the payment service"
	@echo "  clean        - Clean build artifacts"
	@echo "  deps         - Install dependencies"
	@echo "  docker-build - Build Docker image"
	@echo "  docker-run   - Run Docker container"
	@echo "  help         - Show this help message"

.PHONY: all clean deps docker-build docker-run help