# CI/CD GitHub — Bản Dễ Hiểu (Mini‑Visa)

Mục tiêu: Mỗi lần push/PR, GitHub tự build + test (CI). Khi merge vào `main`, có thể tự triển khai lên staging (CD).

Tóm tắt 5 bước CI nhanh:
1) Tạo file `.github/workflows/ci.yml` (mẫu bên dưới).
2) CI bật Postgres service, cài deps, chạy `db/schema.sql`.
3) Build với `make`, chạy `./tests/run_all.sh` (cần `DB_URI`, `PORT`).
4) Xem tab Actions để thấy kết quả.
5) Lỗi? Mở artifact log để điều tra.

CD (tuỳ chọn):
- Cách A (khuyên dùng): Build Docker, push GHCR, SSH kéo image chạy container.
- Cách B: Build binary, copy lên server, restart service bằng systemd.

## 1) Điều kiện & chuẩn bị (ngắn gọn)
- Repo có `Makefile`, `db/schema.sql`, `tests/run_all.sh`, `scripts/run.sh` (đã có).
- CI dùng Postgres service trong workflow, không cần secret DB cho bước CI.
- Yêu cầu cài đặt trong runner: `build-essential`, `libpq-dev`, `postgresql-client`.

Mẹo: Thử build/test local trước khi push: `make clean && make && PORT=9090 ./tests/run_all.sh`.

## 2) CI: Build + Test (mẫu sẵn dùng)
- Tạo file `.github/workflows/ci.yml` với nội dung sau:

```yaml
name: CI
on: [push, pull_request]

jobs:
  build-test:
    runs-on: ubuntu-latest
    services:
      postgres:
        image: postgres:15
        env:
          POSTGRES_PASSWORD: mini
          POSTGRES_USER: mini
          POSTGRES_DB: mini_visa
        ports: ["5432:5432"]
        options: >-
          --health-cmd "pg_isready -U mini"
          --health-interval 5s --health-timeout 5s --health-retries 20
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential libpq-dev postgresql-client

      - name: Wait for Postgres and init schema
        env:
          DB_URI: postgresql://mini:mini@127.0.0.1:5432/mini_visa
        run: |
          for i in {1..30}; do pg_isready -h 127.0.0.1 -p 5432 -U mini && break || sleep 1; done
          psql "$DB_URI" -f db/schema.sql
          if [ -f db/seed.sql ]; then psql "$DB_URI" -f db/seed.sql; fi

      - name: Build
        run: make clean && make

      - name: Run tests
        env:
          DB_URI: postgresql://mini:mini@127.0.0.1:5432/mini_visa
          PORT: 9090
        run: ./tests/run_all.sh

      - name: Upload logs (optional)
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: logs-and-binaries
          path: |
            build/**
            logs/**
```

- Tuỳ chọn ma trận compiler (gcc/clang):
  - Thêm dưới `jobs.build-test.strategy.matrix.compiler: [gcc, clang]`, và cài `clang` khi cần.

## 3) CD A (Docker + GHCR + SSH)
- Dùng GitHub Container Registry (GHCR) để lưu image.
- Tạo secrets trong repo (Settings → Secrets and variables → Actions → New repository secret):
  - `STAGING_HOST`, `STAGING_USER`, `STAGING_SSH_KEY` (private key), `STAGING_DB_URI`.
- Tạo file `.github/workflows/cd.yml`:

