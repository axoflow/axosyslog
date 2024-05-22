<p align="center">
  <picture>
    <source media="(prefers-color-scheme: light)" srcset="https://github.com/axoflow/axosyslog/raw/main/doc/axosyslog.svg">
    <source media="(prefers-color-scheme: dark)" srcset="https://github.com/axoflow/axosyslog/raw/main/doc/axosyslog-white.svg">
    <img alt="Axoflow" src="https://github.com/axoflow/axosyslog/raw/main/doc/axosyslog.svg" width="550">
  </picture>
</p>

# AxoSyslog - a cloud-native distribution of syslog-ng by Axoflow


[![Discord](https://img.shields.io/discord/1082023686028148877?label=Discord&logo=discord&logoColor=white)](https://discord.gg/qmq53uBm2c)
[![Build Status](https://github.com/axoflow/axosyslog/actions/workflows/devshell.yml/badge.svg)](https://github.com/axoflow/axosyslog/actions/workflows/devshell.yml)
[![Nightly](https://github.com/axoflow/axosyslog/actions/workflows/axosyslog-nightly.yml/badge.svg)](https://github.com/axoflow/axosyslog/actions/workflows/axosyslog-nightly.yml)
[![Binary packages](https://github.com/axoflow/axosyslog/actions/workflows/packages.yml/badge.svg)](https://github.com/axoflow/axosyslog/actions/workflows/packages.yml)

This repository contains the AxoSyslog source tree, cloud-ready syslog-ng images, and Helm charts
created and maintained by [Axoflow](https://axoflow.com).

## Container images

You can find the list of tagged versions at [https://github.com/axoflow/axosyslog/pkgs/container/axosyslog](https://github.com/axoflow/axosyslog/pkgs/container/axosyslog).

To install the latest stable version, run:

```shell
docker pull ghcr.io/axoflow/axosyslog:latest
```

You can also use it as a base image in your Dockerfile:

```shell
FROM ghcr.io/axoflow/axosyslog:latest
```

If you want to test a development version, you can use the nightly builds:

```shell
docker pull ghcr.io/axoflow/axosyslog:nightly
```

> Note: These named packages are automatically updated when a new syslog-ng package is released. To install a specific version, run `docker pull ghcr.io/axoflow/axosyslog:<version-number>`, for example:
>
> ```shell
> docker pull ghcr.io/axoflow/axosyslog:4.7.1
> ```

### Difference from upstream images

Our images are different from the [upstream syslog-ng images](https://hub.docker.com/r/balabit/syslog-ng/) in a number of ways:

- They are based on Alpine Linux, instead of Debian testing for reliability and smaller size (thus smaller attack surface).
- They incorporate cloud-native features and settings (such as the Kubernetes source).
- They incorporate container-level optimizations (like the use of an alternative malloc library) for better performance and improved security.
- They support the ARM architecture.

Our images are available for the following architectures:

- amd64
- arm/v7
- arm64

## Helm Charts

AxoSyslog provides [Helm charts](https://helm.sh/docs/topics/charts/) to deploy syslog-ng on Kubernetes.

[Helm](https://helm.sh) must be installed to use the charts.  Please refer to
Helm's [documentation](https://helm.sh/docs) to get started.

Once Helm has been set up correctly, add the repo as follows:

    helm repo add axosyslog https://axoflow.github.io/axosyslog

If you had already added this repo earlier, run `helm repo update` to retrieve
the latest versions of the packages.  You can then run `helm search repo
axosyslog` to see the charts.

To install the axosyslog chart:

    helm install my-axosyslog axosyslog/axosyslog

To uninstall the chart:

    helm delete my-axosyslog

## Documentation

You can find [comprehensive documentation for AxoSyslog](https://axoflow.com/docs/axosyslog-core)
on the [Axoflow website](https://axoflow.com/).

The documentation is a combination of the syslog-ng reference guide and the
AxoSyslog reference guide.  The [syslog-ng documentation](https://axoflow.com/docs/axosyslog-core)
was included in there as the upstream documentation fell out of maintenance.

## Contact and support

In case you need help or want to contact us, open a [GitHub issue](https://github.com/axoflow/axosyslog/issues), or come chat with us in the [syslog-ng channel of the Axoflow Discord server](https://discord.gg/4Fzy7D66Qq).

## Contribution

If you have fixed a bug or would like to contribute your improvements to
AxoSyslog, [open a pull request](https://github.com/axoflow/axosyslog/pulls). We truly appreciate your help.

## About Axoflow

The [Axoflow](https://axoflow.com) founder team consists of successful entrepreneurs with a vast knowledge and hands-on experience about observability, log management, and how to apply these technologies in the enterprise security context. We also happen to be the creators of wide-spread open source technologies in this area, like syslog-ng and the [Logging operator for Kubernetes](https://github.com/kube-logging/logging-operator).

To learn more about our products and our open-source projects, visit the [Axoflow blog](https://axoflow.com/blog/), or [subscribe to the Axoflow newsletter](https://axoflow.com/#newsletter-subscription).
