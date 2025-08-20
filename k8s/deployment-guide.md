# Mini-Visa Kubernetes Deployment Guide

## Overview
This guide covers the complete Kubernetes deployment strategy for the mini-visa payment system, transforming the monolithic C application into a cloud-native microservices architecture.

## Architecture Overview

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   API Gateway   │────│  Payment Service │────│   Risk Service  │
│  (Istio/Nginx)  │    │     (Core)       │    │   (Rules)       │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                │                        │
                                ▼                        ▼
                       ┌─────────────────┐    ┌─────────────────┐
                       │ Clearing Service │    │  Redis Cluster  │
                       │   (External)     │    │ (Event Stream)  │
                       └─────────────────┘    └─────────────────┘
                                │                        │
                                ▼                        ▼
                       ┌─────────────────┐    ┌─────────────────┐
                       │ Ledger Service  │    │ Reversal Service│
                       │ (Accounting)    │    │ (Compensation)  │
                       └─────────────────┘    └─────────────────┘
```

## Prerequisites

### Cluster Requirements
- **Kubernetes Version**: 1.25+
- **Node Resources**: 4 CPU, 8GB RAM minimum per node
- **Storage**: Dynamic provisioning (AWS EBS, GCE PD)
- **Networking**: CNI plugin with Network Policies support

### Required Add-ons
```bash
# Install Istio service mesh
curl -L https://istio.io/downloadIstio | sh -
istioctl install --set values.defaultRevision=default

# Install Prometheus Operator
kubectl apply -f https://raw.githubusercontent.com/prometheus-operator/prometheus-operator/main/bundle.yaml

# Install cert-manager for TLS
kubectl apply -f https://github.com/cert-manager/cert-manager/releases/download/v1.13.0/cert-manager.yaml
```

## Deployment Strategy

### Phase 1: Foundation (Database + Infrastructure)
```bash
# Create namespace and RBAC
kubectl apply -f namespace.yaml
kubectl apply -f rbac/service-accounts.yaml

# Deploy PostgreSQL databases
kubectl apply -f databases/postgres-payment.yaml

# Deploy Redis cluster for event streaming
kubectl apply -f redis/redis-cluster.yaml

# Verify infrastructure
kubectl get pods -n mini-visa -l tier=database
kubectl get pods -n mini-visa -l tier=cache
```

### Phase 2: Core Services
```bash
# Apply secrets and configs
kubectl apply -f secrets/database-secrets.yaml
kubectl apply -f configmaps/payment-service-config.yaml

# Deploy payment service (core)
kubectl apply -f deployments/payment-service.yaml
kubectl apply -f services/payment-service-svc.yaml

# Apply HPA for auto-scaling
kubectl apply -f hpa/payment-service-hpa.yaml

# Verify core deployment
kubectl get pods -n mini-visa -l app=payment-service
kubectl get svc -n mini-visa -l app=payment-service
```

### Phase 3: Traffic Management
```bash
# Deploy Istio gateway and virtual services
kubectl apply -f istio/gateway.yaml

# Configure monitoring
kubectl apply -f monitoring/prometheus-config.yaml

# Verify traffic routing
kubectl get gateway -n mini-visa
kubectl get virtualservice -n mini-visa
```

## Advanced Deployment Patterns

### Blue-Green Deployment
```bash
# Deploy green version
kubectl patch deployment payment-service -n mini-visa -p '{"spec":{"template":{"metadata":{"labels":{"version":"green"}}}}}'

# Update service selector gradually
kubectl patch service payment-service -n mini-visa -p '{"spec":{"selector":{"version":"green"}}}'

# Rollback if needed
kubectl rollout undo deployment/payment-service -n mini-visa
```

### Canary Deployment with Istio
```yaml
# 90/10 traffic split
apiVersion: networking.istio.io/v1beta1
kind: VirtualService
metadata:
  name: payment-canary
spec:
  http:
  - match:
    - headers:
        canary:
          exact: "true"
    route:
    - destination:
        host: payment-service
        subset: canary
  - route:
    - destination:
        host: payment-service
        subset: v1
      weight: 90
    - destination:
        host: payment-service
        subset: canary
      weight: 10
```

## Monitoring and Observability

### Metrics Collection
- **Application Metrics**: Prometheus scraping `/metrics` endpoint
- **Infrastructure Metrics**: Node Exporter, kube-state-metrics
- **Database Metrics**: PostgreSQL Exporter, Redis Exporter
- **Custom Metrics**: Payment throughput, error rates, latency percentiles

### Alerting Rules
Key alerts configured in `prometheus-config.yaml`:
- High error rate (>1%)
- High latency (P95 >1s)
- Service down
- Database connection issues
- Memory usage >80%

### Distributed Tracing
```yaml
# Enable Jaeger tracing
apiVersion: v1
kind: ConfigMap
metadata:
  name: jaeger-config
data:
  JAEGER_AGENT_HOST: jaeger-agent
  JAEGER_SAMPLER_RATE: "0.1"
