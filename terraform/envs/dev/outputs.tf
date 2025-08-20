output "cluster_name" {
  value = module.eks.cluster_name
}

output "cluster_endpoint" {
  value = data.aws_eks_cluster.this.endpoint
}

output "region" {
  value = var.aws_region
}

output "oidc_provider_arn" {
  value = module.eks.oidc_provider_arn
}

