4.28.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Highlights

### HTTP source

The new experimental `ehttp()` source receives log messages over HTTP and HTTPS. It is built on a new
HTTP server framework with HTTP/1.1 keep-alive and pipelining support, HTTP/2 is not supported. The
driver will be renamed to `http()` once its options are stable.

Options:
  * `mode()`: how the request body is split into messages: `single` (the whole body is one message),
    `line-separated` or `jsonl` (one message per line), `json` (a JSON array or concatenated JSON
    objects, one message each) or `auto` (`json` when the body parses as JSON, `line-separated`
    otherwise), defaults to `auto`
  * `auth-token()`: the full value of the `Authorization` header the clients must send, for example
    `"Bearer <token>"`; without it every request is accepted
  * `response-body()`: a template for the response body, defaults to a plain `200 OK`
  * `max-request-size()`: the largest request body accepted, applies after decompression
  * `max-connections()`: defaults to `100`
  * the network and TLS options of the `network()` source, for example `ip()`, `port()`, `transport()`
    and `tls()`

Requests compressed with `gzip` or `deflate` are decompressed by their `Content-Encoding` header.

Example configuration:

```
source s_http {
  ehttp(
    port(8080)
    mode("json")
    auth-token("Bearer s3cr3t")
    response-body('{"status": "received"}')
    flags(no-parse)
  );
};
```
([#1184](https://github.com/axoflow/axosyslog/pull/1184),
[#1194](https://github.com/axoflow/axosyslog/pull/1194),
[#1221](https://github.com/axoflow/axosyslog/pull/1221))

### Splunk HEC source

The new `splunk-hec()` source receives events from Splunk HTTP Event Collector clients, so forwarders and
applications that log to Splunk can send their events to AxoSyslog by changing the HEC URL. The `event`
field becomes `$MESSAGE`, `host` and `time` set `$HOST` and the timestamp, and the whole HEC envelope is
available under `${.splunk.*}`. Bodies that are not JSON are parsed as syslog.

Options:
  * `token()`: the HEC token the clients must send
  * `port()`: defaults to `8088`
  * `prefix()`: the name-value prefix of the HEC fields, defaults to `.splunk.`
  * the options of `ehttp()`, for example `transport()` and `tls()`

Example configuration:

```
source s_hec {
  splunk-hec(
    token("a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d")
    transport(tls)
    tls(key-file("/etc/syslog-ng/hec.key") cert-file("/etc/syslog-ng/hec.crt"))
  );
};
```
([#1194](https://github.com/axoflow/axosyslog/pull/1194))

### Elasticsearch Bulk API source

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

## Features

  * `update_metric()` FilterX function: Added a new `set` parameter, which assigns an absolute value
    to the metric instead of incrementing it, making the function usable for
    gauge-like metrics.

        update_metric("demo_gauge", set=int($MSG), labels={"host": "demo"});

    `set` and `increment` are mutually exclusive, specifying both is a configuration
    error. The value must be non-negative. Negative values are rejected at evaluation
    time and the metric is left unchanged.
    ([#1227](https://github.com/axoflow/axosyslog/pull/1227))

  * `tuple()` unpacking in FilterX: Added Python-like tuple unpacking, e.g. in assignments.

         (a, b, c) = (1, 2, 3)
    ([#1209](https://github.com/axoflow/axosyslog/pull/1209))

  * `format_kv` FilterX function: Added `quote_char` and `always_quote` options.
    ([#1208](https://github.com/axoflow/axosyslog/pull/1208))

  * `opentelemetry()` source: Added `mode()` option.

    * `mode(logmessage)`: The old behavior, creating `${.otel_raw.<...>}` NVs.
    * `mode(filterx-dict)`: Creates declared `resource`, `scope` and `log`
      FilterX variables, each holding a FilterX dict.

    `mode(filterx-dict)` skips the serialization and deserialization
    of the `${.otel_raw.<...>}` NVs, which improves performance.
    ([#1032](https://github.com/axoflow/axosyslog/pull/1032))

  * `opentelemetry()` source: Set the `.tls.x509_cn`, `.tls.x509_o` and
    `.tls.x509_ou` name-value pairs from the client certificate, the same way the
    `network()` and `syslog()` sources do it.

    The values are set when the client sends a certificate, which requires
    `auth(tls(peer-verify()))` to be set to something other than
    `optional-untrusted`.
    ([#1196](https://github.com/axoflow/axosyslog/pull/1196))

  * `syslog-ng-ctl stop --force`: Added a forced variant of the stop command, which terminates the process
    without waiting for the worker threads and without tearing down the configuration.

    Use it when a shutdown never completes because a worker never finishes its job, in which case both
    `syslog-ng-ctl stop` and `syslog-ng-ctl reload` are ignored. In-memory queue contents are lost, disk-buffer
    contents stay on disk. The process exits with status 1.
    ([#1249](https://github.com/axoflow/axosyslog/pull/1249))


## Bugfixes

  * `opentelemetry()` source: Fixed `$SOURCEIP` for IPv6 peers.

    IPv6 clients showed up with the `127.0.0.1` fallback address.
    ([#1032](https://github.com/axoflow/axosyslog/pull/1032))

  * `opentelemetry()`, `clickhouse()`, `loki()`, `bigquery()`, `pubsub()` destinations: Fixed a crash
    in the container image when the destination's host name does not resolve.
    ([#1233](https://github.com/axoflow/axosyslog/pull/1233))

  * `opentelemetry()` destination: Fixed a crash when a batch held more than one signal type, for example a log and a
    metric.
    ([#1224](https://github.com/axoflow/axosyslog/pull/1224))

  * `opentelemetry()` source: Fixed frozen local timestamps.
    ([#1241](https://github.com/axoflow/axosyslog/pull/1241))

  * `opentelemetry()` source: Fixed a crash on configuration reload.
    ([#1232](https://github.com/axoflow/axosyslog/pull/1232))

  * `strftime()` FilterX function: Fixed the `%s` format specifier when the datetime's timezone
    differs from the local timezone.
    ([#1245](https://github.com/axoflow/axosyslog/pull/1245))

  * `strptime()` FilterX function, `date-parser()`: Fixed a field leak between the formats of a multi-format parse.

    The result kept fields, for example the year, from an earlier format that did not match.
    ([#1229](https://github.com/axoflow/axosyslog/pull/1229))

  * `kafka()`: Fixed an `rd_kafka_conf_t` leak when the client could not be constructed during startup.
    ([#1235](https://github.com/axoflow/axosyslog/pull/1235))

  * `s3`: Fixed an `AssertionError` when the flush timer finished an object during a write.
    ([#1228](https://github.com/axoflow/axosyslog/pull/1228))

  * `s3`: Fixed messages being written to an object that was already finished.

    Such messages were never uploaded and the destination logged "Part uploads still pending" in a loop
    until shutdown.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: Fixed message loss on a failed part upload by retrying it instead of dropping the data.

    A part upload that failed with any client error previously discarded the buffered messages of the
    failing part. It now follows the same policy as the `http()` destination: only responses that can never
    succeed as-is are dropped, every other error including access denied and throttling keeps the buffered
    data and the upload is retried a limited number of times, so a transient outage or a fixable
    misconfiguration no longer loses messages.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: Fixed the `canned-acl()` option being rejected by the S3 API.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: Fixed duplicate multipart uploads being created for the same object.

    They left orphaned uploads in the bucket and could fail the object with an invalid part error.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: Fixed part uploads not being retried on every network error.

    A timeout or a closed connection during a part upload left the part on disk until the next start and
    crashed the destination on shutdown.
    ([#1228](https://github.com/axoflow/axosyslog/pull/1228))

  * `s3`: Fixed a crash on shutdown when a multipart upload could not be completed.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: Fixed crashes and a race between message delivery and the periodic flush.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `s3`: Fixed a hang on reload and shutdown after a failed part upload.
    ([#1211](https://github.com/axoflow/axosyslog/pull/1211))

  * `dict_to_pairs()` FilterX function: Fixed dropped writes into the values of the returned pairs.
    ([#1253](https://github.com/axoflow/axosyslog/pull/1253))

  * `parse_xml()`, `parse_windows_eventlog_xml()` FilterX functions: Fixed dropped writes into nested elements.
    ([#1253](https://github.com/axoflow/axosyslog/pull/1253))

  * `parse_cef()`, `parse_leef()` FilterX functions: Fixed dropped writes into the `extensions` dict.
    ([#1253](https://github.com/axoflow/axosyslog/pull/1253))

  * `filterx`: Fixed a lost write into a dict literal that has a runtime member.

    `d = {"user": {"name": v}}; d.user.name = "X";` kept the old value with no error.
    Subscript writes, `dpath()` targets and `declare`d variables were affected the same way.
    ([#1246](https://github.com/axoflow/axosyslog/pull/1246))

  * `filterx`: Fixed writes into a dict value that two keys refer to, for example after `d.b = d.a`.

    Such a write could produce wrong values, and debug builds failed an assertion.
    ([#1215](https://github.com/axoflow/axosyslog/pull/1215))

  * `filterx`: Fixed a crash when `isset()` was called on a repeated field of an OpenTelemetry object, for example
    `isset(log.attributes)` on an `otel_logrecord()`. These fields are always defined by the protocol, so `isset()`
    returns true for them.
    ([#1252](https://github.com/axoflow/axosyslog/pull/1252))

  * `parse_windows_eventlog_xml()` FilterX function: Fixed an abort when `EventID` is the first child of `System`.
    ([#1254](https://github.com/axoflow/axosyslog/pull/1254))

  * `sql()`: Fixed a small memory leak that happened once for every configured destination.
    ([#1204](https://github.com/axoflow/axosyslog/pull/1204))

  * `hook-commands()`, `sql()`, `file()`: Fixed small memory leaks of the `startup()`, `shutdown()`, `quote-char()` and
    `symlink-as()` option values.
    ([#1236](https://github.com/axoflow/axosyslog/pull/1236))

  * `type(glob)`: Fixed a `GLib-CRITICAL` assertion warning that was printed at
    startup for every glob matcher.
    ([#1214](https://github.com/axoflow/axosyslog/pull/1214))

  * `wildcard-file()`: Fixed a race where a file created while the source was starting to watch its directory could be
    missed until the next restart.
    ([#1204](https://github.com/axoflow/axosyslog/pull/1204))

  * `example-msg-generator()`: Fixed generating fewer messages than `num()` when the window was full.
    ([#1239](https://github.com/axoflow/axosyslog/pull/1239))


## Packaging

  * `sql()` destination: Added the `axosyslog-sql` package to the AlmaLinux 9 packages.

    EPEL 9 ships libdbi and its `libdbi-dbd-*` drivers, so the module is available on EL 9.
    ([#1230](https://github.com/axoflow/axosyslog/pull/1230))

  * Debian packages: Dropped the Debian 11 (bullseye) packages.
    ([#1247](https://github.com/axoflow/axosyslog/pull/1247))

  * FilterX JIT: Added the `--with-llvm-linking=shared|static` configure option to link the JIT against the
    shared `libLLVM` or the static LLVM archives, the default is what `llvm-config` reports.
    ([#1242](https://github.com/axoflow/axosyslog/pull/1242))



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
