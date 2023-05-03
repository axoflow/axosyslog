<p align="center">
  <picture>
    <source media="(prefers-color-scheme: light)" srcset="https://github.com/axoflow/axosyslog-docker/raw/main/docs/axoflow-logo-color.svg">
    <source media="(prefers-color-scheme: dark)" srcset="https://github.com/axoflow/axosyslog-docker/raw/main/docs/axoflow-logo-white.svg">
    <img alt="Axoflow" src="https://github.com/axoflow/axosyslog-docker/raw/main/docs/axoflow-logo-color.svg" width="550">
  </picture>
</p>

# AxoSyslog Helm Charts - a cloud-native distribution of syslog-ng by Axoflow

This repository contains various [Helm charts](https://helm.sh/docs/topics/charts/) for syslog-ng. You can use these charts to install the [AxoSyslog - cloud-ready syslog-ng images](https://github.com/axoflow/axosyslog-docker) created and maintained by [Axoflow](https://axoflow.com).

## How to use

[Helm](https://helm.sh) must be installed to use these charts.
Please refer to the [official documentation](https://helm.sh/docs/intro/install/) to get started.

Currently the charts are only available through this git repositoy.
You can install charts using the following commands:

```bash
git clone git@github.com:axoflow/axosyslog-charts.git
cd axosyslog-charts
helm install --generate-name charts/axosyslog-collector
```

> **Tip**: List all installed releases using `helm list`.

To uninstall a chart release, run:

```bash
helm delete my-release
```

## Contact and support

In case you need help or want to contact us, open a [GitHub issue](https://github.com/axoflow/axosyslog-charts/issues), or come chat with us in the [syslog-ng channel of the Axoflow Discord server](https://discord.gg/4Fzy7D66Qq).

## Contribution

If you have fixed a bug or would like to contribute your improvements to these charts, [open a pull request](https://github.com/axoflow/axosyslog-charts/pulls). We truly appreciate your help.

## About Axoflow

The [Axoflow](https://axoflow.com) founder team consists of successful entrepreneurs with a vast knowledge and hands-on experience about observability, log management, and how to apply these technologies in the enterprise security context. We also happen to be the creators of wide-spread open source technologies in this area, like syslog-ng and the [Logging operator for Kubernetes](https://github.com/kube-logging/logging-operator).

To learn more about our products and our open-source projects, visit the [Axoflow blog](https://axoflow.com/blog/), or [subscribe to the Axoflow newsletter](https://axoflow.com/#newsletter-subscription).
