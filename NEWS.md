4.16.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * New `$PROTO_NAME` macro: add a $PROTO_NAME macro that expands to "tcp" or
    "udp" depending on the transport used by syslog-ng.
    ([#724](https://github.com/axoflow/axosyslog/pull/724))


## Bugfixes

  * Fix memory leaks during configuration parsing
    ([#755](https://github.com/axoflow/axosyslog/pull/755))

  * `grpc` based destinations: Fixed a race condition around `syslogng_output_grpc_requests_total` metrics
    ([#754](https://github.com/axoflow/axosyslog/pull/754))

  * `$PEER_PORT`: fix the value for the PEER_PORT macro, as it was incorrectly
    reversing the digits in the port value, port 514 would become port 415.
    ([#724](https://github.com/axoflow/axosyslog/pull/724))

  * `google-pubsub()`, `logscale()`, `openobserver()`, `splunk()`, and other batching destinations: fix slowdown

    The default value of `batch-timeout()` is now 0.
    This prevents artificial slowdowns in destinations when flow control is enabled
    and the `log-iw-size()` option of sources is set to a value lower than `batch-lines()`.

    If you enable `batch-timeout()`, you can further improve batching performance,
    but you must also adjust the `log-iw-size()` option of your sources accordingly:

    `log-iw-size / max-connections >= batch-lines * workers`
    ([#753](https://github.com/axoflow/axosyslog/pull/753))


## FilterX features

  * `str_strip()`, `str_lstrip()`, `str_rstrip()`: new string transformation functions to remove leading/trailing whitespaces from a string
    ([#745](https://github.com/axoflow/axosyslog/pull/745))


## FilterX bugfixes

  * `in` operator: fix crash, that happened, when the list was declared as an operand

    Example:
    ```
    if ("test" in ["foo", "bar", "test"]) {
      $MSG = "YES";
    };
    ```
    ([#759](https://github.com/axoflow/axosyslog/pull/759))

  * `parse_cef()`, `parse_leef()`: Renamed some parsed header fields.

    **These are breaking changes**

    The motivation behind the renaming is that these names were too
    generic, and there is a chance to find them in the extensions.

    `parse_cef()`:
      * `version` -> `cef_version`
      * `name` -> `event_name`

    `parse_leef()`:
      * `version` -> `leef_version`
      * `vendor` -> `vendor_name`
      * `delimiter` -> `leef_delimiter`
    ([#748](https://github.com/axoflow/axosyslog/pull/748))


## Other changes

  * The base of the AxoSyslog container image is updated to Alpine 3.22
  ([#758](https://github.com/axoflow/axosyslog/pull/758))


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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, László Várady,
Tamás Kosztyu
