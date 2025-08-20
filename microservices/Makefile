# Mini-Visa Microservices Build System
# Master Makefile for building all microservices

.PHONY: all clean deps build test docker-build docker-run docker-stop help

# Service directories
SERVICES = payment-service risk-service clearing-service ledger-service reversal-service query-service

# Docker compose files
COMPOSE_FILE = docker-compose.yml
COMPOSE_OVERRIDE = docker-compose.override.yml

# Default target
all: build

# Build all services
build:
	@echo "🏗️  Building all microservices..."
	@for service in $(SERVICES); do \
		echo "Building $$service..."; \
		$(MAKE) -C $$service all; \
	done
	@echo "✅ All services built successfully"

# Clean all build artifacts
clean:
	@echo "🧹 Cleaning all build artifacts..."
	@for service in $(SERVICES); do \
		echo "Cleaning $$service..."; \
		$(MAKE) -C $$service clean; \
	done
	@echo "✅ All build artifacts cleaned"

# Install dependencies for all services
deps:
	@echo "📦 Installing dependencies for all services..."
	@for service in $(SERVICES); do \
		echo "Installing deps for $$service..."; \
		$(MAKE) -C $$service deps; \
	done
	@echo "✅ All dependencies installed"

# Run tests for all services
test:
	@echo "🧪 Running tests for all services..."
	@for service in $(SERVICES); do \
		echo "Testing $$service..."; \
		$(MAKE) -C $$service test; \
	done
	@echo "✅ All tests completed"

# Build Docker images for all services
docker-build:
	@echo "🐳 Building Docker images for all services..."
	docker-compose build --parallel
	@echo "✅ All Docker images built"

# Start all services with Docker Compose
docker-run: docker-build
	@echo "🚀 Starting all services..."
	docker-compose up -d
	@echo "✅ All services started"
	@echo ""
	@echo "🌐 Service endpoints:"
	@echo "  Payment Service:  http://localhost:8080"
	@echo "  Risk Service:     http://localhost:8081"  
	@echo "  Clearing Service: http://localhost:8082"
	@echo "  Ledger Service:   http://localhost:8083"
	@echo "  Reversal Service: http://localhost:8084"
	@echo "  Query Service:    http://localhost:8085"
	@echo "  API Gateway:      http://localhost"
	@echo "  Grafana:          http://localhost:3000 (admin/admin123)"
	@echo "  Prometheus:       http://localhost:9090"

# Stop all services
docker-stop:
	@echo "⏹️  Stopping all services..."
	docker-compose down
	@echo "✅ All services stopped"

# Remove all containers and volumes
docker-clean: docker-stop
	@echo "🗑️  Removing containers and volumes..."
	docker-compose down -v --remove-orphans
	docker system prune -f
	@echo "✅ Docker cleanup completed"

# View logs for all services
logs:
	docker-compose logs -f

# View logs for specific service
logs-%:
	docker-compose logs -f $*

# Check service health
health:
	@echo "🏥 Checking service health..."
	@curl -f http://localhost:8080/health 2>/dev/null && echo "✅ Payment Service: healthy" || echo "❌ Payment Service: unhealthy"
	@curl -f http://localhost:8081/health 2>/dev/null && echo "✅ Risk Service: healthy" || echo "❌ Risk Service: unhealthy"
	@curl -f http://localhost:8082/health 2>/dev/null && echo "✅ Clearing Service: healthy" || echo "❌ Clearing Service: unhealthy"
	@curl -f http://localhost:8083/health 2>/dev/null && echo "✅ Ledger Service: healthy" || echo "❌ Ledger Service: unhealthy"
	@curl -f http://localhost:8084/health 2>/dev/null && echo "✅ Reversal Service: healthy" || echo "❌ Reversal Service: unhealthy"
	@curl -f http://localhost:8085/health 2>/dev/null && echo "✅ Query Service: healthy" || echo "❌ Query Service: unhealthy"

# Run integration tests
integration-test: docker-run
	@echo "🔗 Running integration tests..."
	@sleep 10  # Wait for services to start
	@./tests/integration-test.sh
	@echo "✅ Integration tests completed"

# Load testing
load-test:
	@echo "⚡ Running load tests..."
	@if command -v k6 >/dev/null 2>&1; then \
		k6 run tests/load-test.js; \
	else \
		echo "❌ k6 not installed. Install with: sudo apt-get install k6"; \
		exit 1; \
	fi

# Performance benchmarking
benchmark:
	@echo "📊 Running performance benchmarks..."
	@./scripts/benchmark.sh

# Security scanning
security-scan:
	@echo "🔒 Running security scans..."
	@for service in $(SERVICES); do \
		echo "Scanning $$service..."; \
		docker scan $$service:latest || true; \
	done

# Database migration
migrate:
	@echo "🗄️  Running database migrations..."
	@docker-compose exec postgres-payments psql -U payment_user -d payments_db -f /migrations/payments.sql
	@docker-compose exec postgres-risk psql -U risk_user -d risk_db -f /migrations/risk.sql
	@docker-compose exec postgres-clearing psql -U clearing_user -d clearing_db -f /migrations/clearing.sql
	@docker-compose exec postgres-ledger psql -U ledger_user -d ledger_db -f /migrations/ledger.sql
	@echo "✅ Database migrations completed"

