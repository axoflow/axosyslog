4.21.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * `format_syslog_5424()`: Added new FilterX function for syslog formatting

    Usage:
    ```
    format_syslog_5424(
      message,
      add_octet_count=false,
      pri=expr,
      timestamp=expr,
      host=expr,
      program=expr,
      pid=expr,
      msgid=expr
    )
    ```

    `message` is the only mandatory argument.

    Fallback values will be used instead of the named arguments
    if they are not set, or their evaluation fails.
    ([#875](https://github.com/axoflow/axosyslog/pull/875))

  * `http()` and other threaded destinations: add `worker-partition-autoscaling(yes)`

    When `worker-partition-key()` is used to categorize messages into different batches,
    the messages are - by default - hashed into workers, which prevents them from being distributed across workers
    efficiently, based on load.

    The new `worker-partition-autoscaling(yes)` option uses a 1-minute statistic to help distribute
    high-traffic partitions among multiple workers, allowing each worker to maximize its batch size.

    When using this autoscaling option, it is recommended to oversize the number of workers: set it higher than the
    expected number of partitions.
    ([#855](https://github.com/axoflow/axosyslog/pull/855))

  * `network()`: add `transport(nul-terminated)` to support NULL characters to separate log records instead of the
    more traditional newline separated format
    ([#867](https://github.com/axoflow/axosyslog/pull/867))

  * New metrics: `syslogng_output_workers` and `syslogng_output_active_worker_partitions`

    Using the new `worker-partition-autoscaling(yes)` option allows producing partition metrics, which can be used
    for alerting: if the number of active partitions remains higher than the configured number of workers,
    it may indicate that events are not being batched properly, which can lead to performance degradation.
    ([#866](https://github.com/axoflow/axosyslog/pull/866))

## Bugfixes

  * FilterX `parse_csv()`: fix crash
    ([#879](https://github.com/axoflow/axosyslog/pull/879))

  * FilterX `metrics_labels()`: fix `+=` operator
    ([#878](https://github.com/axoflow/axosyslog/pull/878))

  * `disk-buffer()`: fix memory leaks
    ([#872](https://github.com/axoflow/axosyslog/pull/872))

## Other changes

  * `disk-buffer()`: significant performance improvements for the non-reliable disk buffer
    ([#857](https://github.com/axoflow/axosyslog/pull/857))

  * Performance improvements for memory queues
    ([#881](https://github.com/axoflow/axosyslog/pull/881))

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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, László Várady, Szilard Parrag,
Tamás Kosztyu, shifter
