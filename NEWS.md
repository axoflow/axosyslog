4.18.1
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Bugfixes

  * `strftime()` FilterX function: Fixed %Z formatting for some rare cases

    America/Caracas (-04:30) time offset will now be correctly formatted.
    ([#811](https://github.com/axoflow/axosyslog/pull/811))

  * `disk-buffer()`: fix getting stuck under rare circumstances
    ([#813](https://github.com/axoflow/axosyslog/pull/813))

  * `disk-buffer()`: do not allow flow-control misconfiguration
    The `flow-control-window_size()` (formerly `mem-buf-length()`) option is now deprecated and no longer has any
    effect.
    ([#813](https://github.com/axoflow/axosyslog/pull/813))


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
