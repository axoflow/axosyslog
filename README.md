<p align="center">
  <picture>
    <source media="(prefers-color-scheme: light)" srcset="https://github.com/axoflow/axosyslog/raw/main/doc/axosyslog.svg">
    <source media="(prefers-color-scheme: dark)" srcset="https://github.com/axoflow/axosyslog/raw/main/doc/axosyslog-white.svg">
    <img alt="Axoflow" src="https://github.com/axoflow/axosyslog/raw/main/doc/axosyslog.svg" width="550">
  </picture>
</p>

# AxoSyslog - the scalable security data processor


[![Discord](https://img.shields.io/discord/1082023686028148877?label=Discord&logo=discord&logoColor=white)](https://discord.gg/qmq53uBm2c)
[![Build Status](https://github.com/axoflow/axosyslog/actions/workflows/devshell.yml/badge.svg)](https://github.com/axoflow/axosyslog/actions/workflows/devshell.yml)
[![Nightly](https://github.com/axoflow/axosyslog/actions/workflows/axosyslog-nightly.yml/badge.svg)](https://github.com/axoflow/axosyslog/actions/workflows/axosyslog-nightly.yml)
[![Binary packages](https://github.com/axoflow/axosyslog/actions/workflows/packages.yml/badge.svg)](https://github.com/axoflow/axosyslog/actions/workflows/packages.yml)

AxoSyslog started as a syslog-ng [[1]](#r1) fork, branched right after
syslog-ng v4.7.1 with the following focus:
  * cloud native (containers, helm charts, kubernetes integration),
  * security data tailored parsing and transformation (filterx, app-parser, app-transform, etc)
  * performance (eBPF, memory allocator, etc),

AxoSyslog (created by the original creators of syslog-ng [[1]](#r1)):
- is a drop in replacement for syslog-ng [[1]](#r1),
- keeps using the same license and development practices.

This repository contains the AxoSyslog source tree, container images, and Helm charts
created and maintained by [Axoflow](https://axoflow.com).

<a id="r1">[1]</a> syslog-ng is a trademark of One Identity.

## Quick-start

To start using AxoSyslog, you can use one of these
deployment mechanisms:
  - pure containers (docker, podman)
  - Helm charts (Kubernetes)
  - packages (deb, rpm, etc)

Once the binaries are deployed, create a configuration file called 
`/etc/syslog-ng/syslog-ng.conf`, which will then be processed by
the `syslog-ng` process.

A simple example is to ingest syslog traffic on tcp/514 and write it to a
file:

```
@version: 4.7
@include "scl.conf"

log {
	source {
		system();
		network();
	};
	destination { file("/var/log/syslog"); };
};
```

You can find more examples in the [Quickstart section of the
documentation](https://axoflow.com/docs/axosyslog-core/quickstart/).

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

The container images contain a default configuration file which you probably
want to customize. Read more about using these images [directly via
podman/docker](https://axoflow.com/docs/axosyslog-core/install/podman-systemd/)

Our images are available for the following architectures:

- amd64
- arm/v7
- arm64

## Helm Charts

AxoSyslog provides [Helm charts](https://helm.sh/docs/topics/charts/) to deploy on Kubernetes.

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

Helm charts would use the latest images by default, but you can customize
that via the values file.
For details, see [Install AxoSyslog with Helm](https://axoflow.com/docs/axosyslog-core/install/helm/).
## Documentation

You can find [comprehensive documentation for AxoSyslog](https://axoflow.com/docs/axosyslog-core)
on the [Axoflow website](https://axoflow.com/).

## Difference from syslog-ng

The original founder of syslog-ng forked off AxoSyslog from the original
syslog-ng after the 4.7.1 release. AxoSyslog is a drop in replacement, retaining
the original license, release schedule and processes.

## Contact and support

In case you need help or want to contact us, open a [GitHub issue](https://github.com/axoflow/axosyslog/issues),
or come chat with us in the [syslog-ng channel of the Axoflow Discord server](https://discord.gg/4Fzy7D66Qq).

## Contribution

If you have fixed a bug or would like to contribute your improvements to
AxoSyslog, [open a pull request](https://github.com/axoflow/axosyslog/pulls). We truly appreciate your help.

## About Axoflow

The [Axoflow](https://axoflow.com) founder team has a long history and
hands-on experience about observability, log management, and how to apply
these technologies in the enterprise security context.  We also happen to be
the original creators of wide-spread open source technologies in this area, like
syslog-ng and the [Logging operator for Kubernetes](https://github.com/kube-logging/logging-operator).

To learn more about our products and our open-source projects, visit the
[Axoflow blog](https://axoflow.com/blog/), or [subscribe to the Axoflow
newsletter](https://axoflow.com/#newsletter-subscription).
