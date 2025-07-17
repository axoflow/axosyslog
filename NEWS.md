4.14.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * `cisco-parser()`: Added support for Cisco Nexus NXOS 9.3 syslog format
    ([#713](https://github.com/axoflow/axosyslog/pull/713))

  * `loggen`: Added `--client-port` option to set the outbound (client) port
    ([#709](https://github.com/axoflow/axosyslog/pull/709))


## FilterX features

  * `format_xml()` and `format_windows_eventlog_xml()`: new functions added

    Example usage:
    ```
    $MSG = format_xml({"a":{"b":"foo"}});
    ```
    ([#684](https://github.com/axoflow/axosyslog/pull/684))


## FilterX bugfixes

  * `metrics_labels()`: Fixed a bug where `update_metrics()` did not omit `null` values.
    ([#711](https://github.com/axoflow/axosyslog/pull/711))


## Other changes

  * `syslog-ng-ctl stats prometheus`: show orphan metrics

    Stale counters will be shown in order not to lose information, for example,
    when messages are sent using short-lived connections and metrics are scraped in
    minute intervals.

    We recommend using `syslog-ng-ctl stats --remove-orphans` during each configuration reload,
    but only after the values of those metrics have been scraped by all scrapers.
    ([#715](https://github.com/axoflow/axosyslog/pull/715))



[1] syslog-ng is a trademark of One Identity.

## Discord

For a bit more interactive discussion, join our Discord server:

[![Axoflow Discord Server](https://discordapp.com/api/guilds/1082023686028148877/widget.png?style=banner2)](https://discord.gg/E65kP9aZGm)

## Credits

AxoSyslog is developed as a community project, and as such it relies
on volunteers, to do the work necessary to produce AxoSyslog.

Reporting bugs, testing changes, writing code or simply providing
feedback is an important contribution, so please if you are a user
of AxoSyslog, contribute.

We would like to thank the following people for their contribution:

Andras Mitzki, Attila Szakacs, Balazs Scheidler, Hofi, László Várady,
Szilard Parrag, Tamás Kosztyu, shifter, Shiraz McClennon
