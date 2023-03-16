
![Axoflow logo](AxoFlowBlueBlack.svg)

# Cloud-ready syslog-ng images by Axoflow

This repository contains the cloud-ready syslog-ng images created and maintained by [Axoflow](https://axoflow.com). Our images are different from the [upstream syslog-ng images](https://hub.docker.com/r/balabit/syslog-ng/) in a number of ways:

- They are based on Alpine Linux, instead of Debian testing for reliability and smaller size (thus smaller attack surface).
- They incorporate cloud-native features and settings (such as the Kubernetes source).
- They incorporate container-level optimizations (like the use of an alternative malloc library) for better performance and improved security.
- They support the ARM architecture.

Our images are available for the following architectures:

- amd64
- arm/v7
- arm64

## How to use

You can find the list of tagged versions at [https://github.com/axoflow/syslog-ng-docker/pkgs/container/syslog-ng](https://github.com/axoflow/syslog-ng-docker/pkgs/container/syslog-ng).

To install the latest stable version, run:

```shell
docker pull ghcr.io/axoflow/syslog-ng:latest
```

You can also use it as a base image in your Dockerfile:

```shell
FROM ghcr.io/axoflow/syslog-ng:latest
```

If you want to test a development version, you can use the nightly builds:

```shell
docker pull ghcr.io/axoflow/syslog-ng:nightly
```

## Contact and support

In case you need help or want to contact us, open a [GitHub issue](https://github.com/axoflow/syslog-ng-docker/issues), or come chat with us on the [Axoflow Discord server](FIXME).

## Contribution

https://github.com/axoflow/syslog-ng-docker/pulls

## About Axoflow

The [Axoflow](https://axoflow.com) founder team consists of successful entrepreneurs with a vast knowledge and hands-on experience about observability, log management, and how to apply these technologies in the enterprise security context. We also happen to be the creators of wide-spread open source technologies in this area, like syslog-ng and the [Logging operator for Kubernetes](https://github.com/kube-logging/logging-operator).

To learn more about our products and our open-source projects, visit the [Axoflow blog](https://axoflow.com/blog/), or [subscribe to the Axoflow newsletter](https://axoflow.com/#newsletter-subscription).
