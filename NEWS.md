4.28.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Highlights

<Fill this block manually from the blocks below>

## Features

  * `elasticsearch-bulk()`: Added a new source that implements the Elasticsearch Bulk API

    Elastic Agent, Beats and other clients of the
    [Elasticsearch Bulk API](https://www.elastic.co/docs/api/doc/elasticsearch/operation/operation-bulk)
    can send their events to AxoSyslog by pointing their Elasticsearch output at this source. The document
    of each index and create action becomes a log message and its action line is stored in `${.es_bulk.action}`.
    Update and delete actions are acknowledged and skipped.

    Options:
      * `auth-token()`: the full value of the `Authorization` header the clients send, for example `"ApiKey <key>"`
      * `version()`: the Elasticsearch version reported to the clients, defaults to `8.15.0`; Elastic clients refuse
        a version older than their own
      * the network and TLS options of `ehttp()`

    The source answers the version and license probes of the Elastic clients and accepts gzip and deflate
    compressed requests.

    Example configuration:

    ```
    source s_es {
      elasticsearch-bulk(
        port(9200)
        auth-token("ApiKey <key>")
        version("8.15.0")
      );
    };
    ```
    ([#1223](https://github.com/axoflow/axosyslog/pull/1223))

  * `update_metric()`: added a new `set` parameter, which assigns an absolute value
    to the metric instead of incrementing it, making the function usable for
    gauge-like metrics:

        update_metric("demo_gauge", set=int($MSG), labels={"host": "demo"});

    `set` and `increment` are mutually exclusive, specifying both is a configuration
    error. The value must be non-negative. Negative values are rejected at evaluation
    time and the metric is left unchanged.
    ([#1224](https://github.com/axoflow/axosyslog/pull/1224))

  * `opentelemetry()` source: Added `mode()` option.

    * `mode(logmessage)`: The old behavior, creating `${.otel_raw.<...>}` NVs.
    * `mode(filterx-dict)`: Creates declared `resource`, `scope` and `log`
      FilterX variables, each holding a FilterX dict.

    `mode(filterx-dict)` skips the serialization and deserialization
    of the `${.otel_raw.<...>}` NVs, which improves performance.
    ([#1032](https://github.com/axoflow/axosyslog/pull/1032))

  * `tuple()` unpacking in filterx: added Python-like tuple unpacking, e.g.
    assignments like this:

         (a, b, c) = (1, 2, 3)
    ([#1209](https://github.com/axoflow/axosyslog/pull/1209))

  * `syslog-ng-ctl stop --force`: added a forced variant of the stop command, which terminates the process
    without waiting for the worker threads and without tearing down the configuration.

    Use it when a shutdown never completes because a worker never finishes its job, in which case both
    `syslog-ng-ctl stop` and `syslog-ng-ctl reload` are ignored. In-memory queue contents are lost, disk-buffer
    contents stay on disk. The process exits with status 1.
    ([#1249](https://github.com/axoflow/axosyslog/pull/1249))

  * `opentelemetry()` source: set the `.tls.x509_cn`, `.tls.x509_o` and
    `.tls.x509_ou` name-value pairs from the client certificate, the same way the
    `network()` and `syslog()` sources do it.

    The values are set when the client sends a certificate, which requires
    `auth(tls(peer-verify()))` to be set to something other than
    `optional-untrusted`.
    ([#1196](https://github.com/axoflow/axosyslog/pull/1196))

  * `format_kv` FilterX function: add `quote_char` and `always_quote` options
    ([#1208](https://github.com/axoflow/axosyslog/pull/1208))


## Bugfixes

  * `opentelemetry()` source: Fixed `$SOURCEIP` for IPv6 peers.

    Newer gRPC versions percent-encode the peer address, which broke
    the source address extraction, so IPv6 clients showed up with the
    `127.0.0.1` fallback address.
    ([#1032](https://github.com/axoflow/axosyslog/pull/1032))

  * `opentelemetry()`, `clickhouse()`, `loki()`, `bigquery()`, `pubsub()` destinations: Fixed a crash
    in the container image when the destination's host name does not resolve.

    c-ares 1.34.8 can lose the DNS query that follows a search domain step, so the lookup never
    completes and gRPC aborts the process when it gives up on it. The image now ships c-ares with the
    upstream fix applied.
    ([#1233](https://github.com/axoflow/axosyslog/pull/1233))

  * `opentelemetry()` destination: Fixed a crash when a batch held more than one signal type, for example a log and a
    metric. The batch reused one gRPC client context for its logs, metrics and traces requests, which gRPC does not
    allow, so the process aborted on the second request.
    ([#1224](https://github.com/axoflow/axosyslog/pull/1224))

  * `strftime()`: Fixed the `%s` format specifier when the datetime's timezone
    differs from the local timezone.
    ([#1245](https://github.com/axoflow/axosyslog/pull/1245))

  * `kafka()`: fixed an `rd_kafka_conf_t` leak when the client could not be constructed during startup.
    ([#1235](https://github.com/axoflow/axosyslog/pull/1235))

  * `opentelemetry()` source: fix frozen local timestamps
    ([#1241](https://github.com/axoflow/axosyslog/pull/1241))

  * `opentelemetry()` source: Fixed a crash on configuration reload.
    ([#1232](https://github.com/axoflow/axosyslog/pull/1232))

  * `s3`: fixed an `AssertionError` when the flush timer finished an object during a write

    The flush timer could close the chunk while a message was being written to it. The write then
    failed with an assertion that was logged as an unhandled exception and the message was retried.
    ([#1228](https://github.com/axoflow/axosyslog/pull/1228))

  * `s3`: fixed messages being written to an object that was already finished

    When the periodic flush finished an object right after it had closed a chunk, the object stayed
    writable. The next message opened a new part on it that was never uploaded, and the destination
    logged "Part uploads still pending" in a loop until shutdown. Such a message is now retried on a new
    object instead.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `dict_to_pairs()`: fixed dropped writes into the values of the returned pairs
    ([#1253](https://github.com/axoflow/axosyslog/pull/1253))

  * `s3`: retry failed part uploads instead of dropping the data

    A part upload that failed with any client error previously discarded the buffered messages of the
    failing part. It now follows the same policy as the `http()` destination: only responses that can never
    succeed as-is are dropped, every other error including access denied and throttling keeps the buffered
    data and the upload is retried a limited number of times, so a transient outage or a fixable
    misconfiguration no longer loses messages.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: fixed the `canned-acl()` option being rejected by the S3 API

    The configured value was passed to the S3 API as a tuple instead of a string, which raised an unhandled
    exception whenever `canned-acl()` was set.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: avoid creating duplicate multipart uploads for the same object

    When several parts of a freshly opened object were uploaded concurrently, each upload could start its
    own multipart upload. This left an orphaned upload in the bucket and could fail the object with an
    invalid part error. Multipart upload creation is now serialized.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `parse_xml()`, `parse_windows_eventlog_xml()`: fixed dropped writes into nested elements
    ([#1253](https://github.com/axoflow/axosyslog/pull/1253))

  * `parse_cef()`, `parse_leef()`: fixed dropped writes into the `extensions` dict
    ([#1253](https://github.com/axoflow/axosyslog/pull/1253))

  * `filterx`: Fixed a lost write into a dict literal that has a runtime member.

    `d = {"user": {"name": v}}; d.user.name = "X";` kept the old value with no error.
    Subscript writes, `dpath()` targets and `declare`d variables were affected the same way.
    ([#1246](https://github.com/axoflow/axosyslog/pull/1246))

  * `s3`: retry part uploads on every network error

    Only a refused connection was retried. A timeout or a closed connection during a part upload was
    treated as an unexpected error, which left the part on disk until the next start and crashed the
    destination on shutdown.
    ([#1228](https://github.com/axoflow/axosyslog/pull/1228))

  * `sql()`: Fixed a small memory leak that happened once for every configured destination.
    ([#1204](https://github.com/axoflow/axosyslog/pull/1204))

  * `type(glob)`: Fixed a `GLib-CRITICAL` assertion warning that was printed at
    startup for every glob matcher.
    ([#1214](https://github.com/axoflow/axosyslog/pull/1214))

  * `s3`: fixed a crash on shutdown when a multipart upload could not be completed

    The destination crashed with an `AssertionError` when finishing an S3 object whose multipart upload had
    been started but had no successfully uploaded parts, for example when the first part upload failed with a
    client error. Such an upload is now aborted instead of triggering the assertion.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `hook-commands()`, `sql()`, `file()`: fixed small memory leaks of the `startup()`, `shutdown()`, `quote-char()` and
    `symlink-as()` option values.
    ([#1236](https://github.com/axoflow/axosyslog/pull/1236))

  * `filterx`: Fixed a crash when `isset()` was called on a repeated field of an OpenTelemetry object, for example
    `isset(log.attributes)` on an `otel_logrecord()`. These fields are always defined by the protocol, so `isset()`
    returns true for them.
    ([#1252](https://github.com/axoflow/axosyslog/pull/1252))

  * `wildcard-file()`: Fixed a race where a file created while the source was starting to watch its directory could be
    missed until the next restart.
    ([#1204](https://github.com/axoflow/axosyslog/pull/1204))

  * `strptime()`, `date-parser()`: Fixed a field leak between the formats of a multi-format parse.

    A format that failed midway left the fields it already parsed (for example the year) behind,
    and a later matching format that does not name those fields kept them in the result.
    ([#1229](https://github.com/axoflow/axosyslog/pull/1229))

  * `s3`: fixed crashes and a race between message delivery and the periodic flush

    Delivering a message to an object that the flush timer finished at the same moment raised an unhandled
    exception, and the timer and the delivery path accessed the shared object bookkeeping without a lock,
    which could abort the flush timer with a dictionary iteration error. The two paths are now
    synchronized, the flush timer no longer runs after shutdown and a message racing with a flush is
    retried instead of crashing.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: fixed a hang on reload and shutdown after a failed part upload

    A part upload that had to be retried was queued behind the task that completes the multipart upload,
    and that task waited for the part uploads to finish while occupying one of the `upload-threads()`.
    Once every thread waited this way, no retry could run, the affected objects were never uploaded and
    `syslog-ng` never finished its reload or shutdown. The multipart upload is now completed by the last
    part upload that finishes, so no upload thread waits for another one.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))


## Packaging

  * `sql()` destination: The AlmaLinux 9 packages now include it in the `axosyslog-sql` package.

    EPEL 9 ships libdbi and its `libdbi-dbd-*` drivers, so the module is available on EL 9.
    ([#1230](https://github.com/axoflow/axosyslog/pull/1230))



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

Andras Mitzki, Antal Nemes, Attila Szakacs-Bertok, Balazs Scheidler,
Balint Ferencz, Hofi, Josef Schlehofer, László Várady, Szilard Parrag
