variable "aws_region" {
  description = "AWS region for the EKS cluster"
  type        = string
  default     = "ap-southeast-1"
}

variable "cluster_name" {
  description = "EKS cluster name"
  type        = string
  default     = "mini-visa-dev"
}

variable "vpc_cidr" {
  description = "VPC CIDR"
  type        = string
  default     = "10.10.0.0/16"
}

variable "kubernetes_version" {
  description = "Kubernetes version for EKS"
  type        = string
  default     = "1.29"
}

variable "node_instance_types" {
  description = "Instance types for managed node group"
  type        = list(string)
  default     = ["t3.medium"]
}

variable "node_desired_size" {
  description = "Desired nodes"
  type        = number
  default     = 2
}

variable "node_min_size" {
  description = "Min nodes"
  type        = number
  default     = 1
}

variable "node_max_size" {
  description = "Max nodes"
  type        = number
  default     = 3
}

