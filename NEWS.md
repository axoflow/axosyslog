4.19.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * `dict_to_pairs()` FilterX function: Added a new function to convert dicts to list of pairs

    Example usage:
    ```
    dict = {
        "value_1": "foo",
        "value_2": "bar",
        "value_3": ["baz", "bax"],
    };

    list = dict_to_pairs(dict, "key", "value");
    # Becomes:
    # [
    #   {"key":"value_1","value":"foo"},
    #   {"key":"value_2","value":"bar"},
    #   {"key":"value_3","value":["baz","bax"]}
    # ]
    ```
    ([#810](https://github.com/axoflow/axosyslog/pull/810))


## Bugfixes

  * `syslogng_output_unreachable` metric: fix marking destinations unreachable during reload
    ([#818](https://github.com/axoflow/axosyslog/pull/818))

  * `transport(proxied-tcp)`: Fix a HAProxy protocol v2 parsing issue that
    caused a failed assertion.  This essentially triggers a crash with a SIGABRT
    whenever a "LOCAL" command was sent in the HAProxy header without address
    information.
    ([#814](https://github.com/axoflow/axosyslog/pull/814))

  * filterx: fix `parse_csv` function crash if coulumns specified non-existent variable
    ([#819](https://github.com/axoflow/axosyslog/pull/819))

  * `opentelemetry()` source: fix various crashes during startup/reload
    ([#822](https://github.com/axoflow/axosyslog/pull/822))


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
Szilard Parrag, Tamás Kosztyu, shifter
