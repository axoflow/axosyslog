# Helm Charts

Various Helm [charts](https://helm.sh/docs/topics/charts/) for syslog-ng.


## Usage

[Helm](https://helm.sh) must be installed to use these charts.
Please refer to the [official documentation](https://helm.sh/docs/intro/install/) to get started.

Currently the charts are only available through this git repositoy.
You can install charts using the following commands:

```bash
$ git clone git@github.com:axoflow/syslog-ng-charts.git
$ cd syslog-ng-charts
$ helm install --generate-name charts/syslog-ng-collector
```

> **Tip**: List all installed releases using `helm list`.

To uninstall a chart release:

```bash
$ helm delete my-release
```
