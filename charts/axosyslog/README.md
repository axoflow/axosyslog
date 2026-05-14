# axosyslog

![type: application](https://img.shields.io/badge/type-application-informational?style=flat-square) ![kube version: >=1.22.0-0](https://img.shields.io/badge/kube%20version->=1.22.0--0-informational?style=flat-square) [![artifact hub](https://img.shields.io/badge/artifact%20hub-axosyslog-informational?style=flat-square)](https://artifacthub.io/packages/helm/axosyslog/axosyslog)

AxoSyslog is the cloud-native syslog-ng for Kubernetes

**Homepage:** <https://axoflow.com/docs/axosyslog-core/>

## TL;DR;

```bash
helm repo add axosyslog https://axoflow.github.io/axosyslog
helm repo update
helm install --generate-name axosyslog/axosyslog
```

## Introduction

This chart bootstraps [AxoSyslog](https://axoflow.com/docs/axosyslog-core/) on a [Kubernetes](http://kubernetes.io) cluster.

The chart deploys two complementary components that can run side by side:

- A **collector DaemonSet** that tails pod logs from `/var/log/pods` and forwards them to configured destinations.
- A **syslog StatefulSet** that receives incoming syslog/OTLP traffic, which can include logs forwarded by the collector.

## Values

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| affinity | object | `{}` | Default affinity rules for all pods (overridden per component) |
| aggregator | object | See values.yaml | Aggregator StatefulSet: receives incoming syslog/OTLP traffic (including from the collector) and routes it to destinations. |
| aggregator.affinity | object | `{}` | Affinity rules for aggregator pod scheduling |
| aggregator.annotations | object | `{}` | Additional annotations for the aggregator StatefulSet and its pods |
| aggregator.bufferStorage.enabled | bool | `false` | Enable persistent volume for syslog-ng disk buffers |
| aggregator.bufferStorage.size | string | `"10Gi"` | Size of the disk buffer PVC |
| aggregator.bufferStorage.storageClass | string | `"standard"` | StorageClass for the disk buffer PVC |
| aggregator.config.raw | string | `""` | Override the entire syslog-ng.conf content for the aggregator |
| aggregator.extraVolumeMounts | list | `[]` | Additional volume mounts to add to the aggregator container |
| aggregator.extraVolumes | list | `[]` | Additional volumes to add to the aggregator pod |
| aggregator.hostAliases | list | `[]` | Custom entries added to /etc/hosts for aggregator pods |
| aggregator.labels | object | `{}` | Additional labels for the aggregator StatefulSet and its pods |
| aggregator.logFileStorage.enabled | bool | `false` | Enable persistent volume for written log files |
| aggregator.logFileStorage.size | string | `"50Gi"` | Size of the log file PVC |
| aggregator.logFileStorage.storageClass | string | `"standard"` | StorageClass for the log file PVC |
| aggregator.nodeSelector | object | `{}` | Node selector for aggregator pod assignment |
| aggregator.replicaCount | int | `1` | Number of aggregator replicas |
| aggregator.resources | object | `{}` | CPU/memory resource requests and limits for the aggregator. Falls back to global `resources` if not set. |
| aggregator.secretMounts | list | `[]` | Secrets to mount as files into the aggregator container |
| aggregator.securityContext | object | `{}` | Container-level security context for the aggregator |
| aggregator.tolerations | list | `[]` | Tolerations for aggregator pod scheduling |
| collector | object | See values.yaml | Collector DaemonSet: runs on every node to tail /var/log/pods and forward logs to destinations. |
| collector.affinity | object | `{}` | Affinity rules for collector pod scheduling |
| collector.annotations | object | `{}` | Additional annotations for the collector DaemonSet and its pods |
| collector.config.raw | string | `""` | Override the entire syslog-ng.conf content for the collector |
| collector.extraVolumeMounts | list | `[]` | Additional volume mounts to add to the collector container |
| collector.extraVolumes | list | `[]` | Additional volumes to add to the collector pod |
| collector.hostAliases | list | `[]` | Custom entries added to /etc/hosts for collector pods |
| collector.hostNetworking | bool | `false` | Use the host network namespace for collector pods |
| collector.labels | object | `{}` | Additional labels for the collector DaemonSet and its pods |
| collector.maxUnavailable | int | `1` | Maximum number of unavailable pods during a DaemonSet rolling update |
| collector.nodeSelector | object | `{}` | Node selector for collector pod assignment |
| collector.resources | object | `{}` | CPU/memory resource requests and limits for the collector. Falls back to global `resources` if not set. |
| collector.secretMounts | list | `[]` | Secrets to mount as files into the collector container |
| collector.securityContext | object | `{}` | Container-level security context for the collector |
| collector.tolerations | list | `[]` | Tolerations for collector pod scheduling |
| dnsConfig | object | `{}` | Custom DNS configuration for all pods |
| extraVolumeMounts | list | `[]` | Default extra volume mounts for all pods (overridden per component) |
| extraVolumes | list | `[]` | Default extra volumes for all pods (overridden per component) |
| fullnameOverride | string | `""` | Override the fully qualified chart name |
| hostAliases | list | `[]` | Default custom /etc/hosts entries for all pods (overridden per component) |
| image.extraArgs | list | `[]` | Additional arguments passed to the syslog-ng process |
| image.pullPolicy | string | `"IfNotPresent"` | Image pull policy |
| image.repository | string | `"ghcr.io/axoflow/axosyslog"` | Container image repository |
| image.tag | string | `""` | Image tag (defaults to Chart.AppVersion) |
| imagePullSecrets | list | `[]` | Secrets for pulling images from private registries |
| metricsExporter.enabled | bool | `false` | Deploy axosyslog-metrics-exporter as a sidecar on the collector DaemonSet |
| metricsExporter.image.pullPolicy | string | `nil` | Metrics exporter image pull policy (omitted when unset, letting Kubernetes apply its default) |
| metricsExporter.image.repository | string | `"ghcr.io/axoflow/axosyslog-metrics-exporter"` | Metrics exporter image repository |
| metricsExporter.image.tag | string | `"latest"` | Metrics exporter image tag |
| metricsExporter.resources | object | `{}` | CPU/memory resource requests and limits for the metrics exporter sidecar |
| metricsExporter.securityContext | object | `{}` | Container-level security context for the metrics exporter sidecar |
| nameOverride | string | `""` | Override the chart name |
| namespace | string | `""` | Namespace override (defaults to Release.Namespace) |
| nodeSelector | object | `{}` | Default node selector for all pods (overridden per component) |
| openShift.enabled | bool | `false` | Enable OpenShift-specific resources |
| openShift.securityContextConstraints.annotations | object | `{}` | Annotations for the SecurityContextConstraints resource |
| openShift.securityContextConstraints.create | bool | `true` | Create a SecurityContextConstraints resource for OpenShift |
| service.create | Whether to create the service | true |
| service.type | Type of the service to create | NodePort |
| service.annotations | Annotations to apply to the service | {} |
| service.extraPorts | Additional ports to expose on the service | [] |
| podAnnotations | object | `{}` | Annotations applied to all pods |
| podMonitor.annotations | object | `{}` | Additional annotations for the PodMonitor |
| podMonitor.enabled | bool | `false` | Deploy a PodMonitor CR for Prometheus Operator (requires metricsExporter.enabled) |
| podMonitor.labels | object | `{}` | Additional labels for the PodMonitor |
| podSecurityContext | object | `{}` | Pod-level security context applied to all pods |
| priorityClassName | string | `""` | Priority class name for all pods |
| rbac.create | bool | `true` | Create ClusterRole and ClusterRoleBinding for the collector |
| rbac.extraRules | list | `[]` | Additional RBAC rules to add to the ClusterRole |
| resources | object | `{}` | Default CPU/memory resource requests and limits for all components (overridden per component via collector.resources / aggregator.resources) |
| secretMounts | list | `[]` | Default secrets to mount as files (overridden per component) |
| securityContext | object | `{}` | Default container-level security context for all components (overridden per component via collector.securityContext / aggregator.securityContext) |
| service.create | bool | `true` | Create a Service for the aggregator StatefulSet |
| service.extraPorts | list | `[]` | Additional ports to expose on the aggregator Service |
| serviceAccount.annotations | object | `{}` | Annotations for the ServiceAccount |
| serviceAccount.create | bool | `true` | Create a ServiceAccount for the pods |
| terminationGracePeriodSeconds | int | `30` | Time in seconds given to pods to terminate gracefully |
| tolerations | list | `[]` | Default tolerations for all pods (overridden per component) |
| updateStrategy | string | `"RollingUpdate"` | Update strategy type for the DaemonSet and StatefulSet |
