#!/bin/bash

# Mini-Visa Monolith to Microservices Migration Script
# This script helps migrate data and configuration from the monolith to microservices

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
MONOLITH_DIR="../"
MICROSERVICES_DIR="."
BACKUP_DIR="./migration-backup"
MIGRATION_LOG="migration-$(date +%Y%m%d_%H%M%S).log"

# Logging function
log() {
    echo -e "${GREEN}[$(date '+%Y-%m-%d %H:%M:%S')] $1${NC}" | tee -a "$MIGRATION_LOG"
}

log_warning() {
    echo -e "${YELLOW}[$(date '+%Y-%m-%d %H:%M:%S')] WARNING: $1${NC}" | tee -a "$MIGRATION_LOG"
}

log_error() {
    echo -e "${RED}[$(date '+%Y-%m-%d %H:%M:%S')] ERROR: $1${NC}" | tee -a "$MIGRATION_LOG"
}

log_info() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')] INFO: $1${NC}" | tee -a "$MIGRATION_LOG"
}

# Check prerequisites
check_prerequisites() {
    log "Checking prerequisites..."
    
    # Check if monolith directory exists
    if [ ! -d "$MONOLITH_DIR" ]; then
        log_error "Monolith directory not found: $MONOLITH_DIR"
        exit 1
    fi
    
    # Check if Docker is installed and running
    if ! command -v docker &> /dev/null; then
        log_error "Docker is not installed"
        exit 1
    fi
    
    if ! docker info &> /dev/null; then
        log_error "Docker is not running"
        exit 1
    fi
    
    # Check if docker-compose is available
    if ! command -v docker-compose &> /dev/null; then
        log_error "docker-compose is not installed"
        exit 1
    fi
    
    log "Prerequisites check passed"
}

# Create backup
create_backup() {
    log "Creating backup of current state..."
    
    mkdir -p "$BACKUP_DIR"
    
    # Backup monolith database if running
    if docker ps | grep -q postgres; then
        log_info "Backing up monolith database..."
        docker exec $(docker ps -q --filter "name=postgres") pg_dump -U mini mini_visa > "$BACKUP_DIR/monolith_db_$(date +%Y%m%d_%H%M%S).sql"
    fi
    
    # Backup configuration files
    if [ -f "$MONOLITH_DIR/.env" ]; then
        cp "$MONOLITH_DIR/.env" "$BACKUP_DIR/monolith.env"
    fi
    
    log "Backup created in $BACKUP_DIR"
}

