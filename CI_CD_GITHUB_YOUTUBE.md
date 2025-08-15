Dưới đây là phần giải thích đơn giản, dễ hiểu về nội dung video “CI/CD Tutorial using GitHub Actions – Automated Testing & Automated Deployments” bạn đang mở.[1]

- CI/CD là gì?
  - CI (Continuous Integration): Mỗi khi bạn mở Pull Request (PR), hệ thống tự động chạy test/build để đảm bảo code mới không làm hỏng dự án.
  - CD (Continuous Delivery/Deployment): Khi code được merge vào nhánh chính (main), hệ thống tự động build và triển khai (deploy) lên server, giảm thao tác thủ công và lỗi con người.[1]

- Tại sao dùng GitHub Actions?
  - Tích hợp sẵn trong GitHub, dễ cấu hình bằng file YAML.
  - Có log chi tiết cho từng bước, dễ biết lỗi nằm ở đâu.
  - Có nhiều mẫu (template), nhưng trong video tác giả hướng dẫn viết từ đầu để hiểu rõ.[1]

- Quy trình CI (khi tạo Pull Request):
  1) Tạo file YAML trong thư mục .github/workflows, đặt tên workflow và cấu hình trigger on: pull_request: branches: [main].
  2) Khai báo jobs, thường chạy trên ubuntu-latest, dùng container Node.js cho dự án Node.
  3) Các bước (steps) điển hình:
     - actions/checkout: lấy mã nguồn.
     - npm ci: cài dependencies sạch.
     - npm test (ví dụ dùng Jest): chạy test.
     - npm run build: build/compile dự án.
  4) Nếu test/build fail → không nên merge/deploy; nếu pass sẽ có dấu xanh xác nhận.[1]

- Quy trình CD (khi push/merge vào main):
  1) Tạo file YAML khác, trigger on: push: branches: [main].
  2) Lặp lại bước chuẩn bị: checkout, thiết lập Node, npm ci, npm run build.
  3) Triển khai lên server:
     - Dùng SCP để upload file đã build.
     - Cần SSH key có quyền vào server. Key này KHÔNG đưa vào repo, mà lưu ở GitHub Actions Secrets.
     - Trong workflow, lấy secrets để cấu hình SSH agent, rồi chạy lệnh SCP/SSH như khi làm bằng tay.
     - Sau khi upload, SSH vào server, cài deps nếu cần và restart process (ví dụ quản lý bằng PM2).[1]

- Secrets là gì và dùng thế nào?
  - Là nơi lưu thông tin nhạy cảm (SSH key, API key như Stripe/OpenAI) trong GitHub, không commit vào repo.
  - Workflow truy cập secrets qua cú pháp như secrets.MY_SSH_KEY để thiết lập môi trường an toàn trước khi SCP/SSH.[1]

- Test workflow cục bộ bằng Act:
  - Cài Docker và dùng công cụ act để chạy GitHub Actions offline.
  - Tạo file .secrets (dùng secrets giả cho môi trường test, tránh dùng secrets production).
  - Chạy act để kiểm tra workflow hoạt động đúng trước khi đẩy lên GitHub.[1]

- Lợi ích chính:
  - Tiết kiệm thời gian, giảm lỗi thao tác thủ công.
  - Mọi PR đều được kiểm chứng tự động; chỉ code “xanh” mới lên production.
  - Dòng chảy chuẩn: PR → CI test/build → merge vào main → CD deploy tự động.[1]

- Mẫu “sườn” YAML tham khảo (minh họa ý tưởng):
  - CI (pull_request):
    - on: pull_request vào main
    - jobs:
      - runs-on: ubuntu-latest
      - steps: checkout → setup Node → npm ci → npm test → npm run build
  - CD (push):
    - on: push vào main
    - jobs:
      - runs-on: ubuntu-latest
      - steps: checkout → setup Node → npm ci → npm run build → thiết lập SSH từ secrets → scp upload → ssh vào server chạy npm ci/restart PM2[1]

Bạn muốn mình viết giúp 2 file workflow YAML mẫu (CI cho PR và CD deploy khi push main) theo stack Node.js như trong video để bạn copy vào dự án không?

[1] https://www.youtube.com/watch?v=YLtlz88zrLg