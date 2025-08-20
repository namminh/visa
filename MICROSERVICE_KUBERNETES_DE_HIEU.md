# HƯỚNG DẪN MICROSERVICE & KUBERNETES CHO MINI-VISA
## (Dễ Hiểu - Từ Cơ Bản Đến Nâng Cao)

---

## 📚 **MỤC LỤC**
1. [Tại Sao Cần Microservice?](#1-tại-sao-cần-microservice)
2. [Từ Monolith Đến Microservice](#2-từ-monolith-đến-microservice)
3. [Kubernetes Là Gì?](#3-kubernetes-là-gì)
4. [Thiết Kế Hệ Thống Mới](#4-thiết-kế-hệ-thống-mới)
5. [Cách Deploy Trên Kubernetes](#5-cách-deploy-trên-kubernetes)
6. [Monitoring & Bảo Mật](#6-monitoring--bảo-mật)
7. [Thực Hành Từng Bước](#7-thực-hành-từng-bước)

---

## 1. **TẠI SAO CẦN MICROSERVICE?**

### 🏠 **Ví Dụ Đời Thường**
Tưởng tượng hệ thống thanh toán như một **ngôi nhà lớn**:

**Monolith (Nhà Cũ)**:
```
┌─────────────────────────────────┐
│  🏠 NGÔI NHÀ LỚN (1 tòa nhà)   │
│                                 │
│  🛏️ Phòng ngủ (Payment)        │
│  🍳 Bếp (Risk Check)           │
│  🚿 Phòng tắm (Clearing)       │
│  📚 Thư viện (Ledger)          │
│                                 │
│  ❌ Sửa bếp → toàn nhà ngưng    │
│  ❌ Mở rộng khó khăn            │
└─────────────────────────────────┘
```

**Microservice (Khu Phố Mới)**:
```
🏘️ KHU PHỐ (Nhiều ngôi nhà nhỏ)

🏠 Nhà Payment    🏠 Nhà Risk      🏠 Nhà Clearing
   (Xử lý giao dịch)  (Kiểm tra rủi ro)  (Thanh toán thẻ)

🏠 Nhà Ledger     🏠 Nhà Reversal  🏠 Nhà Query
   (Ghi sổ sách)      (Hoàn tiền)       (Tra cứu)

✅ Sửa 1 nhà → nhà khác vẫn hoạt động
✅ Mở rộng dễ dàng (thêm nhà mới)
✅ Mỗi nhà có chuyên môn riêng
```

### 💰 **Lợi Ích Thực Tế**

| **Vấn Đề Monolith** | **Giải Pháp Microservice** |
|---------------------|---------------------------|
| Deploy 1 lỗi → toàn hệ thống down | Deploy từng service riêng lẻ |
| Code lớn → khó maintain | Code nhỏ, tập trung 1 chức năng |
| Scale toàn bộ (tốn kém) | Scale từng service theo nhu cầu |
| 1 team làm tất cả | Mỗi team chuyên về 1 service |
| Công nghệ cố định | Mỗi service chọn công nghệ phù hợp |

---

## 2. **TỪ MONOLITH ĐẾN MICROSERVICE**

### 🔄 **Quá Trình Chuyển Đổi**

#### **Bước 1: Phân Tích Monolith Hiện Tại**
```c
// main.c - Điểm vào chính
int main() {
    config_init();     // Đọc cấu hình
    db_connect();      // Kết nối database
    threadpool_create(); // Tạo thread pool
    net_server_run();  // Chạy TCP server
    return 0;
}
```

#### **Bước 2: Xác Định Ranh Giới Service**
```
📦 MONOLITH CŨ:
├── 💳 Payment Logic (authorize, capture)
├── 🛡️ Risk Management (fraud check, rules)
├── 🏦 Clearing (network simulation)
├── 📊 Ledger (accounting, journal)
├── 🔄 Reversal (compensation)
└── 🔍 Query (read-only operations)

⬇️ TÁCH THÀNH ⬇️

🏠 Payment Service    🏠 Risk Service      🏠 Clearing Service
🏠 Ledger Service     🏠 Reversal Service  🏠 Query Service
```

#### **Bước 3: Thiết Kế API Communication**

**Trước (In-Process Calls)**:
```c
// Gọi trực tiếp trong cùng 1 process
int risk_result = risk_evaluate(transaction);
int clearing_result = clearing_prepare(txn_id);
```

**Sau (HTTP API Calls)**:
```c
// Gọi qua HTTP API
POST http://risk-service:8081/risk/evaluate
{
  "pan": "4111111111111111",
  "amount": "100.00",
  "merchant": "SHOP001"
}

POST http://clearing-service:8082/clearing/prepare
{
  "txn_id": "visa_123456",
  "amount": "100.00"
}
```

---

## 3. **KUBERNETES LÀ GÌ?**

### 🎭 **Ví Dụ Dễ Hiểu**

Kubernetes như **người quản lý khu phố thông minh**:

```
🎭 KUBERNETES = NGƯỜI QUẢN LÝ KHU PHỐ

👥 Pods = Ngôi nhà (chứa ứng dụng)
🏘️ Deployment = Bản thiết kế xây nhà
🚪 Service = Địa chỉ cố định cho nhà
📊 ConfigMap = Sổ tay cấu hình
🔐 Secret = Két sắt chứa mật khẩu
📈 HPA = Hệ thống tự động mở rộng
```

### 🏗️ **Kiến Trúc Kubernetes**

```
┌─────────────────────────────────────────┐
│           🎮 MASTER NODE                │
│  ┌─────────────────────────────────────┐│
│  │ API Server (Bộ não chính)          ││
│  │ Controller (Người giám sát)        ││
│  │ Scheduler (Người phân công việc)   ││
│  │ etcd (Kho lưu trữ thông tin)      ││
│  └─────────────────────────────────────┘│
└─────────────────────────────────────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
┌─────────────┐┌─────────────┐┌─────────────┐
│ WORKER 1    ││ WORKER 2    ││ WORKER 3    │
│             ││             ││             │
│ 🏠Pod1      ││ 🏠Pod3      ││ 🏠Pod5      │
│ 🏠Pod2      ││ 🏠Pod4      ││ 🏠Pod6      │
└─────────────┘└─────────────┘└─────────────┘
```

---

## 4. **THIẾT KẾ HỆ THỐNG MỚI**

### 🎯 **Kiến Trúc Tổng Thể**

```
                🌐 INTERNET
                     │
              ┌─────────────┐
              │ API Gateway │ ← Cổng vào chính
              │ (Istio)     │
              └─────────────┘
                     │
    ┌────────────────┼────────────────┐
    │                │                │
    ▼                ▼                ▼
┌─────────┐    ┌─────────┐    ┌─────────┐
│Payment  │────│  Risk   │    │Clearing │
│Service  │    │Service  │    │Service  │
│:8080    │    │:8081    │    │:8082    │
└─────────┘    └─────────┘    └─────────┘
    │                │                │
    └────────────┐   │   ┌────────────┘
                 │   │   │
                 ▼   ▼   ▼
            ┌─────────────────┐
            │  📨 Event Bus   │ ← Giao tiếp bất đồng bộ
            │   (Redis)       │
            └─────────────────┘
                     │
    ┌────────────────┼────────────────┐
    ▼                ▼                ▼
┌─────────┐    ┌─────────┐    ┌─────────┐
│Ledger   │    │Reversal │    │ Query   │
│Service  │    │Service  │    │Service  │
│:8083    │    │:8084    │    │:8085    │
└─────────┘    └─────────┘    └─────────┘
```

### 📊 **Chi Tiết Từng Service**

#### **1. Payment Service (Trái Tim Hệ Thống)**
```yaml
Chức năng:
  - Nhận yêu cầu thanh toán
  - Điều phối các service khác
  - Đảm bảo idempotency (không trùng lặp)

API:
  - POST /payments/authorize
  - POST /payments/capture
  - GET /payments/status/{id}

Database:
  - payments_db (PostgreSQL)
  - Bảng: transactions, outbox_events
```

#### **2. Risk Service (Bộ Phận Bảo Vệ)**
```yaml
Chức năng:
  - Kiểm tra gian lận
  - Áp dụng rules business
  - Blacklist/whitelist

API:
  - POST /risk/evaluate
  - PUT /risk/rules/{id}

Database:
  - risk_db (PostgreSQL)
  - Bảng: rules, blacklist, risk_scores
```

#### **3. Clearing Service (Kết Nối Ngân Hàng)**
```yaml
Chức năng:
  - Mô phỏng mạng lưới thẻ
  - Thực hiện 2PC (Two-Phase Commit)
  - Xử lý prepare/commit/abort

API:
  - POST /clearing/prepare
  - POST /clearing/commit
  - POST /clearing/abort

Database:
  - clearing_db (PostgreSQL)
  - Bảng: clearing_states
```

---

## 5. **CÁCH DEPLOY TRÊN KUBERNETES**

### 📦 **Cấu Trúc File Deployment**

```
k8s/
├── 🏷️ namespace.yaml              # Tạo không gian riêng
├── 🔐 secrets/                    # Mật khẩu database
├── ⚙️ configmaps/                 # Cấu hình ứng dụng
├── 🚀 deployments/                # Triển khai services
├── 🌐 services/                   # Địa chỉ network
├── 📊 hpa/                        # Auto scaling
├── 🗄️ databases/                  # PostgreSQL
├── 📨 redis/                      # Event streaming
├── 📈 monitoring/                 # Prometheus
├── 🌍 istio/                      # API Gateway
└── 📖 deployment-guide.md         # Hướng dẫn chi tiết
```

### 🛠️ **Deployment Strategies**

#### **1. Blue-Green Deployment (Không Downtime)**
```
🔵 Blue Environment (Phiên bản cũ)
🟢 Green Environment (Phiên bản mới)

Bước 1: Deploy Green song song với Blue
Bước 2: Test Green kỹ lưỡng
Bước 3: Chuyển traffic từ Blue → Green
Bước 4: Giữ Blue để rollback nếu cần

┌─────────┐    ┌─────────┐
│🔵 Blue  │    │🟢 Green │
│ v1.0    │    │ v1.1    │
│ 100%    │ ─→ │ 0%     │
└─────────┘    └─────────┘

┌─────────┐    ┌─────────┐
│🔵 Blue  │    │🟢 Green │
│ v1.0    │    │ v1.1    │
│ 0%      │ ←─ │ 100%   │
└─────────┘    └─────────┘
```

#### **2. Canary Deployment (Từ Từ)**
```
Triển khai từng bước với % traffic:

Step 1: 95% old + 5% new
Step 2: 90% old + 10% new  
Step 3: 80% old + 20% new
Step 4: 50% old + 50% new
Step 5: 0% old + 100% new

Nếu có lỗi → rollback ngay lập tức
```

### 🎯 **Auto Scaling (Tự Động Mở Rộng)**

```yaml
# HPA Configuration
Min Replicas: 3        # Tối thiểu 3 instance
Max Replicas: 20       # Tối đa 20 instance

Triggers:
  - CPU > 70%         → Tăng instance
  - Memory > 80%      → Tăng instance  
  - RPS > 100/s       → Tăng instance
  
Scale Up: +50% (tối đa 2 pods/60s)
Scale Down: -10% (tối đa 1 pod/5min)
```

---

## 6. **MONITORING & BẢO MẬT**

### 📊 **Hệ Thống Giám Sát**

```
📈 MONITORING STACK:

┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ Application │────│ Prometheus  │────│   Grafana   │
│  (Metrics)  │    │ (Collector) │    │ (Dashboard) │
└─────────────┘    └─────────────┘    └─────────────┘
       │                   │                   │
       ▼                   ▼                   ▼
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│    Logs     │    │   Alerts    │    │   Reports   │
│ (ELK Stack) │    │(AlertManager│    │  (Business) │
└─────────────┘    └─────────────┘    └─────────────┘
```

#### **Metrics Quan Trọng**
```yaml
Business Metrics:
  - payment_requests_total          # Tổng số giao dịch
  - payment_success_rate           # Tỷ lệ thành công
  - payment_declined_rate          # Tỷ lệ từ chối
  - average_transaction_value      # Giá trị TB giao dịch

Technical Metrics:
  - request_duration_seconds       # Thời gian xử lý
  - error_rate_percent            # Tỷ lệ lỗi
  - database_connections          # Kết nối DB
  - memory_usage_bytes           # Sử dụng RAM
```

### 🛡️ **Bảo Mật Multi-Layer**

```
🏰 SECURITY LAYERS:

Layer 1: Network Security
├── 🌐 Istio Service Mesh (mTLS)
├── 🚪 Network Policies 
└── 🔥 Firewall Rules

Layer 2: Identity & Access
├── 🎫 Service Accounts
├── 🔑 RBAC (Role-Based Access)  
└── 🎭 Pod Security Standards

Layer 3: Data Protection
├── 🔐 Encrypted Secrets
├── 🗄️ Database Encryption
└── 📝 Audit Logging

Layer 4: Runtime Security  
├── 👤 Non-root containers
├── 📂 Read-only filesystems
└── 🚫 Capability dropping
```

#### **Secret Management**
```yaml
# Database Password (Base64 encoded)
apiVersion: v1
kind: Secret
metadata:
  name: payment-db-secret
data:
  DB_PASSWORD: cGF5bWVudF9zZWNyZXQ=  # "payment_secret"

# Sử dụng trong Pod
env:
- name: DB_PASSWORD
  valueFrom:
    secretKeyRef:
      name: payment-db-secret
      key: DB_PASSWORD
```

---

## 7. **THỰC HÀNH TỪNG BƯỚC**

### 🚀 **Phase 1: Setup Cluster**

#### **Bước 1: Chuẩn Bị Môi Trường**
```bash
# Cài đặt tools cần thiết
curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl

# Verify kubectl
kubectl version --client

# Cài đặt Helm (Package Manager)
curl https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3 | bash
```

#### **Bước 2: Tạo Namespace**
```bash
# Tạo namespace cho môi trường
kubectl apply -f - <<EOF
apiVersion: v1
kind: Namespace
metadata:
  name: mini-visa
  labels:
    name: mini-visa
    environment: production
EOF

# Kiểm tra
kubectl get namespaces
```

#### **Bước 3: Setup Secrets**
```bash
# Tạo secret cho database
kubectl create secret generic payment-db-secret \
  --from-literal=DB_PASSWORD=payment_secret \
  --from-literal=DB_URI=postgresql://mini_payment:payment_secret@postgres-payment:5432/payments_db \
  -n mini-visa

# Kiểm tra
kubectl get secrets -n mini-visa
```

### 🗄️ **Phase 2: Deploy Databases**

#### **PostgreSQL for Payment Service**
```bash
# Deploy PostgreSQL StatefulSet
kubectl apply -f - <<EOF
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: postgres-payment
  namespace: mini-visa
spec:
  serviceName: postgres-payment-headless
  replicas: 1
  selector:
    matchLabels:
      app: postgres-payment
  template:
    metadata:
      labels:
        app: postgres-payment
    spec:
      containers:
      - name: postgres
        image: postgres:15-alpine
        env:
        - name: POSTGRES_DB
          value: "payments_db"
        - name: POSTGRES_USER
          value: "mini_payment"
        - name: POSTGRES_PASSWORD
          valueFrom:
            secretKeyRef:
              name: payment-db-secret
              key: DB_PASSWORD
        ports:
        - containerPort: 5432
          name: postgres
        volumeMounts:
        - name: postgres-storage
          mountPath: /var/lib/postgresql/data
  volumeClaimTemplates:
  - metadata:
      name: postgres-storage
    spec:
      accessModes: [ "ReadWriteOnce" ]
      resources:
        requests:
          storage: 10Gi
EOF
```

#### **Redis for Event Streaming**
```bash
# Deploy Redis Cluster
kubectl apply -f k8s/redis/redis-cluster.yaml

# Kiểm tra database pods
kubectl get pods -n mini-visa -l tier=database
kubectl get pvc -n mini-visa
```

### 🏗️ **Phase 3: Deploy Services**

#### **Payment Service (Core)**
```bash
# Deploy payment service
kubectl apply -f k8s/deployments/payment-service.yaml
kubectl apply -f k8s/services/payment-service-svc.yaml

# Kiểm tra deployment
kubectl get deployments -n mini-visa
kubectl get pods -n mini-visa -l app=payment-service

# Xem logs
kubectl logs -n mini-visa deployment/payment-service --tail=50
```

#### **Setup Auto-Scaling**
```bash
# Deploy HPA
kubectl apply -f k8s/hpa/payment-service-hpa.yaml

# Kiểm tra HPA status
kubectl get hpa -n mini-visa
kubectl describe hpa payment-service-hpa -n mini-visa
```

### 🌐 **Phase 4: Setup Networking**

#### **Istio Service Mesh**
```bash
# Cài đặt Istio
curl -L https://istio.io/downloadIstio | sh -
cd istio-*
sudo cp bin/istioctl /usr/local/bin/

# Install Istio
istioctl install --set values.defaultRevision=default

# Enable sidecar injection cho namespace
kubectl label namespace mini-visa istio-injection=enabled

# Deploy Gateway và VirtualService
kubectl apply -f k8s/istio/gateway.yaml
```

#### **Test Connectivity**
```bash
# Port-forward để test local
kubectl port-forward -n mini-visa svc/payment-service 8080:8080

# Test payment API
curl -X POST http://localhost:8080/payments/authorize \
  -H "Content-Type: application/json" \
  -d '{
    "request_id": "test_001",
    "pan": "4111111111111111", 
    "amount": "100.00",
    "currency": "USD"
  }'

# Test health endpoints
curl http://localhost:8080/healthz
curl http://localhost:8080/metrics
```

### 📊 **Phase 5: Setup Monitoring**

#### **Prometheus & Grafana**
```bash
# Install Prometheus Operator
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
helm repo update

helm install prometheus prometheus-community/kube-prometheus-stack \
  --namespace mini-visa \
  --create-namespace \
  --set grafana.adminPassword=admin123

# Access Grafana Dashboard
kubectl port-forward -n mini-visa svc/prometheus-grafana 3000:80

# Login: admin/admin123
# URL: http://localhost:3000
```

#### **Custom Dashboards**
```bash
# Import payment system dashboard
curl -X POST http://admin:admin123@localhost:3000/api/dashboards/db \
  -H "Content-Type: application/json" \
  -d '{
    "dashboard": {
      "title": "Mini-Visa Payment System",
      "panels": [
        {
          "title": "Payment Success Rate",
          "type": "stat",
          "targets": [
            {
              "expr": "rate(payment_requests_total{status=\"success\"}[5m]) / rate(payment_requests_total[5m]) * 100"
            }
          ]
        }
      ]
    }
  }'
```

### 🔧 **Phase 6: Load Testing & Tuning**

#### **Load Testing với K6**
```bash
# Install K6
sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys C5AD17C747E3415A3642D57D77C6C491D6AC1D69
echo "deb https://dl.k6.io/deb stable main" | sudo tee /etc/apt/sources.list.d/k6.list
sudo apt-get update
sudo apt-get install k6

# Create load test script
cat > payment-load-test.js << 'EOF'
import http from 'k6/http';
import { check } from 'k6';

export let options = {
  stages: [
    { duration: '2m', target: 10 },   // Ramp up
    { duration: '5m', target: 50 },   // Stay at 50 users  
    { duration: '2m', target: 100 },  // Ramp up to 100
    { duration: '5m', target: 100 },  // Stay at 100
    { duration: '2m', target: 0 },    // Ramp down
  ],
};

export default function() {
  const payload = JSON.stringify({
    request_id: `test_${__VU}_${__ITER}`,
    pan: '4111111111111111',
    amount: '50.00',
    currency: 'USD'
  });

  const params = {
    headers: { 'Content-Type': 'application/json' },
  };

  let response = http.post('http://localhost:8080/payments/authorize', payload, params);
  
  check(response, {
    'status is 200': (r) => r.status === 200,
    'response time < 500ms': (r) => r.timings.duration < 500,
  });
}
EOF

# Run load test
k6 run payment-load-test.js
```

#### **Performance Tuning**
```bash
# Monitor HPA during load test
watch kubectl get hpa -n mini-visa

# Check pod scaling
watch kubectl get pods -n mini-visa -l app=payment-service

# View resource usage
kubectl top pods -n mini-visa
kubectl top nodes

# Adjust HPA thresholds if needed
kubectl patch hpa payment-service-hpa -n mini-visa --type='merge' -p='{
  "spec": {
    "metrics": [
      {
        "type": "Resource",
        "resource": {
          "name": "cpu",
          "target": {
            "type": "Utilization", 
            "averageUtilization": 60
          }
        }
      }
    ]
  }
}'
```

---

## 🎓 **TÓM TẮT & BƯỚC TIẾP THEO**

### ✅ **Những Gì Đã Làm**

1. **Phân tích kiến trúc**: Từ monolith → microservice
2. **Thiết kế system**: 6 services độc lập  
3. **Setup Kubernetes**: Complete deployment manifests
4. **Security & Monitoring**: Production-ready configuration
5. **Testing & Scaling**: Auto-scaling với HPA
6. **Documentation**: Hướng dẫn từng bước chi tiết

### 🚀 **Roadmap Tiếp Theo**

#### **Phase 7: Advanced Features**
```bash
# 1. Service Mesh Security (mTLS)
istioctl proxy-config cluster payment-service-xxx.mini-visa

# 2. Distributed Tracing (Jaeger)
kubectl apply -f https://raw.githubusercontent.com/jaegertracing/jaeger-operator/main/deploy/crds/jaegertracing.io_jaegers_crd.yaml

# 3. Chaos Engineering (Chaos Monkey)
helm install chaos-monkey chaos-monkey/chaos-monkey --namespace mini-visa

# 4. GitOps với ArgoCD
helm install argocd argo/argo-cd --namespace argocd --create-namespace
```

#### **Phase 8: Multi-Environment**
```bash
# Development Environment
kubectl create namespace mini-visa-dev
helm install mini-visa-dev ./helm-chart --namespace mini-visa-dev \
  --set environment=development \
  --set replicas=1 \
  --set resources.limits.memory=256Mi

# Staging Environment  
kubectl create namespace mini-visa-staging
helm install mini-visa-staging ./helm-chart --namespace mini-visa-staging \
  --set environment=staging \
  --set replicas=2
```

### 🎯 **Key Takeaways**

```
🏆 THÀNH CÔNG KHI:

✅ Hiểu rõ sự khác biệt monolith vs microservice
✅ Biết cách thiết kế API communication
✅ Nắm vững Kubernetes concepts (Pod, Service, Deployment)
✅ Setup được monitoring & alerting
✅ Implement security best practices
✅ Có khả năng troubleshoot và scale system

🎓 KỸ NĂNG ĐẠT ĐƯỢC:
- Container orchestration với Kubernetes
- Service mesh với Istio  
- CI/CD pipeline setup
- Infrastructure as Code
- Microservice design patterns
- Production operations
```

---

## 📞 **Hỗ Trợ & Resources**

### 📚 **Tài Liệu Tham Khảo**
- [Kubernetes Official Docs](https://kubernetes.io/docs/)
- [Istio Service Mesh](https://istio.io/latest/docs/)
- [Prometheus Monitoring](https://prometheus.io/docs/)
- [Microservice Patterns](https://microservices.io/)

### 🛠️ **Tools Hữu Ích**
```bash
# Kubernetes Dashboard
kubectl apply -f https://raw.githubusercontent.com/kubernetes/dashboard/v2.7.0/aio/deploy/recommended.yaml

# Lens Desktop (K8s IDE)  
# Download: https://k8slens.dev/

# K9s (Terminal Dashboard)
snap install k9s

# Kubectx/Kubens (Context switching)
sudo git clone https://github.com/ahmetb/kubectx /opt/kubectx
sudo ln -s /opt/kubectx/kubectx /usr/local/bin/kubectx
sudo ln -s /opt/kubectx/kubens /usr/local/bin/kubens
```

### 🆘 **Troubleshooting Common Issues**

```bash
# Pod không start được
kubectl describe pod <pod-name> -n mini-visa
kubectl logs <pod-name> -n mini-visa --previous

# Service không accessible
kubectl get endpoints -n mini-visa
kubectl port-forward svc/<service-name> 8080:8080 -n mini-visa

# Database connection issues
kubectl exec -it postgres-payment-0 -n mini-visa -- psql -U mini_payment -d payments_db

# Resource issues
kubectl top nodes
kubectl top pods -n mini-visa
kubectl describe node <node-name>
```

Chúc bạn thành công với việc triển khai hệ thống microservice trên Kubernetes! 🎉