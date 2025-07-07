4.13.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## License

The licensing of AxoSyslog has been simplified and upgraded from a combination of `LGPL-2.1-or-later` and
`GPL-2.0-or-later` to `GPL-3.0-or-later`.

As before, Contributory Licensing Agreements (CLAs) are NOT required to contribute to AxoSyslog: contributors retain
their own copyright, making AxoSyslog a combination of code from hundreds of individuals and companies.
This, and the use of GPL v3 ensures that AxoSyslog or AxoSyslog derived code cannot become proprietary software.

While this has basically no impact on users of AxoSyslog, it reflects a step towards a more open and more
community-friendly project. [Read more here](https://axoflow.com/blog/axosyslog-syslog-ng-fork-license-change-gpl3)

## FilterX features

  * `format_xml()` and `format_windows_eventlog_xml()`: new functions added

    Example usage:
    ```
    $MSG = format_xml({"a":{"b":"foo"}});
    ```
    ([#684](https://github.com/axoflow/axosyslog/pull/684))

  * `protobuf_message()`: Added a new function to create arbitrary protobuf data

    Usage:
    ```
    protobuf_data = protobuf_message(my_dict, schema_file="my_schema_file.proto");
    ```
    ([#678](https://github.com/axoflow/axosyslog/pull/678))

  * `clickhouse()`, `bigquery()` destination: Added `proto-var()` option

    This option can be used to send a FilterX prepared protobuf payload.
    ([#678](https://github.com/axoflow/axosyslog/pull/678))

  * `format_cef()`, `format_leef()`: Added new functions for CEF and LEEF formatting
    ([#690](https://github.com/axoflow/axosyslog/pull/690))

  * `parse_cef()`, `parse_leef()`: Extensions are no longer put under the `extensions` inner dict

    By default now they get placed on the same level as the headers.
    The new `separate_extensions=true` argument can be used for the
    old behavior.
    ([#690](https://github.com/axoflow/axosyslog/pull/690))


## FilterX bugfixes

  * Fixed some FilterX evaluation error messages being printed to stderr.
    ([#654](https://github.com/axoflow/axosyslog/pull/654))

  * `parse_cef()`, `parse_leef()`: Fixed some failed parsing around escaped delimiters
    ([#699](https://github.com/axoflow/axosyslog/pull/699))




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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, László Várady, Szilard Parrag,
Tamás Kosztyu, shifter
