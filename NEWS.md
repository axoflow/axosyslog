4.8.0
=====

We are excited to announce the first [independent release](https://axoflow.com/axosyslog-syslog-ng-fork/) of AxoSyslog.

Explore and learn more about the new features in our [release announcement blog post](https://axoflow.com/axosyslog-release-4-8/).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Highlights

### Send log messages to Elasticsearch data stream
The `elasticsearch-datastream()` destination can be used to feed Elasticsearch [data streams](https://www.elastic.co/guide/en/elasticsearch/reference/current/data-streams.html).

Example config:

```
elasticsearch-datastream(
  url("https://elastic-endpoint:9200/my-data-stream/_bulk")
  user("elastic")
  password("ba3DI8u5qX61We7EP748V8RZ")
);
```
([#178](https://github.com/axoflow/axosyslog/pull/178))

## Features

  * `s3()`: Introduced server side encryption related options

    `server-side-encryption()` and `kms-key()` can be used to configure encryption.

    Currently only `server-side-encryption("aws:kms")` is supported.
    The `kms-key()` should be:
      * an ID of a key
      * an alias of a key, but in that case you have to add the alias/prefix
      * an ARN of a key

    To be able to use the aws:kms encryption the AWS Role or User has to have the following
    permissions on the given key:
      * `kms:Decrypt`
      * `kms:Encrypt`
      * `kms:GenerateDataKey`

    Check [this](https://repost.aws/knowledge-center/s3-large-file-encryption-kms-key) page on why the `kms:Decrypt` is mandatory.

    Example config:
    ```
    destination d_s3 {
      s3(
        bucket("log-archive-bucket")
        object-key("logs/syslog")
        server-side-encryption("aws:kms")
        kms-key("alias/log-archive")
      );
    };
    ```

    See the [S3](https://docs.aws.amazon.com/AmazonS3/latest/userguide/UsingKMSEncryption.html) documentation for more details.
    ([#127](https://github.com/axoflow/axosyslog/pull/127))

  * `opentelemetry()`, `loki()`, `bigquery()` destination: Added `headers()` option

    With this option you can add gRPC headers to each RPC call.

    Example config:
    ```
    opentelemetry(
      ...
      headers(
        "organization" => "Axoflow"
        "stream-name" => "axo-stream"
      )
    );
    ```
    ([#192](https://github.com/axoflow/axosyslog/pull/192))


## Bugfixes

  * `csv-parser()`: fix escape-backslash-with-sequences dialect on ARM
    ([#4947](https://github.com/syslog-ng/syslog-ng/pull/4947))

  * `csv-parser()` produced invalid output on platforms where char is an unsigned type.
    ([#4947](https://github.com/syslog-ng/syslog-ng/pull/4947))

  * `rate-limit()`: Fixed a crash which occured on a config parse failure.
    ([#169](https://github.com/axoflow/axosyslog/pull/169))

  * macros: Fixed a bug which always set certain macros to string type

    The affected macros are `$PROGRAM`, `$HOST` and `$MESSAGE`.
    ([#162](https://github.com/axoflow/axosyslog/pull/162))

  * `wildcard-file()`: fix crash when a deleted file is concurrently written
    ([#160](https://github.com/axoflow/axosyslog/pull/160))

  * `disk-buffer()`: fix crash when pipeline initialization fails

    `log_queue_disk_free_method: assertion failed: (!qdisk_started(self->qdisk))`
    ([#128](https://github.com/axoflow/axosyslog/pull/128))

  * `syslog-ng-ctl query`: fix showing Prometheus metrics as unnamed values

    `none.value=726685`
    ([#129](https://github.com/axoflow/axosyslog/pull/129))

  * `syslog-ng-ctl query`: show timestamps and fix `g_pattern_spec_match_string` assert
    ([#129](https://github.com/axoflow/axosyslog/pull/129))


## Other changes

  * packages/dbld: add support for Ubuntu 24.04 (Noble Numbat)
    ([#4925](https://github.com/syslog-ng/syslog-ng/pull/4925))

  * `syslog-ng-ctl`: do not show orphan metrics for `stats prometheus`

    As the `stats prometheus` command is intended to be used to forward metrics
    to Prometheus or any other time-series database, displaying orphaned metrics
    should be avoided in order not to insert new data points when a given metric
    is no longer alive.

    In case you are interested in the last known value of orphaned counters, use
    the `stats` or `query` subcommands.
    ([#4921](https://github.com/syslog-ng/syslog-ng/pull/4921))

  * `bigquery()`, `loki()`, `opentelemetry()`, `cloud-auth()`: C++ modules can be compiled with clang

    Compiling and using these C++ modules are now easier on FreeBSD and macOS.
    ([#4933](https://github.com/syslog-ng/syslog-ng/pull/4933))

  * `s3()`: new metric `syslogng_output_event_bytes_total`
    ([#4958](https://github.com/syslog-ng/syslog-ng/pull/4958))

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

Arpad Kunszt, Attila Szakacs, Balazs Scheidler, Dmitry Levin,
Ferenc HERNADI, Gabor Kozma, Hofi, Ilya Kheifets, Kristof Gyuracz,
László Várady, Mate Ory, Máté Őry, Robert Fekete, Szilard Parrag,
Wolfram Joost, shifter
