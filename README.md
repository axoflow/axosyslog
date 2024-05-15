# Helm charts for AxoSyslog.

[![Artifact Hub](https://img.shields.io/endpoint?url=https://artifacthub.io/badge/repository/axosyslog)](https://artifacthub.io/packages/search?repo=axosyslog)
![Kubernetes Version](https://img.shields.io/badge/k8s%20version-%3E=1.19-3f68d7.svg?style=flat-square)


## Usage

[Helm](https://helm.sh) must be installed to use these charts.
Please refer to the [official documentation](https://helm.sh/docs/intro/install/) to get started.

```bash
helm repo add axosyslog https://axoflow.github.io/axosyslog
```

You can then see the charts by running:

```bash
helm search repo axosyslog
```

You can install charts using the following command:

```bash
helm install --generate-name axosyslog/axosyslog
# OR
helm install my-axosyslog axosyslog/axosyslog
```

> **Tip**: List all installed releases using `helm list`.

To uninstall a chart release:

```bash
helm delete my-axosyslog
```
