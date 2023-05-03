# AxoSyslog Collector

AxoSyslog Kubernetes log collector.

## Prerequisites

- Kubernetes 1.16+
- Helm 3.0+


## Configuration
The following table lists the configurable parameters of the AxoSyslog Collector chart and their default values:


| Parameter | Description | Default |
| --------- | ----------- | ------- |
|  image.repository  | The container image repository |  ghcr.io/axoflow/axosyslog  |
|  image.pullPolicy  | The container image pull policy |  IfNotPresent  |
|  image.tag  | The container image tag |  4.1.1  |
|  imagePullSecrets  | The names of secrets containing private registry credentials |  []  |
|  nameOverride  | Override the chart name |  ""  |
|  fullnameOverride  | Override the full chart name |  ""  |
|  daemonset.enabled  | Deploy AxoSyslog as a DaemonSet |  true  |
|  daemonset.labels  | Additional labels to apply to the DaemonSet |  {}  |
|  daemonset.annotations  | Additional annotations to apply to the DaemonSet |  {}  |
|  daemonset.affinity  | Pod affinity |  {}  |
|  daemonset.nodeSelector  | Node labels for pod assignment |  {}  |
|  daemonset.resources  | Resource requests and limits |  {}  |
|  daemonset.tolerations  | Tolerations for pod assignment |  []  |
|  daemonset.hostAliases  | Add host aliases |  []  |
|  daemonset.secretMounts  | Mount additional secrets as volumes |  []  |
|  daemonset.extraVolumes  | Additional volumes to mount |  []  |
|  daemonset.securityContext  | Security context for the pod |  {}  |
|  daemonset.maxUnavailable  | The maximum number of unavailable pods during a rolling update |  1  |
|  daemonset.hostNetworking  | Whether to enable host networking |  false  |
|  rbac.create  | Whether to create RBAC resources |  false  |
|  rbac.extraRules  | Additional RBAC rules |  []  |
|  openShift.enabled  | Whether to deploy on OpenShift |  false  |
|  openShift.securityContextConstraints.create  | Whether to create SecurityContextConstraints on OpenShift |  true  |
|  openShift.securityContextConstraints.annotations  | Annotations to apply to SecurityContextConstraints |  {}  |
|  serviceAccount.create  | Whether to create a service account |  false  |
|  serviceAccount.annotations  | Annotations to apply to the service account |  {}  |
|  namespace  | The Kubernetes namespace to deploy to |  ""  |
|  podAnnotations  | Additional annotations to apply to the pod |  {}  |
|  podSecurityContext  | Security context for the pod |  {}  |
|  securityContext  | Security context for the container |  {}  |
|  resources  | Resource requests and limits for the container |  {}  |
|  nodeSelector  | Node labels for pod assignment |  {}  |
|  tolerations  | Tolerations for pod assignment |  []  |
|  affinity  | Pod affinity |  {}  |
|  updateStrategy  | Update strategy for the DaemonSet |  RollingUpdate  |
