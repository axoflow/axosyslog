4.8.1
=====

This is a bugfix release of AxoSyslog.

AxoSyslog is binary-compatible with syslog-ng [[1]](#r1) and serves as a drop-in replacement.

Explore and learn more about the new features in our [release announcement blog post](https://axoflow.com/axosyslog-release-4-8/).

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/pkgs/container/axosyslog) and Helm charts.

Packages are available for Debian and Ubuntu from our APT repository.
RPM packages are available in the Assets section (we’re working on an RPM repository as well, and hope to have it up and running for the next release).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Bugfixes

  * Fixed crash around wildard `@include` configuration pragmas when compiled with musl libc

    The AxoSyslog container image, for example, was affected by this bug.

    ([#261](https://github.com/axoflow/axosyslog/pull/261))

  * `metrics-probe()`: fix disappearing metrics from `stats prometheus` output

    `metrics-probe()` metrics became orphaned and disappeared from the `syslog-ng-ctl stats prometheus` output
    whenever an ivykis worker stopped (after 10 seconds of inactivity).
    ([#243](https://github.com/axoflow/axosyslog/pull/243))

  * `syslog-ng-ctl`: fix escaping of `stats prometheus`

    Metric labels (for example, the ones produced by `metrics-probe()`) may contain control characters, invalid UTF-8 or `\`
    characters. In those specific rare cases, the escaping of the `stats prometheus` output was incorrect.
    ([#224](https://github.com/axoflow/axosyslog/pull/224))

  * Fixed potential null pointer deref issues

    ([#216](https://github.com/axoflow/axosyslog/pull/216))

## Other changes

  * `tls()`: expose the key fingerprint of the peer in ${.tls.x509_fp} if
    trusted-keys() is used to retain the actual peer identity in received
    messages.
    ([#136](https://github.com/axoflow/axosyslog/pull/136))

  * `network()`, `syslog()` sources and `syslog-parser()`: add `no-piggyback-errors` flag

    With the `no-piggyback-errors` flag of `syslog-parser()`, the message will not be attributed to AxoSyslog in
    case of errors. Actually it retains everything that was present at the time of the parse error,
    potentially things that were already extracted.

    So $MSG remains that was set (potentially the raw message), $HOST may or may not be extracted,
    likewise for $PROGRAM, $PID, $MSGID, etc.

    The error is still indicated via $MSGFORMAT set to "syslog:error".

    ([#245](https://github.com/axoflow/axosyslog/pull/245))

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

Andras Mitzki, Attila Szakacs, Balazs Scheidler, Dmitry Levin,  Hofi,
László Várady, Szilárd Parrag, shifter
