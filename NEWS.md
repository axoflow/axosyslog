9.99.99
=======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Highlights

<Fill this block manually from the blocks below>

## Bugfixes

  * `rate-limit()`: fix precision issue that could occur at a very low message rate
    ([#599](https://github.com/axoflow/axosyslog/pull/599))

  * `metrics`: fix `syslogng_last_config_file_modification_timestamp_seconds`
    ([#612](https://github.com/axoflow/axosyslog/pull/612))


## FilterX features

  * `strcasecmp()`: case insensitive string comparison
    ([#580](https://github.com/axoflow/axosyslog/pull/580))


## FilterX bugfixes

  * `metrics_labels`: Fixed a crash that occurred when trying to get a label from an empty object.
    ([#601](https://github.com/axoflow/axosyslog/pull/601))

  * `cache_json_file()`: fix updating json content on file changes
    ([#612](https://github.com/axoflow/axosyslog/pull/612))


## Other changes

  * `loggen`: statistics output has slightly changed

    The new `--perf` option can be used to measure the log throughput of AxoSyslog.
    ([#598](https://github.com/axoflow/axosyslog/pull/598))

  * `stats()`: `freq()` now defaults to 0

    Internal statistic messages were produced every 10 minutes by default.
    Metrics are available through `syslog-ng-ctl`, we believe modern monitoring and
    observability render this periodic message obsolete.
    ([#600](https://github.com/axoflow/axosyslog/pull/600))



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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, Bálint Horváth,
Christian Heusel, Hofi, László Várady, Mate Ory
