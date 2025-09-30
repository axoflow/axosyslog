4.18.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * `http()`: Added support for templated `headers()`

    In case of batching the templates in `headers()` will be calculated
    from the first message. Make sure to use `worker-partition-key()` to
    group similar messages together.

    Literal dollar signs (`$`) used in `headers()` must be escaped like `$$`.
    ([#794](https://github.com/axoflow/axosyslog/pull/794))

  * FilterX: unary `+` and `-` operators

    Useful for dynamic string slicing, for example:
    ```
    str[..-tempvar]
    ```
    ([#788](https://github.com/axoflow/axosyslog/pull/788))

  * FilterX `parse_csv()`: add `quote_pairs` parameter

    For example:

    ```
    filterx {
      str = "sarga,[bogre],'gorbe'";
      $MSG = parse_csv(str, quote_pairs=["[]", "'"]);
    };
    ```
    ([#804](https://github.com/axoflow/axosyslog/pull/804))


## Bugfixes

  * FilterX `in` operator: fix crash when left- or right-hand side operand evaluation fails
    ([#798](https://github.com/axoflow/axosyslog/pull/798))

  * `in` FilterX operator: Fixed finding `message_value`s in arrays.
    ([#791](https://github.com/axoflow/axosyslog/pull/791))

  * `python()`: `LogTemplate::format()` now returns a `bytes` object

    In the Python bindings, `LogMessage` is not UTF-8 or Unicode–safe by default.
    This means developers must explicitly call `decode()` on message fields and handle any decoding errors themselves.

    Since `LogTemplate` operates on `LogMessage` fields, this behavior also applies to it.

    *Breaking change*:
    When using templates, you now need to decode the result manually. For example:
    `format().decode("utf-8", errors="backslashreplace")`
    ([#799](https://github.com/axoflow/axosyslog/pull/799))

  * `in` FilterX operator: Fixed possible memory corruption regarding unreferencing an operand.
    ([#792](https://github.com/axoflow/axosyslog/pull/792))



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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, Hofi, László Várady,
Szilard Parrag, Tamás Kosztyu, shifter
