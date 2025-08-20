Hướng dẫn Terraform cho Mini‑Visa (Môi trường dev)

Mục tiêu
- Tạo “hạ tầng dev” nhanh: VPC + EKS + node group.
- Kết nối Terraform → Kubernetes/Helm providers để bootstrap tiện ích cơ bản.
- Hỗ trợ GitOps (ArgoCD) đồng bộ manifests trong `k8s/` của repo.

Thành phần được tạo
- VPC riêng, 2 AZ, public/private subnets, NAT Gateway (đơn giản cho dev).
- EKS cluster + managed node group (instance mặc định `t3.medium`).
- Cài qua Helm:
  - `metrics-server` (HPA hoạt động).
  - `ingress-nginx` (Ingress Controller, type=LoadBalancer).
  - `argo-cd` (GitOps, service type=LoadBalancer).
- Namespace ứng dụng: `mini-visa`.

Cấu trúc thư mục
- `terraform/`
  - `envs/dev/` — cấu hình môi trường dev
    - `backend.tf` — backend Terraform (mặc định local, dev)
    - `providers.tf` — khai báo aws/kubernetes/helm/random
    - `variables.tf` — biến cấu hình (region, cluster, node…)
    - `main.tf` — VPC + EKS + Helm bootstrap
    - `outputs.tf` — thông tin xuất (endpoint, region, OIDC…)
    - `argocd-app.sample.yaml` — mẫu ArgoCD Application trỏ `k8s/`
  - `.gitignore`

Điều kiện tiên quyết
- Terraform >= 1.5
- Tài khoản AWS và thông tin xác thực hoạt động (`AWS_PROFILE`, hoặc `AWS_ACCESS_KEY_ID`/`AWS_SECRET_ACCESS_KEY`).
- `kubectl` và `awscli` để truy cập cluster sau khi apply.

Biến cấu hình chính (envs/dev/variables.tf)
- `aws_region`: vùng AWS (mặc định `ap-southeast-1`).
- `cluster_name`: tên cụm EKS (mặc định `mini-visa-dev`).
- `vpc_cidr`: CIDR VPC (mặc định `10.10.0.0/16`).
- `kubernetes_version`: phiên bản EKS (mặc định `1.29`).
- `node_instance_types`: loại node (mặc định `["t3.medium"]`).
- `node_desired_size`/`node_min_size`/`node_max_size`: số lượng node.

Khởi chạy nhanh (dev)
1) Init và apply
   - `cd terraform/envs/dev`
   - `terraform init`
   - `terraform apply`

2) Kết nối kubectl vào EKS
   - `aws eks update-kubeconfig --name mini-visa-dev --region ap-southeast-1`
   - Kiểm tra: `kubectl get nodes`

3) Truy cập ArgoCD (tùy chọn)
   - Xem LB: `kubectl get svc -n argocd argocd-server`
   - Lấy IP/hostname rồi mở trình duyệt (mặc định chart bật `server.insecure: true` cho dev).

4) Đồng bộ ứng dụng `k8s/` bằng ArgoCD
   - Sửa `terraform/envs/dev/argocd-app.sample.yaml`, thay `<YOUR_GIT_REPO_URL>` bằng URL repo của bạn.
   - Apply: `kubectl apply -f terraform/envs/dev/argocd-app.sample.yaml`
   - ArgoCD sẽ sync toàn bộ manifests trong `k8s/` vào namespace `mini-visa`.

Cách hoạt động (tóm tắt)
- `main.tf` dùng modules:
  - `terraform-aws-modules/vpc/aws` tạo VPC/subnets.
  - `terraform-aws-modules/eks/aws` tạo EKS + node groups, addons.
- Sau khi EKS tạo xong, provider `kubernetes` và `helm` kết nối vào API server.
- Helm cài `metrics-server`, `ingress-nginx`, `argo-cd`.
- Tạo sẵn namespace `mini-visa` cho app.

