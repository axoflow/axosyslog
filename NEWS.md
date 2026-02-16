4.23.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * Memory queues: Reduced the amount of memory we use to store queued messages.

    Savings can be as much as 50%, or 8GB less memory use to represent
    1M messages (e.g. 8GB instead of 15GB, as measured by RSS).
    ([#891](https://github.com/axoflow/axosyslog/pull/891))

  * `network()`/`syslog()`: Added `extended-key-usage-verify(yes)` for TLS sources/destinations.
    ([#907](https://github.com/axoflow/axosyslog/pull/907))

  * FilterX: Added a new function called `move()`.

    `move()` tells FilterX that the variable/expression specified as argument
    can be moved to its new location, instead of copying it.
    While FilterX optimizes most copies using its copy-on-write mechanism,
    some cases can be faster by telling it that the old location is not needed
    anymore. `move()` is equivalent to an `unset()` but is more explicit and
    returns the moved value, unlike `unset()`.
    ([#876](https://github.com/axoflow/axosyslog/pull/876))

  * FilterX `format_isodate()` function: Added new function for datetime formatting.
    ([#922](https://github.com/axoflow/axosyslog/pull/922))

  * http: Added `force-content-compression()` option.

    Usage:

    ```
    destination {
      http(
        url("http://example.com/endpoint")
        content-compression("gzip")
        force-content-compression(yes)
      );
    }
    ```
    ([#916](https://github.com/axoflow/axosyslog/pull/916))

  * `opentelemetry()` source: Added `ip()` option to specify bind address.
    ([#949](https://github.com/axoflow/axosyslog/pull/949))


## Bugfixes

  * FilterX `dict` and `list`: Fixed a potential crash when recursively inserting
    a `dict` or `list` instance into itself.
    ([#891](https://github.com/axoflow/axosyslog/pull/891))

  * FilterX `parse_kv()` function: Fixed improperly quoted key-value pair overwriting previous entry.
    ([#921](https://github.com/axoflow/axosyslog/pull/921))

  * `json-parser()`: Fixed parsing JSON array of string with comma.
    ([#923](https://github.com/axoflow/axosyslog/pull/923))

  * `metrics`: Made message memory usage metrics more accurate.

    AxoSyslog keeps track of memory usage by messages both globally and on
    a per queue basis. The accounting behind those metrics were inaccurate,
    the value shown being smaller than the actual memory use.
    These inaccuracies were fixed.
    ([#889](https://github.com/axoflow/axosyslog/pull/889))

  * `internal()` source: Fixed message loss during reload.
    ([#944](https://github.com/axoflow/axosyslog/pull/944))

  * `network()`/`syslog()`: Fix performance degradation around `dynamic-window-size()` when senders disconnect early.
    ([#937](https://github.com/axoflow/axosyslog/pull/937))

  * `network()`/`syslog()` sources: Fix a `dynamic-window()` crash on client disconnect while messages are pending.
    ([#931](https://github.com/axoflow/axosyslog/pull/931))

  * `network()`/`syslog()`: Fixed setting TLS-related macros, like `${.tls.x509_cn}`, for the first message.
    ([#911](https://github.com/axoflow/axosyslog/pull/911))

  * `config:` Fixed a bug where `@define`s and environment variables were not substituted in SCL arguments
    in case those arguments were spread across multiple lines.
    ([#920](https://github.com/axoflow/axosyslog/pull/920))

  * `parallelize()`: Fix leaking messages and losing input window during reload.
    ([#962](https://github.com/axoflow/axosyslog/pull/962))



## Other changes

  * `http()`: Added debug and notice level request logging for failed requests.
    ([#928](https://github.com/axoflow/axosyslog/pull/928))

  * FilterX `repr()` function: datetime representation now has microsecond resolution.

    Before:
    `2000-01-01T00:00:00.000+00:00`

    After:
    `2000-01-01T00:00:00.000000+00:00`
    ([#919](https://github.com/axoflow/axosyslog/pull/919))

  * Batching destinations: Added `syslogng_output_batch_timedout_total` metric.
    ([#947](https://github.com/axoflow/axosyslog/pull/947))

  * `parallelize()`: Added `syslogng_parallelize_failed_events_total` metric.
    ([#924](https://github.com/axoflow/axosyslog/pull/924))

  * `metrics`: Added a single `syslogng_input_window_full_total` on `stats(level(1))`

    It shows the total number of input window full events for the whole config.
    ([#938](https://github.com/axoflow/axosyslog/pull/938))

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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, Fᴀʙɪᴇɴ Wᴇʀɴʟɪ, Hofi,
László Várady, Peter Czanik (CzP), Romain Tartière, Szilard Parrag
