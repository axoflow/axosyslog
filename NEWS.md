4.17.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * FilterX `dpath()`: new function to set a potentially non-existing path within a dict

    For example,
    ```
    dpath(dict.path.to["complex.n-v"].create) = {...};
    ```
    ([#746](https://github.com/axoflow/axosyslog/pull/746))

  * FilterX string slices: support negative indexing

    For example,
    ```
    filterx {
      str = "example";
      str[..-2] == "examp";
      str[-3..] == "ple";
    };
    ```
    ([#780](https://github.com/axoflow/axosyslog/pull/780))

  * `parallelize()`: add `batch-size()` option and other perf improvements

    The `batch-size()` option specifies, for each input thread, how many consecutive
    messages should be processed by a single `parallelize()` worker.

    This ensures that this many messages preserve their order on the output side
    and also improves `parallelize()` performance. A value around 100 is recommended.
    ([#757](https://github.com/axoflow/axosyslog/pull/757))

  * `clickhouse-destination()`: new `json-var()` directive

    The new `json-var()` option accepts either a JSON template or a variable containing a JSON string, and sends it to the ClickHouse server in Protobuf/JSON mixed mode (JSONEachRow format). In this mode, type validation is performed by the ClickHouse server itself, so no Protobuf schema is required for communication.

    This option is mutually exclusive with `proto-var()`, `server-side-schema()`, `schema()`, and `protobuf-schema()` directives.

    example:
    ```
       destination {
          clickhouse (
          ...
          json-var("$json_data");
    or
          json-var(json("{\"ingest_time\":1755248921000000000, \"body\": \"test template\"}"))
          ...
          };
       };
    ```
    ([#761](https://github.com/axoflow/axosyslog/pull/761))

  * FilterX `parse_kv()`: add `stray_words_append_to_value` flag

    For example,
    ```
    filterx {
      $MSG = parse_kv($MSG, value_separator="=", pair_separator=" ", stray_words_append_to_value=true);
    };

    input: a=b b=c d e f=g
    output: {"a":"b","b":"c d e","f":"g"}
    ```
    ([#770](https://github.com/axoflow/axosyslog/pull/770))


## Bugfixes

  * filterx: fix `startswith()`/`endswith()`/`includes()` functions early free
    ([#772](https://github.com/axoflow/axosyslog/pull/772))

  * `/string/`: fix escaping

    Strings between / characters are now treated literally. The only exception is `\/`, which can be used to represent a `/` character.

    This syntax allows you to construct regular expression patterns without worrying about double escaping.

    Note: Because of the simplified escaping rules, you cannot represent strings that end with a single `\` character,
    but such strings would not be valid regular expression patterns anyway.
    ([#776](https://github.com/axoflow/axosyslog/pull/776))

  * `program()` destination: Fix invalid access of freed log-writer cfg.
    ([#779](https://github.com/axoflow/axosyslog/pull/779))

  * `ebpf()`: acquire CAP_BPF before loading eBPF programs

    Previously, when AxoSyslog was compiled with Linux capabilities enabled,
    the `ebpf()` module was unable to load programs.
    ([#768](https://github.com/axoflow/axosyslog/pull/768))



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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, Ben Ireland,
László Várady, Szilard Parrag, Tamás Kosztyu, shifter
