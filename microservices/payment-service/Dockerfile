FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    gcc \
    libc6-dev \
    libcjson-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy source code and Makefile
COPY src/ ./src/
COPY Makefile ./

# Build the application
RUN make all

# Create non-root user for security
RUN useradd -r -s /bin/false paymentuser

# Change ownership of the app directory
RUN chown -R paymentuser:paymentuser /app

# Switch to non-root user
USER paymentuser

# Expose port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Run the service
CMD ["./build/payment-service"]