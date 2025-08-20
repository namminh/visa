terraform {
  backend "local" {}
}

# For prod/staging, switch to an S3/GCS backend with locking.
# Example (S3):
# backend "s3" {
#   bucket = "your-tfstate-bucket"
#   key    = "mini-visa/dev/terraform.tfstate"
#   region = "ap-southeast-1"
#   dynamodb_table = "your-tflock-table"
# }

