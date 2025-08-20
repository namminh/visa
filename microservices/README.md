# Mini-Visa Microservices Architecture

Hệ thống thanh toán mini-visa được chuyển đổi từ monolith sang microservices architecture với 6 services độc lập.

## 🏗️ **Kiến Trúc Tổng Thể**

```
                    🌐 API Gateway (Nginx)
                           :80, :443
                              │
                              ▼
    ┌─────────────────┬─────────────────┬─────────────────┐
    │                 │                 │                 │
    ▼                 ▼                 ▼                 ▼
┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐
│Payment  │    │  Risk   │    │Clearing │    │ Ledger  │
│Service  │    │Service  │    │Service  │    │Service  │
│ :8080   │    │ :8081   │    │ :8082   │    │ :8083   │
└─────────┘    └─────────┘    └─────────┘    └─────────┘
    │                                             │
    └─────────────────┬──────────────────────────┘
                      ▼
              ┌─────────────────┐
              │  📨 Redis       │ ← Event Bus
              │   :6379         │
              └─────────────────┘
                      │
    ┌─────────────────┼─────────────────┐
    ▼                 ▼                 ▼
┌─────────┐    ┌─────────┐    ┌─────────┐
│Reversal │    │ Query   │    │Database │
│Service  │    │Service  │    │Cluster  │
│ :8084   │    │ :8085   │    │         │
└─────────┘    └─────────┘    └─────────┘
```

## 📦 **Services Overview**

### **Core Services**

| Service | Port | Purpose | Database |
|---------|------|---------|----------|
| **Payment Service** | 8080 | Authorization orchestration, 2PC coordinator | payments_db |
| **Risk Service** | 8081 | Fraud detection, velocity limiting, business rules | risk_db |
| **Clearing Service** | 8082 | External network simulation, 2PC participant | clearing_db |
| **Ledger Service** | 8083 | Double-entry bookkeeping, accounting | ledger_db |
| **Reversal Service** | 8084 | Compensation transactions, retry logic | - |
| **Query Service** | 8085 | Read-only operations, reporting | payments_db |

### **Infrastructure Services**

| Service | Port | Purpose |
|---------|------|---------|
| **API Gateway** | 80/443 | Request routing, load balancing, SSL termination |
| **Redis** | 6379 | Event streaming, pub/sub messaging |
| **PostgreSQL Cluster** | 5432-5435 | Per-service databases |
| **Prometheus** | 9090 | Metrics collection and alerting |
| **Grafana** | 3000 | Monitoring dashboards |

## 🚀 **Quick Start**

### **Prerequisites**
```bash
# Install Docker & Docker Compose
sudo apt-get update
sudo apt-get install docker.io docker-compose

# Clone the repository
git clone <repo-url>
cd mini-visa/microservices
```

### **Start All Services**
```bash
# Build and start all services
docker-compose up --build

# Start in background
docker-compose up -d --build

# View logs
docker-compose logs -f payment-service
```

### **Service Health Checks**
```bash
# Check all services are running
docker-compose ps

# Health check endpoints
curl http://localhost:8080/health    # Payment Service
curl http://localhost:8081/health    # Risk Service  
curl http://localhost:8082/health    # Clearing Service
curl http://localhost:8083/health    # Ledger Service
```

## 🧪 **Testing the System**

### **1. Complete Payment Flow**
```bash
# Test authorization request
curl -X POST http://localhost/payments/authorize \
  -H "Content-Type: application/json" \
  -d '{
    "request_id": "test_001",
    "pan": "4111111111111111",
    "amount": "100.00",
    "currency": "USD",
    "merchant_id": "MERCHANT001"
  }'

# Expected response:
# {
#   "status": "APPROVED",
#   "txn_id": "visa_test_001_1640995200",
#   "timestamp": 1640995200
# }
```

### **2. Risk Evaluation**
```bash
# Test risk service directly  
curl -X POST http://localhost:8081/risk/evaluate \
  -H "Content-Type: application/json" \
  -d '{
    "pan": "4111111111111111",
    "amount": "100.00",
    "currency": "USD",
    "merchant_id": "MERCHANT001"
  }'

# View risk rules
curl http://localhost:8081/risk/rules
```

### **3. Clearing Operations**
```bash
# Test 2-Phase Commit flow
# Phase 1: Prepare
curl -X POST http://localhost:8082/clearing/prepare \
  -H "Content-Type: application/json" \
  -d '{
    "txn_id": "visa_test_123",
    "pan": "4111111111111111", 
    "amount": "100.00"
  }'

# Phase 2: Commit
curl -X POST http://localhost:8082/clearing/commit \
  -H "Content-Type: application/json" \
  -d '{"txn_id": "visa_test_123"}'
```

### **4. Load Testing**
```bash
# Install k6 for load testing
sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys C5AD17C747E3415A3642D57D77C6C491D6AC1D69
echo "deb https://dl.k6.io/deb stable main" | sudo tee /etc/apt/sources.list.d/k6.list
sudo apt-get update
sudo apt-get install k6

# Run load test
k6 run tests/load-test.js
```

## 📊 **Monitoring & Observability**

### **Metrics Endpoints**
```bash
# Service-specific metrics
curl http://localhost:8080/metrics    # Payment metrics
curl http://localhost:8081/metrics    # Risk metrics
curl http://localhost:8082/metrics    # Clearing metrics

# Prometheus metrics aggregation
curl http://localhost:9090/metrics
```

### **Grafana Dashboards**
- **URL**: http://localhost:3000
- **Credentials**: admin/admin123
- **Pre-configured dashboards**:
  - Payment System Overview
  - Service Health & Performance
  - Error Rate & Latency Monitoring
  - Business Metrics (Approval Rate, Volume)

