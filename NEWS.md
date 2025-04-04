4.11.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * `webhook()`: headers support

    `include-request-headers(yes)` stores request headers under the `${webhook.headers}` key,
    allowing further processing, for example, in FilterX:

    ```
    filterx {
      headers = json(${webhook.headers});
      $type = headers["Content-Type"][-1];
    };
    ```

    `proxy-header("x-forwarded-for")` helps retain the sender's original IP and the proxy's IP address
    (`$SOURCEIP`, `$PEERIP`).
    ([#524](https://github.com/axoflow/axosyslog/pull/524))

  * `network()`, `syslog()` sources: add `$PEERIP` and `$PEERPORT` macros

    The `$PEERIP` and `$PEERPORT` macros always display the address and port of the direct sender.
    In most cases, these values are identical to `$SOURCEIP` and `$SOURCEPORT`.
    However, when dealing with proxied protocols, `$PEERIP` and `$PEERPORT` reflect the proxy's address and port,
    while `$SOURCEIP` and `$SOURCEPORT` indicate the original source of the message.
    ([#523](https://github.com/axoflow/axosyslog/pull/523))

  * gRPC based destinations: Added `response-action()` option

    With this option, it is possible to fine tune how AxoSyslog
    behaves in case of different gRPC results.

    Supported by the following destination drivers:
      * `opentelemetry()`
      * `loki()`
      * `bigquery()`
      * `clickhouse()`
      * `google-pubsub-grpc()`

    Supported gRPC results:
      * ok
      * unavailable
      * cancelled
      * deadline-exceeded
      * aborted
      * out-of-range
      * data-loss
      * unknown
      * invalid-argument
      * not-found
      * already-exists
      * permission-denied
      * unauthenticated
      * failed-precondition
      * unimplemented
      * internal
      * resource-exhausted

    Supported actions:
      * disconnect
      * drop
      * retry
      * success

    Usage:
    ```
    google-pubsub-grpc(
      project("my-project")
      topic("my-topic")
      response-action(
        not-found => disconnect
        unavailable => drop
      )
    );
    ```
    ([#561](https://github.com/axoflow/axosyslog/pull/561))


## FilterX features

  * `set_pri()`: Added new filterx function to set the message priority value.

    Example usage:
    ```
    set_pri(pri=100);
    ```

    Note: Second argument must be between 0 and 191 inclusive.
    ([#521](https://github.com/axoflow/axosyslog/pull/521))

  * `set_timestamp()`: Added new filterx function to set the message timestamps.

    Example usage:
    ```
    set_timestamp(datetime, stamp="stamp");
    ```

    Note: Second argument can be "stamp" or "recvd", based on the timestamp to be set.
    Default is "stamp".
    ([#510](https://github.com/axoflow/axosyslog/pull/510))

  * `cache_json_file()`: inotify-based reloading of JSON file
    ([#517](https://github.com/axoflow/axosyslog/pull/517))

## FilterX bugfixes

  * `switch`: Fixed a crash that occurred when the selector or case failed to evaluate.
    ([#527](https://github.com/axoflow/axosyslog/pull/527))


## Notes to developers

  * editorconfig: configure supported editors for the project's style
    ([#550](https://github.com/axoflow/axosyslog/pull/550))

  * We have clarified the meaning of the required "Signed-off-by" line in commit messages
    ([CONTRIBUTING.md](CONTRIBUTING.md))

  * Light, AxoSyslog's lightweight end-to-end testing framework is available as a PyPi package:
    https://pypi.org/project/axosyslog-light/

    It allows you to extend the framework and the test suite out-of-tree (licensed under GPL-2.0-or-later).


## Other changes

  * `azure-monitor()`: unified destination

    The `azure-monitor-builtin()` and `azure-monitor-custom()` destinations are deprecated in favor of `azure-monitor()`.
    `azure-monitor()` requires the `stream-name()` option instead of the table name.
    ([#531](https://github.com/axoflow/axosyslog/pull/531))

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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, David Mandelberg,
Hofi, Janos Szigetvari, László Várady, Szilard Parrag, Tamás Kosztyu,
shifter
