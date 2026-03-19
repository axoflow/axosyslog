4.24.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.


## Features

  * Supported metrics now can be queried with `syslog-ng --metrics-registry`
    ([#993](https://github.com/axoflow/axosyslog/pull/993))

  * `http()` and other threaded destinations: add `batch-idle-timeout()` option

    While `batch-timeout()` defines the maximum amount of time a batch may wait before it is sent (starting from the first message),
    the new `batch-idle-timeout()` measures the elapsed time since the last message was added to the batch.

    Whichever timeout expires first will close and send the batch.

    `batch-idle-timeout()` defaults to 0 (disabled).
    ([#950](https://github.com/axoflow/axosyslog/pull/950))

  * `fix-timestamp()`, `guess_timestamp()` and `set_timestamp()`: new filterx functions.

    Example usage:
    ```
    datetime = strptime("2000-01-01T00:00:00 +0200", "%Y-%m-%dT%H:%M:%S %z");
    timezone = "CET";
    fixed_datetime = fix_timezone(datetime, timezone);
    guessed_datetime = guess_timezone(datetime);
    set_datetime = set_timezone(datetime, "CET");
    ```
    ([#960](https://github.com/axoflow/axosyslog/pull/960))

  * `get_timezone_source()`: new filterx function

    Returns a string indicating where the timezone information originates from.
    Possible values:
     - `"parsed"`: the timezone info is explicitly parsed from the timestamp
     - `"assumed"`: the timestamp didn't contain timezone info, default used (currently local timezone)
     - `"fixed"`: the timezone info was set using `fix_timezone()` or `set_timezone()` functions
     - `"guessed"`: the timezone info was set using `guess_timezone()` function

    Example usage:
    ```
    if (get_timezone_source(timestamp) === "assumed") {
      timestamp = fix_timezone(timestamp, "CET");
    };
    ```
    ([#979](https://github.com/axoflow/axosyslog/pull/979))

  * network-load-balancer: add support for failover

    The confgen script for `network-load-balancer` now supports failover, generating the list of failover servers for each destination automatically.
    ([#908](https://github.com/axoflow/axosyslog/pull/908))


## Bugfixes

  * FilterX LEEF formatter/parser: fix escaping of `=` in extension values
    ([#986](https://github.com/axoflow/axosyslog/pull/986))

  * `dynamic-window-size()`: fix occasional crash and incorrect window calculation
    ([#988](https://github.com/axoflow/axosyslog/pull/988))

  * `opentelemetry()` and other threaded sources: fix occasional crash during reload
    ([#989](https://github.com/axoflow/axosyslog/pull/989))

  * Fix various memory leaks
    ([#978](https://github.com/axoflow/axosyslog/pull/978), [#954](https://github.com/axoflow/axosyslog/pull/954),
    [#953](https://github.com/axoflow/axosyslog/pull/953), [#982](https://github.com/axoflow/axosyslog/pull/982))

  * `network()`/`syslog()` sources and parser: fix crash when using the `sanitize-utf8` flag on large invalid messages
    ([#985](https://github.com/axoflow/axosyslog/pull/985))

  * `opentelemetry()` source: fixed a bug causing the source to hang on reload
    ([#991](https://github.com/axoflow/axosyslog/pull/991))

  * `strptime()`: fix parsing `%s` format.
    ([#984](https://github.com/axoflow/axosyslog/pull/984))

  * `disk-buffer()`: detect abandoned metrics in default directory
    ([#965](https://github.com/axoflow/axosyslog/pull/965))

  * `date-parser()` and FilterX `strptime()`: accept "UTC" in `%z`/`%Z`
    ([#1002](https://github.com/axoflow/axosyslog/pull/1002))


## Other changes

  * `disk-buffer()`: the serialization format has been upgraded to `v27`
    ([#933](https://github.com/axoflow/axosyslog/pull/933))

  * FilterX: JSON-related functionality is now powered by `jsmn`, resulting in improved performance
    ([#882](https://github.com/axoflow/axosyslog/pull/882))

  * New metrics:

    - `syslogng_event_processing_latency_seconds{measurement_point="input/output"}`
      - Histogram of the latency from message receipt to full processing, from the source or destination perspective.
    - `syslogng_output_event_latency_seconds`
      - Histogram of the latency from message receipt to delivery, from the destination perspective.

    `output_event_delay_sample_seconds` has been removed in favor of `output_event_latency_seconds`.
    ([#983](https://github.com/axoflow/axosyslog/pull/983))



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

Akos Zalavary, Andras Mitzki, Attila Szakacs, Balazs Scheidler,
Daniele Ferla, Kevin Mainardis, László Várady, Romain Tartière,
Szilard Parrag, Tamás Kosztyu, shifter
