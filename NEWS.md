4.20.1
======

_This is a hotfix release that fixes a rare metric race condition causing a reload time crash._

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.


## Features

  * Added new metrics

    * `syslogng_window_capacity` and `syslogng_window_available` on `stats(level(3))`.
      * Shows the `log-iw-size()` value and the current state of the source window, respectively.
    * `syslogng_window_full_total`
      * Tracks how many times the window was completely full. This counter will
        increase any time the destination causes the source to be throttled.
    * `syslogng_memory_queue_processed_events_total` and `syslogng_disk_queue_processed_events_total`
      * Counts the number of events processed since startup by each queue.
    * `syslogng_output_batch_size_...`, `syslogng_output_event_size_...`, `syslogng_output_request_latency_...`
      * histogram style metrics for `http()`, `otel()` and other threaded destinations

    ([#823](https://github.com/axoflow/axosyslog/pull/823))
    ([#824](https://github.com/axoflow/axosyslog/pull/824))
    ([#845](https://github.com/axoflow/axosyslog/pull/845))

  * `clickhouse()` destination: Added JSONCompactEachRow format and new `format` directive

    This update enhances the ClickHouse destination by adding support for the `JSONCompactEachRow`
    format and introducing a new `format` directive for explicitly selecting the data format.

    **Background**
    Previously, the destination supported:
    - `Protobuf` (default when using `proto-var`)
    - `JSONEachRow` (default when using `json-var`)

    These defaults remain unchanged.

    **What’s new**
    - Added support for `JSONCompactEachRow` — a more compact, array-based JSON representation (used with `json-var`).
    - Introduced the `format` directive, allowing manual selection of the desired format:
      - `JSONEachRow`
      - `JSONCompactEachRow`
      - `Protobuf`

    **Example**
    ```hcl
    destination {
      clickhouse (
        ...
        json-var(json("$my_filterx_json_var"))
        format("JSONCompactEachRow")
        ...
      );
    };
    ```

    **JSONEachRow (each JSON object per line, more readable):**

    ```
    {"id":1,"name":"foo","value":42}
    {"id":2,"name":"bar","value":17}
    ```

    **JSONCompactEachRow (compact array-based row representation):**

    ```
    [1,"foo",42]
    [2,"bar",17]
    ```

    **Validation and error handling**

    **Invalid format values now produce:**
    ```
    Error parsing within destination: invalid format value 'invalid format', possible values:[JSONEachRow, JSONCompactEachRow, Protobuf]
    ```

    **If the data’s actual format doesn’t match the selected format, ClickHouse returns:**
    ```
    CANNOT_PARSE_INPUT_ASSERTION_FAILED
    ```
    ([#828](https://github.com/axoflow/axosyslog/pull/828))

  * `opentelemetry()` source: Added `keep-alive()` option

    With this new option, client connections can be kept alive during reload,
    avoiding unnecessary retry backoffs and other error messages on the client
    side.

    The default is `yes`.

    ([#832](https://github.com/axoflow/axosyslog/pull/832))

  * `s3()` destination: Added new `object_key_suffix()` option

    The default suffix is an empty string, to ensure backward compatibility.
    ([#797](https://github.com/axoflow/axosyslog/pull/797))

  * `http()` and other threaded destinations: add `worker-partition-buckets()` option

    This allows the same `worker-partition-key()` to use multiple worker threads.
    ([#852](https://github.com/axoflow/axosyslog/pull/852))


## Bugfixes

  * `metrics`: Fixed a rare race condition in dynamic metrics.
    ([#858](https://github.com/axoflow/axosyslog/pull/858))

  * `filterx`: Fixed various memory leaks
    ([#829](https://github.com/axoflow/axosyslog/pull/829))
    ([#836](https://github.com/axoflow/axosyslog/pull/836))
    ([#842](https://github.com/axoflow/axosyslog/pull/842))

  * `filterx`: Fixed a variable synchronization bug.
    ([#849](https://github.com/axoflow/axosyslog/pull/849))

  * `filterx` `otel_logrecord()`: Fixed not clearing `body` before setting dict and array values.
    ([#835](https://github.com/axoflow/axosyslog/pull/835))

  * Fixed `keep-alive()` during config reload revert
    ([#831](https://github.com/axoflow/axosyslog/pull/831))

  * `http()` destination: Fixed batch partitioning in case of templated `body-prefix()`
    ([#843](https://github.com/axoflow/axosyslog/pull/843))

  * `s3`: Bugfixes and general stability improvements for the `s3` destination driver

    * Fixed a major bug causing data loss if multithreaded upload was enabled via the `upload-threads` option.
    * Fixed a bug where in certain conditions finished object buffers would fail to upload.
    * Fixed a bug, where empty chunks were being uploaded, causing errors.

    ([#797](https://github.com/axoflow/axosyslog/pull/797))
    ([#846](https://github.com/axoflow/axosyslog/pull/846))

  * `filterx`: fix potential use-after-free crashes
    ([#854](https://github.com/axoflow/axosyslog/pull/854))

  * `disk-buffer()`: fix memory leak when `worker-partition-key()` and `disk-buffer()` are used together
    ([#853](https://github.com/axoflow/axosyslog/pull/853))


## Other changes

  * Improved reload time for large configurations.
    ([#844](https://github.com/axoflow/axosyslog/pull/844))


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
László Várady, Szilard Parrag, Tamás Kosztyu, shifter
