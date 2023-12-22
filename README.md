<p align="center">
  <picture>
    <source media="(prefers-color-scheme: light)" srcset="https://github.com/axoflow/axosyslog-docker/raw/main/docs/axosyslog.svg">
    <source media="(prefers-color-scheme: dark)" srcset="https://github.com/axoflow/axosyslog-docker/raw/main/docs/axosyslog-white.svg">
    <img alt="Axoflow" src="https://github.com/axoflow/axosyslog-docker/raw/main/docs/axosyslog.svg" width="550">
  </picture>
</p>

# AxoSyslog - a cloud-native distribution of syslog-ng by Axoflow

This repository contains the cloud-ready syslog-ng images and Helm charts
created and maintained by [Axoflow](https://axoflow.com).  

## Container images

Our images are
different from the [upstream syslog-ng
images](https://hub.docker.com/r/balabit/syslog-ng/) in a number of ways:

- They are based on Alpine Linux, instead of Debian testing for reliability and smaller size (thus smaller attack surface).
- They incorporate cloud-native features and settings (such as the Kubernetes source).
- They incorporate container-level optimizations (like the use of an alternative malloc library) for better performance and improved security.
- They support the ARM architecture.

Our images are available for the following architectures:

- amd64
- arm/v7
- arm64

### How to use

You can find the list of tagged versions at [https://github.com/axoflow/axosyslog-docker/pkgs/container/axosyslog](https://github.com/axoflow/axosyslog-docker/pkgs/container/axosyslog).

_NOTE: Images found at [https://github.com/axoflow/axosyslog-docker/pkgs/container/syslog-ng](https://github.com/axoflow/axosyslog-docker/pkgs/container/syslog-ng) are deprecated and will be removed in the future._

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
> docker pull ghcr.io/axoflow/axosyslog:4.5.0
> ```

## Helm Charts

This repository contains various [Helm charts](https://helm.sh/docs/topics/charts/) for syslog-ng. You can use these charts to install the [AxoSyslog - cloud-ready syslog-ng images](https://github.com/axoflow/axosyslog-docker) created and maintained by [Axoflow](https://axoflow.com).

### How to use

[Helm](https://helm.sh) must be installed to use the charts.  Please refer to
Helm's [documentation](https://helm.sh/docs) to get started.

Once Helm has been set up correctly, add the repo as follows:

    helm repo add axosyslog https://axoflow.github.io/axosyslog-charts

If you had already added this repo earlier, run `helm repo update` to retrieve
the latest versions of the packages.  You can then run `helm search repo
axosyslog` to see the charts.

To install the axosyslog-collector chart:

    helm install my-axosyslog-collector axosyslog/axosyslog-collector

To uninstall the chart:

    helm delete my-axosyslog-collector

## Contact and support

In case you need help or want to contact us, open a [GitHub issue](https://github.com/axoflow/axosyslog-docker/issues), or come chat with us in the [syslog-ng channel of the Axoflow Discord server](https://discord.gg/4Fzy7D66Qq).

## Contribution

If you have fixed a bug or would like to contribute your improvements to
AxoSyslog, [open a pull request](https://github.com/axoflow/axosyslog-docker/pulls). We truly appreciate your help.

## About Axoflow

The [Axoflow](https://axoflow.com) founder team consists of successful entrepreneurs with a vast knowledge and hands-on experience about observability, log management, and how to apply these technologies in the enterprise security context. We also happen to be the creators of wide-spread open source technologies in this area, like syslog-ng and the [Logging operator for Kubernetes](https://github.com/kube-logging/logging-operator).

To learn more about our products and our open-source projects, visit the [Axoflow blog](https://axoflow.com/blog/), or [subscribe to the Axoflow newsletter](https://axoflow.com/#newsletter-subscription).