Kết nối với thư mục k8s/
- Repo đã có `k8s/` (namespace, services, configmaps, secrets, databases, redis, monitoring, kustomization.yaml).
- Bạn có thể:
  - Dùng ArgoCD (khuyến nghị) để tự sync `k8s/` theo GitOps.
  - Hoặc `kubectl apply -k k8s/` để áp thủ công.

Secrets và Database (lựa chọn)
- Dev nhanh: dùng Postgres in‑cluster trong `k8s/databases/`. Biến `DB_URI` có thể trỏ đến service cluster (ví dụ `postgres-payment`).
- Prod/staging: khuyến nghị dùng RDS/CloudSQL ngoài cluster.
  - Xóa/không áp manifests DB in‑cluster.
  - Thêm module Terraform RDS/CloudSQL (chưa bao gồm trong scaffold dev này).
  - Trỏ `DB_URI` đến endpoint RDS/CloudSQL.
- Quản lý secrets:
  - Tối thiểu: `kubernetes_secret`/`kubectl create secret`.
  - Tốt hơn: cài External Secrets Operator (bằng Helm) và lưu secrets ở AWS Secrets Manager; K8s tự đồng bộ.

Nhiều môi trường (gợi ý)
- Tạo thêm `terraform/envs/staging` và `terraform/envs/prod` (sao chép từ dev) rồi điều chỉnh:
  - Backend remote S3 + DynamoDB lock (tránh xung đột state):
    ```hcl
    terraform {
      backend "s3" {
        bucket         = "your-tfstate-bucket"
        key            = "mini-visa/staging/terraform.tfstate"
        region         = "ap-southeast-1"
        dynamodb_table = "your-tflock-table"
        encrypt        = true
      }
    }
    ```
  - Tăng kích thước node/replica, gắn domain/ACM cho ingress, harden cấu hình.
- Kết hợp GitOps: mỗi môi trường 1 ArgoCD Application trỏ đến `k8s/` với overlay khác nhau (hoặc nhánh Git khác).

Dọn dẹp (destroy)
- Cảnh báo: sẽ xóa cả LoadBalancers, EKS, VPC (có thể tốn thời gian và bị charge tới khi xóa xong).
- Thực hiện: `terraform destroy` trong `terraform/envs/dev`.

Chi phí (ước lượng dev)
- EKS control plane, NAT Gateway, NLB/ALB cho ingress/argocd, EC2 nodes. Hãy dọn dẹp khi không dùng để tránh phí phát sinh.

Bảo mật & Quyền (tóm lược)
- Sử dụng IAM qua AWS profile/role khi chạy Terraform.
- Có thể bật IRSA để pods truy cập dịch vụ AWS an toàn (module EKS đã sẵn OIDC provider output).
- Hạn chế public LB ở prod; thay bằng Ingress + private/internal, WAF, mTLS (Istio), NetworkPolicy, RBAC.

Troubleshooting nhanh
- Kiểm tra providers kết nối cluster: `kubectl get nodes`
- Xem helm releases: `helm list -A`
- Xem LB ingress-nginx: `kubectl get svc -n ingress-nginx`
- Xem LB ArgoCD: `kubectl get svc -n argocd argocd-server`
- Addons EKS: kiểm tra trong console hoặc output module.
- Lỗi xóa VPC: đảm bảo đã xóa tất cả LB/ENI/SecurityGroup liên quan trước khi `destroy`.

FAQ ngắn
- Terraform có apply `k8s/` trực tiếp không? — Không, scaffold khuyến nghị GitOps (ArgoCD). Bạn vẫn có thể `kubectl apply -k k8s/` thủ công.
- Có dùng được GKE/AKS? — Có, thay modules/providern tương ứng; cấu trúc còn lại tương tự (Helm, ArgoCD, GitOps).
- Muốn canary/blue‑green? — Thêm Argo Rollouts hoặc Istio + VirtualService; triển khai qua Helm/GitOps.

