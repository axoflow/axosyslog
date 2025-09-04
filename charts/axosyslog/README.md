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
|  collector.enabled  | Deploy AxoSyslog as a DaemonSet |  true  |
|  collector.labels  | Additional labels to apply to the DaemonSet |  {}  |
|  collector.annotations  | Additional annotations to apply to the DaemonSet |  {}  |
|  collector.affinity  | Pod affinity |  {}  |
|  collector.nodeSelector  | Node labels for pod assignment |  {}  |
|  collector.resources  | Resource requests and limits |  {}  |
|  collector.tolerations  | Tolerations for pod assignment |  []  |
|  collector.hostAliases  | Add host aliases |  []  |
|  collector.secretMounts  | Mount additional secrets as volumes |  []  |
|  collector.extraVolumes  | Additional volumes to mount |  []  |
|  collector.extraVolumeMounts  | Additional volume mounts |  []  |
|  collector.securityContext  | Security context for the pod |  {}  |
|  collector.maxUnavailable  | The maximum number of unavailable pods during a rolling update |  1  |
|  collector.hostNetworking  | Whether to enable host networking |  false  |
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
|  kubernetes.enabled  | Enable kubernetes log collection  |  true  |
|  kubernetes.prefix  | Set JSON prefix for logs collected from the k8s cluster  |  ""  |
|  kubernetes.keyDelimiter  | Set JSON key delimiter for logs collected from the k8s cluster  |  ""  |
|  extraVolumes | Additional volumes to mount | [] |
|  extraVolumeMounts | Additional volume mounts | [] |
|  syslog.enabled | Enable the syslog component | false |
|  syslog.config | Syslog-ng configuration content | {} |
|  syslog.labels | Additional labels for the StatefulSet | {} |
|  syslog.annotations | Additional annotations for the StatefulSet | {} |
|  syslog.logFileStorage.enabled | Enable persistent storage for log files | false |
|  syslog.logFileStorage.size | Size of log file storage volume | 50Gi |
|  syslog.bufferStorage.enabled | Enable persistent storage for syslog buffers | false |
|  syslog.bufferStorage.size | Size of buffer storage volume | 10Gi |
|  syslog.extraVolumeMounts | Additional volume mounts for the container | [] |
|  syslog.extraVolumes | Additional volumes for the pod | [] |