# Backup databases
backup:
	@echo "💾 Creating database backups..."
	@mkdir -p backups
	@docker-compose exec postgres-payments pg_dump -U payment_user payments_db > backups/payments_$(shell date +%Y%m%d_%H%M%S).sql
	@docker-compose exec postgres-risk pg_dump -U risk_user risk_db > backups/risk_$(shell date +%Y%m%d_%H%M%S).sql
	@docker-compose exec postgres-clearing pg_dump -U clearing_user clearing_db > backups/clearing_$(shell date +%Y%m%d_%H%M%S).sql
	@docker-compose exec postgres-ledger pg_dump -U ledger_user ledger_db > backups/ledger_$(shell date +%Y%m%d_%H%M%S).sql
	@echo "✅ Database backups created in ./backups/"

# Restore database from backup
restore-%:
	@echo "🔄 Restoring database from backup..."
	@if [ -f "$*.sql" ]; then \
		docker-compose exec -T postgres-payments psql -U payment_user -d payments_db < $*.sql; \
		echo "✅ Database restored from $*.sql"; \
	else \
		echo "❌ Backup file $*.sql not found"; \
		exit 1; \
	fi

# Development mode (with hot reload)
dev:
	@echo "🔧 Starting development mode..."
	docker-compose -f $(COMPOSE_FILE) -f docker-compose.dev.yml up --build

# Production deployment
deploy:
	@echo "🚀 Deploying to production..."
	docker-compose -f $(COMPOSE_FILE) -f docker-compose.prod.yml up -d --build
	@echo "✅ Production deployment completed"

# Create monitoring dashboards
monitoring:
	@echo "📈 Setting up monitoring..."
	@./scripts/setup-monitoring.sh
	@echo "✅ Monitoring configured"

# Generate API documentation
docs:
	@echo "📚 Generating API documentation..."
	@./scripts/generate-docs.sh
	@echo "✅ API documentation generated"

# Code quality checks
quality:
	@echo "🔍 Running code quality checks..."
	@for service in $(SERVICES); do \
		echo "Checking $$service..."; \
		$(MAKE) -C $$service lint || true; \
	done
	@echo "✅ Code quality checks completed"

# CI/CD pipeline simulation
ci:
	@echo "🔄 Running CI/CD pipeline..."
	@$(MAKE) clean
	@$(MAKE) deps
	@$(MAKE) build
	@$(MAKE) test
	@$(MAKE) docker-build
	@$(MAKE) integration-test
	@$(MAKE) security-scan
	@echo "✅ CI/CD pipeline completed successfully"

# Show service status
status:
	@echo "📊 Service Status:"
	@docker-compose ps

# Show resource usage
resources:
	@echo "💻 Resource Usage:"
	@docker stats --no-stream

# Interactive shell for debugging
shell-%:
	docker-compose exec $* /bin/sh

# Database shell access
db-shell-%:
	@case "$*" in \
		payments) docker-compose exec postgres-payments psql -U payment_user -d payments_db ;; \
		risk) docker-compose exec postgres-risk psql -U risk_user -d risk_db ;; \
		clearing) docker-compose exec postgres-clearing psql -U clearing_user -d clearing_db ;; \
		ledger) docker-compose exec postgres-ledger psql -U ledger_user -d ledger_db ;; \
		*) echo "❌ Unknown database: $*. Use: payments, risk, clearing, or ledger" ;; \
	esac

# Configuration validation
validate:
	@echo "✅ Validating configuration..."
	@docker-compose config --quiet
	@echo "✅ Configuration is valid"

# Show help
help:
	@echo "🆘 Mini-Visa Microservices Build System"
	@echo ""
	@echo "📋 Available commands:"
	@echo ""
	@echo "🏗️  Building:"
	@echo "  make build         - Build all services"
	@echo "  make clean         - Clean build artifacts"
	@echo "  make deps          - Install dependencies"
	@echo ""
	@echo "🐳 Docker operations:"
	@echo "  make docker-build  - Build Docker images"
	@echo "  make docker-run    - Start all services"
	@echo "  make docker-stop   - Stop all services"
	@echo "  make docker-clean  - Remove containers and volumes"
	@echo ""
	@echo "🧪 Testing:"
	@echo "  make test          - Run unit tests"
	@echo "  make integration-test - Run integration tests"
	@echo "  make load-test     - Run load tests"
	@echo "  make benchmark     - Performance benchmarking"
	@echo ""
	@echo "🔍 Monitoring:"
	@echo "  make health        - Check service health"
	@echo "  make status        - Show service status"
	@echo "  make resources     - Show resource usage"
	@echo "  make logs          - View all logs"
	@echo "  make logs-<service> - View specific service logs"
	@echo ""
	@echo "🗄️  Database:"
	@echo "  make migrate       - Run database migrations"
	@echo "  make backup        - Create database backups"
	@echo "  make restore-<file> - Restore from backup"
	@echo "  make db-shell-<db> - Database shell access"
	@echo ""
	@echo "🚀 Deployment:"
	@echo "  make dev           - Development mode"
	@echo "  make deploy        - Production deployment"
	@echo "  make monitoring    - Setup monitoring"
	@echo ""
	@echo "🔧 Development:"
	@echo "  make quality       - Code quality checks"
	@echo "  make security-scan - Security scanning"
	@echo "  make docs          - Generate documentation"
	@echo "  make shell-<service> - Interactive shell"
	@echo ""
	@echo "🔄 CI/CD:"
	@echo "  make ci            - Full CI/CD pipeline"
	@echo "  make validate      - Validate configuration"
	@echo ""
	@echo "💡 Examples:"
	@echo "  make docker-run    # Start all services"
	@echo "  make logs-payment-service # View payment service logs"
	@echo "  make db-shell-payments    # Access payments database"
	@echo "  make health        # Check all service health"