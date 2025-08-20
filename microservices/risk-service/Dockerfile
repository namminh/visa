FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    gcc \
    libc6-dev \
    libcjson-dev \
    make \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy source code
COPY src/ ./src/
COPY Makefile ./

# Build the application
RUN make all

# Create non-root user for security
RUN useradd -r -s /bin/false riskuser

# Change ownership of the app directory  
RUN chown -R riskuser:riskuser /app

# Switch to non-root user
USER riskuser

# Expose port
EXPOSE 8081

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD nc -z localhost 8081 || exit 1

# Run the service
CMD ["./build/risk-service"]