```

## Security Configuration

### Network Policies
```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: payment-service-netpol
spec:
  podSelector:
    matchLabels:
      app: payment-service
  policyTypes:
  - Ingress
  - Egress
  ingress:
  - from:
    - namespaceSelector:
        matchLabels:
          name: istio-system
  egress:
  - to:
    - podSelector:
        matchLabels:
          app: postgres-payment
    ports:
    - protocol: TCP
      port: 5432
```

### Pod Security Standards
```yaml
apiVersion: v1
kind: Pod
spec:
  securityContext:
    runAsNonRoot: true
    runAsUser: 10001
    fsGroup: 10001
    seccompProfile:
      type: RuntimeDefault
  containers:
  - name: payment-service
    securityContext:
      allowPrivilegeEscalation: false
      readOnlyRootFilesystem: true
      capabilities:
        drop:
        - ALL
```

## Scaling Configuration

### Horizontal Pod Autoscaling
- **Min Replicas**: 3 (high availability)
- **Max Replicas**: 20 (cost control)
- **Target CPU**: 70%
- **Target Memory**: 80%
- **Custom Metrics**: Requests per second

### Vertical Pod Autoscaling
```yaml
apiVersion: autoscaling.k8s.io/v1
kind: VerticalPodAutoscaler
metadata:
  name: payment-service-vpa
spec:
  targetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: payment-service
  updatePolicy:
    updateMode: "Auto"
  resourcePolicy:
    containerPolicies:
    - containerName: payment-service
      minAllowed:
        cpu: 100m
        memory: 128Mi
      maxAllowed:
        cpu: 1000m
        memory: 1Gi
```

## Disaster Recovery

### Backup Strategy
```bash
# Database backups
kubectl create cronjob postgres-backup --image=postgres:15-alpine --schedule="0 2 * * *" -- pg_dump -h postgres-payment -U mini_payment payments_db

# Configuration backups
kubectl get all -n mini-visa -o yaml > mini-visa-backup.yaml
```

### Recovery Procedures
1. **Service Recovery**: Rolling restart with health checks
2. **Database Recovery**: Point-in-time recovery from WAL
3. **Complete Cluster Recovery**: Infrastructure as Code rebuilding

## Performance Tuning

### Database Optimization
- Connection pooling: PgBouncer sidecar
- Read replicas for query service
- Partitioning for transaction tables
- Index optimization for payment lookups

### Application Optimization
- Memory management: jemalloc allocator
- CPU affinity: Kubernetes CPU management
- Network optimization: TCP keepalive tuning
- JVM tuning: GC optimization (if porting to Java)

## Troubleshooting

### Common Issues
1. **Pod OOMKilled**: Increase memory limits
2. **Service Unavailable**: Check readiness probes
3. **High Latency**: Analyze database queries
4. **TLS Certificate Issues**: Verify cert-manager

### Debug Commands
```bash
# Check pod status
kubectl get pods -n mini-visa -o wide

# View logs
kubectl logs -n mini-visa deployment/payment-service --tail=100

# Debug networking
kubectl exec -n mini-visa deployment/payment-service -- netstat -tulpn

# Check resource usage
kubectl top pods -n mini-visa
```

## Cost Optimization

### Resource Right-sizing
- Use VPA recommendations
- Monitor actual vs. requested resources
- Implement cluster autoscaling
- Use spot instances for non-critical workloads

### Multi-tenancy
- Namespace isolation
- Resource quotas
- Shared infrastructure components
- Development/staging environment consolidation

## Compliance and Governance

### PCI DSS Considerations
- Network segmentation
- Encrypted communication (TLS)
- Access logging and monitoring
- Regular security scanning

### Audit Trail
- All API calls logged
- Database transaction logging
- Kubernetes API server auditing
- Prometheus metrics retention

## Migration Strategy

### From Monolith to Microservices
1. **Strangler Fig Pattern**: Gradually replace functionality
2. **Database Decomposition**: Extract bounded contexts
3. **Event Sourcing**: Implement eventual consistency
4. **API Gateway**: Centralize routing and security

### Zero-Downtime Migration
1. Deploy microservices alongside monolith
2. Route traffic incrementally
3. Validate data consistency
4. Decommission monolith components

---

## Quick Start Commands

```bash
# Complete deployment
kubectl apply -k k8s/

# Verify deployment
kubectl get pods -n mini-visa
kubectl get svc -n mini-visa
kubectl get ingress -n mini-visa

# Test payment endpoint
curl -X POST https://payments.mini-visa.local/payments/authorize \
  -H "Content-Type: application/json" \
  -d '{"request_id":"test123","pan":"4111111111111111","amount":"10.00"}'

# Monitor metrics
kubectl port-forward -n mini-visa svc/prometheus 9090:9090
```

This deployment guide provides a production-ready Kubernetes configuration for the mini-visa payment system with comprehensive monitoring, security, and scaling capabilities.