4.15.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * `http`: Added templating support to `body-prefix()`

    In case of batching the templates in `body-prefix()` will be calculated
    from the first message. Make sure to use `worker-partition-key()` to
    group similar messages together.

    Literal dollar signs (`$`) used in `body-prefix()` must be escaped like `$$`.

    Example usage:
    ```
    http(
      ...
      body-prefix('{"log_type": "$log_type", "entries": [')
      body('"$MSG"')
      delimiter(",")
      body-suffix("]}")

      batch-lines(1000)
      batch-timeout(1000)
      worker-partition-key("$log_type")
    );
    ```
    ([#731](https://github.com/axoflow/axosyslog/pull/731))

  * `cloud-auth()`: Added `scope()` option for `gcp(service-account())`

    Can be used for authentications using scope instead of audiance.
    For more info, see: https://google.aip.dev/auth/4111#scope-vs-audience

    Example usage:
    ```
    http(
      ...
      cloud-auth(
        gcp(
          service-account(
            key("/path/to/secret.json")
            scope("https://www.googleapis.com/auth/example-scope")
          )
        )
      )
    );
    ```
    ([#738](https://github.com/axoflow/axosyslog/pull/738))

  * affile: Add ability to refine the `wildcard-file()` `filename-pattern()` option with `exclude-pattern()`, to exclude some matching files. For example, match all `*.log` but exclude `*.?.log`.
    ([#719](https://github.com/axoflow/axosyslog/pull/719))

  * bigquery(), google-pubsub-grpc(): add service-account-key option to ADC auth mode

    Example usage:
    ```
    destination {
            google-pubsub-grpc(
                project("test")
                topic("test")
                auth(adc(service-account-key("absolute path to file")))
           );
    };
    ```

    Note: File path must be the absolute path.
    ([#732](https://github.com/axoflow/axosyslog/pull/732))


## Bugfixes

  * gRPC based destinations: Gracefully stop if field name is not valid.
    ([#739](https://github.com/axoflow/axosyslog/pull/739))

  * `clickhouse()`: Fixed setting `UINT8` protobuf type.
    ([#739](https://github.com/axoflow/axosyslog/pull/739))

  * `disk-buffer()` metrics: fix showing used buffers as both active and abandoned
    ([#726](https://github.com/axoflow/axosyslog/pull/726))


## FilterX features

  * Add `str_replace()` function

    For example:
    ```
    filterx {
        dal = "érik a szőlő, hajlik a vessző";
        str_replace(dal, "a", "egy") == "érik egy szőlő, hegyjlik egy vessző";

        dal = "érik a szőlő, hajlik a vessző";
        str_replace(dal, "a", "egy", 1) == "érik egy szőlő, hajlik a vessző";
    };
    ```
    ([#725](https://github.com/axoflow/axosyslog/pull/725))

  * Support string slicing

    For example:
    ```
    filterx {
        str = "example";
        idx = 3;
        str[idx..5] == "mp";
        str[..idx] == "exa";
        str[idx..] == "mple";
    };
    ```
    ([#720](https://github.com/axoflow/axosyslog/pull/720))

  * Null and error-safe dict elements

    For example, the following fields won't be set:
    ```
    $MSG = {
        "nullidontwant":?? null,
        "erroridontwant":?? nonexistingvar,
    };
    ```
    ([#736](https://github.com/axoflow/axosyslog/pull/736))


## FilterX bugfixes

  * `format_xml()`: Fixed an occasionally occurring crash

    In case the input was not a `dict`, a crash could occour during logging the error.
    ([#730](https://github.com/axoflow/axosyslog/pull/730))

  * `parse_windows_eventlog_xml()`: Fixed a `Data` misparsing

    "<Data Name="key" />" is now parsed correctly, identical to "<Data Name="key"></Data>"
    ([#722](https://github.com/axoflow/axosyslog/pull/722))

  * `format_xml()`, `format_windows_eventlog_xml()`: Fixed escaping in element values

    Example:
    ```
    <b> -> &lt;b&gt;
    ```
    ([#743](https://github.com/axoflow/axosyslog/pull/743))


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
Ross Williams, Szilard Parrag, Tamás Kosztyu