### **Key Metrics**
| Metric | Description | Alert Threshold |
|--------|-------------|-----------------|
| `payment_success_rate` | Percentage of approved transactions | < 95% |
| `service_response_time_p95` | 95th percentile response time | > 1000ms |
| `risk_approval_rate` | Risk service approval rate | < 90% |
| `clearing_success_rate` | Clearing 2PC success rate | < 98% |
| `database_connections` | Active DB connections | > 80% of max |

## 🔧 **Configuration**

### **Environment Variables**

#### **Payment Service**
```bash
PORT=8080
RISK_SERVICE_URL=http://risk-service:8081
CLEARING_SERVICE_URL=http://clearing-service:8082
LEDGER_SERVICE_URL=http://ledger-service:8083
DB_URI=postgresql://user:pass@host:5432/payments_db
MAX_THREADS=10
QUEUE_CAPACITY=1000
```

#### **Risk Service**  
```bash
PORT=8081
RISK_MAX_AMOUNT=10000
RISK_VELOCITY_LIMIT=20
RISK_VELOCITY_WINDOW=60
DB_URI=postgresql://user:pass@host:5432/risk_db
```

#### **Clearing Service**
```bash
PORT=8082
CLEARING_SIMULATE_FAILURES=5    # 5% failure rate for testing
CLEARING_PREPARE_TIMEOUT=30
CLEARING_COMMIT_TIMEOUT=30
DB_URI=postgresql://user:pass@host:5432/clearing_db
```

## 🛠️ **Development**

### **Building Individual Services**
```bash
# Build specific service
cd payment-service
make deps    # Install dependencies
make all     # Build binary
make clean   # Clean build artifacts

# Docker build
make docker-build
make docker-run
```

### **Adding New Services**
1. Create service directory: `mkdir new-service`
2. Implement service in C with HTTP API
3. Add Dockerfile and Makefile
4. Update docker-compose.yml
5. Add to nginx routing
6. Update monitoring configuration

### **Database Schema Management**
```bash
# View schemas
docker-compose exec postgres-payments psql -U payment_user -d payments_db -c "\\d"

# Run migrations
docker-compose exec postgres-payments psql -U payment_user -d payments_db -f /migrations/001_add_indexes.sql
```

## 🔐 **Security Features**

### **Container Security**
- Non-root containers for all services
- Read-only root filesystems
- Minimal base images (Alpine Linux)
- Security scanning with Docker Security Scan

### **Network Security**
- Isolated Docker network (`mini-visa-network`)
- Service-to-service communication only through defined ports
- API Gateway as single entry point
- No direct database access from external network

### **Data Protection**
- PAN masking in logs (show only first 6 + last 4 digits)
- Environment variable injection for secrets
- TLS termination at API Gateway
- Database connection encryption

## 🚨 **Troubleshooting**

### **Common Issues**

**Services won't start:**
```bash
# Check Docker daemon
sudo systemctl status docker

# View service logs
docker-compose logs service-name

# Check resource usage
docker stats
```

**Database connection issues:**
```bash
# Test database connectivity
docker-compose exec postgres-payments pg_isready -U payment_user

# View database logs
docker-compose logs postgres-payments
```

**High memory usage:**
```bash
# Monitor container resources
docker stats --no-stream

# Restart specific service
docker-compose restart payment-service
```

### **Performance Tuning**

**Database Optimization:**
```sql
-- Add indexes for better query performance
CREATE INDEX CONCURRENTLY idx_transactions_request_id ON transactions(request_id);
CREATE INDEX CONCURRENTLY idx_transactions_created_at ON transactions(created_at);

-- Connection pooling settings
ALTER SYSTEM SET max_connections = 200;
ALTER SYSTEM SET shared_buffers = '256MB';
```

**Application Tuning:**
```bash
# Increase worker threads for high load
export MAX_THREADS=20
export QUEUE_CAPACITY=2000

# Enable HTTP keepalive
export HTTP_KEEPALIVE=true
```

## 📈 **Scaling Strategy**

### **Horizontal Scaling**
```yaml
# docker-compose.override.yml
version: '3.8'
services:
  payment-service:
    deploy:
      replicas: 3
    environment:
      - PORT=8080
  
  # Add load balancer
  haproxy:
    image: haproxy:alpine
    ports:
      - "80:80"
    volumes:
      - ./haproxy.cfg:/usr/local/etc/haproxy/haproxy.cfg
```

### **Database Scaling**
- Read replicas for query-heavy services
- Connection pooling with PgBouncer
- Database partitioning for large tables
- Separate OLTP and OLAP workloads

## 🎯 **Production Deployment**

### **Kubernetes Deployment**
```bash
# Use the Kubernetes manifests from k8s/ directory
kubectl apply -k k8s/

# Or use Helm chart
helm install mini-visa ./helm-chart --namespace production
```

### **CI/CD Pipeline**
```yaml
# .github/workflows/microservices.yml
name: Microservices CI/CD
on: [push, pull_request]
jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build services
        run: docker-compose build
      - name: Run tests
        run: docker-compose run --rm test-runner
      - name: Security scan
        run: docker scan payment-service:latest
```

---

## 📞 **Support**

- **Documentation**: See individual service README files
- **Issues**: Create GitHub issues for bugs and feature requests  
- **Monitoring**: Check Grafana dashboards for system health
- **Logs**: Use `docker-compose logs -f <service-name>` for debugging

**Architecture Decision Records (ADRs)**: See `docs/architecture/` for design decisions and trade-offs.