```yaml
name: CD Staging
on:
  push:
    branches: [ main ]

jobs:
  build-push-deploy:
    runs-on: ubuntu-latest
    permissions:
      contents: read
      packages: write   # để push GHCR
    steps:
      - uses: actions/checkout@v4

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Log in to GHCR
        uses: docker/login-action@v3
        with:
          registry: ghcr.io
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Build and push image
        uses: docker/build-push-action@v5
        with:
          context: .
          push: true
          tags: |
            ghcr.io/${{ github.repository }}/mini-visa:${{ github.sha }}
            ghcr.io/${{ github.repository }}/mini-visa:latest

      - name: Deploy to staging via SSH
        uses: appleboy/ssh-action@v1.0.3
        with:
          host: ${{ secrets.STAGING_HOST }}
          username: ${{ secrets.STAGING_USER }}
          key: ${{ secrets.STAGING_SSH_KEY }}
          script: |
            set -euo pipefail
            docker pull ghcr.io/${{ github.repository }}/mini-visa:${{ github.sha }}
            docker rm -f mini-visa || true
            docker run -d --name mini-visa --restart=always \
              -e DB_URI='${{ secrets.STAGING_DB_URI }}' -e PORT=9090 \
              -p 9090:9090 ghcr.io/${{ github.repository }}/mini-visa:${{ github.sha }}
```

- Thêm `Dockerfile` ở root (nếu chưa có):

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y build-essential libpq-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN make clean && make
EXPOSE 9090
ENV PORT=9090
CMD ["./build/server"]
```

## 4) CD B (Binary + systemd, không Docker)
- Secrets cần: `STAGING_HOST`, `STAGING_USER`, `STAGING_SSH_KEY`, `STAGING_DB_URI`.
- `.github/workflows/cd.yml` (biến thể không Docker):

```yaml
name: CD Staging (binary)
on:
  push:
    branches: [ main ]

jobs:
  build-deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get update && sudo apt-get install -y build-essential libpq-dev
      - name: Build
        run: make clean && make
      - name: Package
        run: tar -czf mini-visa-${{ github.sha }}.tar.gz build/server scripts db/schema.sql
      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: mini-visa-${{ github.sha }}
          path: mini-visa-${{ github.sha }}.tar.gz
      # Bước copy + restart systemd phụ thuộc hạ tầng; có thể dùng scp-action hoặc rsync trong SSH
      - name: Deploy via SSH
        uses: appleboy/ssh-action@v1.0.3
        with:
          host: ${{ secrets.STAGING_HOST }}
          username: ${{ secrets.STAGING_USER }}
          key: ${{ secrets.STAGING_SSH_KEY }}
          script: |
            set -euo pipefail
            mkdir -p /opt/mini-visa/releases/${GITHUB_SHA}
            # Giả sử tar.gz đã được copy sẵn hoặc dùng một bước scp khác
            tar -xzf /opt/mini-visa/mini-visa-${GITHUB_SHA}.tar.gz -C /opt/mini-visa/releases/${GITHUB_SHA}
            ln -sfn /opt/mini-visa/releases/${GITHUB_SHA} /opt/mini-visa/current
            sudo systemctl restart mini-visa
```

- Trên máy đích tạo unit systemd `/etc/systemd/system/mini-visa.service`:

```ini
[Unit]
Description=Mini-Visa server
After=network-online.target

[Service]
Environment=DB_URI=
Environment=PORT=9090
WorkingDirectory=/opt/mini-visa/current
ExecStart=/opt/mini-visa/current/build/server
Restart=on-failure
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

## 5) Secrets, Environments, Branch protection (ngắn)
- Lưu secrets tại: Settings → Secrets and variables → Actions.
- Dùng GitHub Environments (Staging/Production) để yêu cầu approve trước khi deploy.
- Bật Branch protection cho `main` (PR required, status checks).

## 6) Mẹo tối ưu
- Cache apt qua `actions/cache` (tuỳ chọn). Build C nhỏ nên có thể bỏ.
- Tách CI (pull_request) và CD (push → main) để tránh deploy từ PR.
- Thu log/binary làm artifact để dễ điều tra lỗi CI.

## 7) Kiểm tra cục bộ trước khi push
- `make clean && make`
- Khởi chạy DB Postgres cục bộ, chạy `psql "$DB_URI" -f db/schema.sql`.
- Chạy test: `PORT=9090 ./tests/run_all.sh`.

---

Muốn mình thêm sẵn `.github/workflows/ci.yml` và `Dockerfile` vào repo theo mẫu trên không?