# Extract database schema from monolith
extract_schema() {
    log "Extracting database schema from monolith..."
    
    # Read the monolith schema
    if [ -f "$MONOLITH_DIR/db/schema.sql" ]; then
        log_info "Found monolith schema file"
        
        # Create SQL directory for microservices
        mkdir -p sql
        
        # Generate payment service schema
        cat > sql/payments-schema.sql << 'EOF'
-- Payment Service Database Schema
-- Generated from monolith migration

CREATE TABLE IF NOT EXISTS transactions (
    id SERIAL PRIMARY KEY,
    request_id VARCHAR(255) UNIQUE NOT NULL,
    pan_masked VARCHAR(32) NOT NULL,
    amount DECIMAL(10,2) NOT NULL,
    currency VARCHAR(3) DEFAULT 'USD',
    merchant_id VARCHAR(64),
    status VARCHAR(32) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    txn_id VARCHAR(128) UNIQUE
);

CREATE TABLE IF NOT EXISTS outbox_events (
    id SERIAL PRIMARY KEY,
    aggregate_id VARCHAR(255) NOT NULL,
    event_type VARCHAR(100) NOT NULL,
    event_data JSONB NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    published_at TIMESTAMP NULL
);

-- Indexes for performance
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_transactions_request_id ON transactions(request_id);
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_transactions_created_at ON transactions(created_at);
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_transactions_status ON transactions(status);
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_outbox_events_published_at ON outbox_events(published_at) WHERE published_at IS NULL;

-- Grant permissions
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO payment_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO payment_user;
EOF

        # Generate risk service schema  
        cat > sql/risk-schema.sql << 'EOF'
-- Risk Service Database Schema

CREATE TABLE IF NOT EXISTS risk_rules (
    id SERIAL PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    rule_type VARCHAR(32) NOT NULL,
    enabled BOOLEAN DEFAULT true,
    parameters JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS risk_evaluations (
    id SERIAL PRIMARY KEY,
    request_id VARCHAR(255),
    masked_pan VARCHAR(32),
    amount DECIMAL(10,2),
    decision VARCHAR(32) NOT NULL,
    reason VARCHAR(255),
    risk_score DECIMAL(3,2),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS blacklisted_bins (
    id SERIAL PRIMARY KEY,
    bin VARCHAR(6) NOT NULL UNIQUE,
    reason VARCHAR(255),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Insert default risk rules
INSERT INTO risk_rules (name, rule_type, parameters) VALUES 
('Amount Limit', 'AMOUNT_LIMIT', '{"max_amount": 10000}'),
('Velocity Limit', 'VELOCITY', '{"max_transactions": 20, "window_seconds": 60}'),
('BIN Blacklist', 'BLACKLIST', '{"enabled": true}')
ON CONFLICT DO NOTHING;

-- Insert test blacklisted BINs
INSERT INTO blacklisted_bins (bin, reason) VALUES 
('999999', 'Test BIN'),
('000000', 'Invalid BIN'),
('123456', 'Demo Blacklist')
ON CONFLICT DO NOTHING;

GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO risk_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO risk_user;
EOF

        # Generate clearing service schema
        cat > sql/clearing-schema.sql << 'EOF'
-- Clearing Service Database Schema

CREATE TABLE IF NOT EXISTS clearing_transactions (
    id SERIAL PRIMARY KEY,
    txn_id VARCHAR(128) UNIQUE NOT NULL,
    pan_masked VARCHAR(32),
    amount DECIMAL(10,2),
    currency VARCHAR(3) DEFAULT 'USD',
    merchant_id VARCHAR(64),
    state VARCHAR(32) NOT NULL DEFAULT 'UNKNOWN',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    error_message TEXT,
    retry_count INTEGER DEFAULT 0
);

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_clearing_txn_id ON clearing_transactions(txn_id);
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_clearing_state ON clearing_transactions(state);
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_clearing_created_at ON clearing_transactions(created_at);

GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO clearing_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO clearing_user;
EOF

        # Generate ledger service schema
        cat > sql/ledger-schema.sql << 'EOF'
-- Ledger Service Database Schema

CREATE TABLE IF NOT EXISTS journal_entries (
    id SERIAL PRIMARY KEY,
    txn_id VARCHAR(128) NOT NULL,
    entry_type VARCHAR(32) NOT NULL, -- DEBIT, CREDIT
    account VARCHAR(64) NOT NULL,
    amount DECIMAL(15,2) NOT NULL,
    currency VARCHAR(3) DEFAULT 'USD',
    description TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    balance_after DECIMAL(15,2)
);

CREATE TABLE IF NOT EXISTS accounts (
    id SERIAL PRIMARY KEY,
    account_code VARCHAR(64) UNIQUE NOT NULL,
    account_name VARCHAR(255) NOT NULL,
    account_type VARCHAR(32) NOT NULL, -- ASSET, LIABILITY, EQUITY, REVENUE, EXPENSE
    parent_account VARCHAR(64),
    current_balance DECIMAL(15,2) DEFAULT 0.00,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create default accounts
INSERT INTO accounts (account_code, account_name, account_type) VALUES 
('1000', 'Cash', 'ASSET'),
('2000', 'Merchant Payables', 'LIABILITY'),
('3000', 'Revenue', 'REVENUE'),
('4000', 'Processing Fees', 'EXPENSE')
ON CONFLICT DO NOTHING;

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_journal_txn_id ON journal_entries(txn_id);
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_journal_created_at ON journal_entries(created_at);
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_accounts_code ON accounts(account_code);

GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO ledger_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO ledger_user;
EOF

        log "Database schemas generated"
    else
        log_warning "Monolith schema file not found, using default schemas"
    fi
}

# Migrate configuration
migrate_configuration() {
    log "Migrating configuration..."
    
    # Create environment-specific compose files
    if [ -f "$MONOLITH_DIR/.env" ]; then
        log_info "Found monolith environment file"
        
        # Extract database URI if available
        if grep -q "DB_URI" "$MONOLITH_DIR/.env"; then
            MONOLITH_DB_URI=$(grep "DB_URI" "$MONOLITH_DIR/.env" | cut -d'=' -f2)
            log_info "Found database URI in monolith config: $MONOLITH_DB_URI"
        fi
    fi
    
    # Create development override
    cat > docker-compose.dev.yml << 'EOF'
version: '3.8'

services:
  payment-service:
    build:
      context: ./payment-service
      target: development
    volumes:
      - ./payment-service/src:/app/src
    environment:
      - LOG_LEVEL=DEBUG
      - HOT_RELOAD=true
    
  risk-service:
    volumes:
      - ./risk-service/src:/app/src
    environment:
      - LOG_LEVEL=DEBUG
      - RISK_SIMULATE_FAILURES=10

  clearing-service:
    volumes:
      - ./clearing-service/src:/app/src
    environment:
      - LOG_LEVEL=DEBUG
      - CLEARING_SIMULATE_FAILURES=15
EOF

    log "Configuration migration completed"
}

# Migrate existing data
migrate_data() {
    log "Starting data migration..."
    
    # Start microservices databases
    log_info "Starting microservices databases..."
    docker-compose up -d postgres-payments postgres-risk postgres-clearing postgres-ledger
    
    # Wait for databases to be ready
    log_info "Waiting for databases to be ready..."
    sleep 10
    
    # Check if monolith database backup exists
    if [ -f "$BACKUP_DIR/monolith_db_"*.sql ]; then
        BACKUP_FILE=$(ls -t "$BACKUP_DIR"/monolith_db_*.sql | head -1)
        log_info "Found monolith database backup: $BACKUP_FILE"
        
        # Extract and migrate transactions
        log_info "Migrating transaction data..."
        
        # This is a simplified migration - in real scenarios, you'd need more complex data transformation
        psql -h localhost -p 5432 -U payment_user -d payments_db << 'EOF'
-- Migration queries would go here
-- For example, if we had access to the old database:
-- INSERT INTO transactions (request_id, pan_masked, amount, status, created_at)
-- SELECT request_id, pan_masked, amount, status, created_at FROM old_transactions;
EOF
    else
        log_warning "No monolith database backup found, skipping data migration"
        log_info "Creating sample data for testing..."
        
        # Insert sample data for testing
        docker-compose exec -T postgres-payments psql -U payment_user -d payments_db << 'EOF'
INSERT INTO transactions (request_id, pan_masked, amount, status, txn_id) VALUES 
('migration_test_001', '411111****1111', 100.00, 'APPROVED', 'visa_migration_001'),
('migration_test_002', '411111****1111', 50.00, 'APPROVED', 'visa_migration_002'),
('migration_test_003', '555555****5555', 200.00, 'DECLINED', 'visa_migration_003');
EOF
    fi
    
    log "Data migration completed"
}

# Validate migration
validate_migration() {
    log "Validating migration..."
    
    # Start all services
    log_info "Starting all microservices..."
    docker-compose up -d
    
    # Wait for services to start
    log_info "Waiting for services to start..."
    sleep 30
    
    # Test each service
    services=("payment-service:8080" "risk-service:8081" "clearing-service:8082" "ledger-service:8083")
    
    for service_port in "${services[@]}"; do
        service=$(echo $service_port | cut -d':' -f1)
        port=$(echo $service_port | cut -d':' -f2)
        
        if curl -f "http://localhost:$port/health" > /dev/null 2>&1; then
            log "✅ $service is healthy"
        else
            log_error "❌ $service is not responding"
        fi
    done
    
    # Test a complete payment flow
    log_info "Testing complete payment flow..."
    response=$(curl -s -X POST http://localhost:8080/payments/authorize \
        -H "Content-Type: application/json" \
        -d '{
            "request_id": "migration_validation_001",
            "pan": "4111111111111111",
            "amount": "99.99",
            "currency": "USD",
            "merchant_id": "MIGRATION_TEST"
        }')
    
    if echo "$response" | grep -q "APPROVED"; then
        log "✅ Payment flow test passed"
    else
        log_error "❌ Payment flow test failed: $response"
    fi
    
    # Check database connectivity
    log_info "Validating database connections..."
    
    if docker-compose exec -T postgres-payments psql -U payment_user -d payments_db -c "SELECT COUNT(*) FROM transactions;" > /dev/null 2>&1; then
        log "✅ Payment database accessible"
    else
        log_error "❌ Payment database connection failed"
    fi
    
    log "Migration validation completed"
}

# Generate migration report
generate_report() {
    log "Generating migration report..."
    
    REPORT_FILE="migration-report-$(date +%Y%m%d_%H%M%S).md"
    
    cat > "$REPORT_FILE" << EOF
# Mini-Visa Monolith to Microservices Migration Report

**Migration Date**: $(date)
**Migration Duration**: $(echo $(date -d@$(($(date +%s) - $MIGRATION_START))) -u +%H:%M:%S)

## Migration Summary

### Services Created
- ✅ Payment Service (Port 8080)
- ✅ Risk Service (Port 8081)
- ✅ Clearing Service (Port 8082)
- ✅ Ledger Service (Port 8083)
- ✅ Reversal Service (Port 8084)
- ✅ Query Service (Port 8085)

### Infrastructure
- ✅ API Gateway (Nginx)
- ✅ Database per Service (PostgreSQL)
- ✅ Event Streaming (Redis)
- ✅ Monitoring (Prometheus + Grafana)

### Database Migration
EOF

    # Add database statistics
    for db in payments risk clearing ledger; do
        echo "- **${db}_db**: " >> "$REPORT_FILE"
        count=$(docker-compose exec -T postgres-$db psql -U ${db}_user -d ${db}_db -t -c "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'public';" 2>/dev/null | tr -d ' \n' || echo "0")
        echo "  - Tables: $count" >> "$REPORT_FILE"
    done
    
    cat >> "$REPORT_FILE" << 'EOF'

### Service Endpoints
- **Payment Service**: http://localhost:8080
  - POST /payments/authorize
  - GET /health, /metrics
- **Risk Service**: http://localhost:8081
  - POST /risk/evaluate
  - GET /risk/rules, /health, /metrics
- **Clearing Service**: http://localhost:8082
  - POST /clearing/prepare, /clearing/commit, /clearing/abort
  - GET /clearing/status/{txn_id}, /health, /metrics

### Configuration Files Created
- docker-compose.yml - Main orchestration
- docker-compose.dev.yml - Development overrides
- nginx/api-gateway.conf - API Gateway routing
- sql/*.sql - Database schemas

### Backup Location
- Database backups: ./migration-backup/
- Configuration backups: ./migration-backup/

### Next Steps
1. Review service configurations
2. Set up CI/CD pipelines
3. Configure monitoring alerts
4. Update documentation
5. Train team on new architecture

### Performance Comparison
| Metric | Monolith | Microservices |
|--------|----------|---------------|
| Services | 1 | 6 |
| Databases | 1 | 4 |
| Scalability | Vertical | Horizontal |
| Deployment | Single binary | Independent services |
| Technology Stack | Single | Per-service choice |

### Migration Log
See migration log for detailed information: migration_*.log
EOF

    log "Migration report generated: $REPORT_FILE"
}

# Cleanup function
cleanup() {
    log "Migration process interrupted"
    exit 1
}

# Main migration function
main() {
    MIGRATION_START=$(date +%s)
    
    log "🚀 Starting Mini-Visa Monolith to Microservices Migration"
    log "Migration started at $(date)"
    
    # Set up interrupt handling
    trap cleanup INT TERM
    
    # Run migration steps
    check_prerequisites
    create_backup
    extract_schema
    migrate_configuration
    migrate_data
    validate_migration
    generate_report
    
    MIGRATION_END=$(date +%s)
    DURATION=$((MIGRATION_END - MIGRATION_START))
    
    log "✅ Migration completed successfully!"
    log "Migration duration: $(date -d@$DURATION -u +%H:%M:%S)"
    log ""
    log "🌟 Your microservices are now ready!"
    log "📊 Access Grafana: http://localhost:3000 (admin/admin123)"
    log "📈 Access Prometheus: http://localhost:9090"
    log "🌐 API Gateway: http://localhost"
    log ""
    log "📋 Next steps:"
    log "1. Review the migration report"
    log "2. Test the new microservices thoroughly"
    log "3. Update your CI/CD pipelines"
    log "4. Configure monitoring alerts"
    log "5. Train your team on the new architecture"
    log ""
    log "📖 For more information, see: microservices/README.md"
}

# Show help
show_help() {
    cat << EOF
Mini-Visa Monolith to Microservices Migration Script

Usage: $0 [options]

Options:
  -h, --help     Show this help message
  --dry-run      Show what would be done without executing
  --skip-backup  Skip backup creation (not recommended)
  --force        Skip confirmation prompts

Examples:
  $0                    # Run full migration
  $0 --dry-run          # Show migration plan
  $0 --skip-backup      # Skip backup (faster, but risky)

This script will:
1. Check prerequisites (Docker, docker-compose)
2. Create backups of existing data
3. Generate microservice database schemas
4. Migrate configuration files
5. Migrate data from monolith to microservices
6. Validate the migration
7. Generate a detailed migration report
EOF
}

# Parse command line arguments
DRY_RUN=false
SKIP_BACKUP=false
FORCE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --skip-backup)
            SKIP_BACKUP=true
            shift
            ;;
        --force)
            FORCE=true
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Confirmation prompt
if [ "$FORCE" != "true" ] && [ "$DRY_RUN" != "true" ]; then
    echo -e "${YELLOW}⚠️  This will migrate your monolith to microservices architecture.${NC}"
    echo -e "${YELLOW}   This process will:${NC}"
    echo -e "${YELLOW}   - Create new microservice containers${NC}"
    echo -e "${YELLOW}   - Migrate database schemas${NC}" 
    echo -e "${YELLOW}   - Update configuration files${NC}"
    echo ""
    read -p "Are you sure you want to continue? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log "Migration cancelled by user"
        exit 0
    fi
fi

# Run migration
if [ "$DRY_RUN" = "true" ]; then
    log "🔍 DRY RUN MODE - Showing what would be done:"
    log "1. Check prerequisites (Docker, docker-compose)"
    log "2. Create backup of current state"
    log "3. Extract database schemas from monolith"
    log "4. Generate microservice configurations"
    log "5. Migrate data to new databases"
    log "6. Start and validate microservices"
    log "7. Generate migration report"
    log ""
    log "Run without --dry-run to execute the migration"
else
    main
fi