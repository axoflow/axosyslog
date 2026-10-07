4.29.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Features

  * `disk-buffer()`: `capacity-bytes()` can now be changed for existing disk-buffer files.

    The new value takes effect on the next reload or restart, or later, once the disk-buffer content allows it.
    ([#1272](https://github.com/axoflow/axosyslog/pull/1272))

  * `syslog-ng --list-paths`: print the files and directories that the configuration refers to, without starting syslog-ng.
    ([#1290](https://github.com/axoflow/axosyslog/pull/1290))

  * `cloud-auth()`: Added `auth_url()` to `azure(monitor())`, which names the login host the access token is
    requested from.

    The token endpoint was fixed at `https://login.microsoftonline.com`, so only the public cloud could be reached.
    ([#1277](https://github.com/axoflow/axosyslog/pull/1277))


## Bugfixes

  * `filterx`: Fixed a crash on startup when `includes()`, `startswith()` or `endswith()` was called with a literal
    first argument and a second argument referring to a variable.
    ([#1257](https://github.com/axoflow/axosyslog/pull/1257))

  * `disk-buffer()`: Fixed a crash when a destination kept retrying the same message from a non-reliable disk-buffer.

    Every retry allocated a new queue node for the message, and after 65535 retries AxoSyslog aborted with
    `log_msg_alloc_queue_node: assertion failed: (cur_node <= LOGMSG_NODES_ASSERT)`.
    ([#1275](https://github.com/axoflow/axosyslog/pull/1275))

  * `opentelemetry()`: fixed the text form of a large integral double in `mode(filterx-dict)`. An OTLP timestamp in ticks
    or in nanoseconds, sent as a double, printed in scientific notation, for example `6.3923074857349325e+17`. It now prints
    as `639230748573493248.0`.
    ([#1270](https://github.com/axoflow/axosyslog/pull/1270))

  * `format_windows_eventlog_xml()`: fixed the closing tags of an `EventData` element that directly follows an
    attribute of its parent element.
    ([#1269](https://github.com/axoflow/axosyslog/pull/1269))

  * `filterx`: Fixed a crash on startup in debug builds when `strcasecmp()` was called with two literal
    arguments.
    ([#1257](https://github.com/axoflow/axosyslog/pull/1257))

  * `format_windows_eventlog_xml()`, `format_xml()`: fixed the closing tags of an element with attributes whose child
    elements are all text-only.
    ([#1265](https://github.com/axoflow/axosyslog/pull/1265))

  * `http()` destination: Fixed templated `headers()` when batching is enabled.

    Request headers were formatted from the first message of the batch, so a batch that mixed messages with different
    header values was sent with the values of the first message.

    Batching with a templated `headers()` now requires `worker-partition-key()` and flushes the batch when the key
    changes, the same as a templated `url()` or `body-prefix()`.
    ([#1258](https://github.com/axoflow/axosyslog/pull/1258))

  * `http()`: relaxed the `worker-partition-key()` requirement for message-independent URL templates

    The partition-key check previously fired for any `url()` containing a `$` sign, even when the template was
    effectively constant (for example `$(url-encode literal)` after SCL backtick substitution). The check now walks the
    compiled template and only requires `worker-partition-key()` if the `url()` or `body-prefix()` is genuinely
    message-dependent.
    ([#1234](https://github.com/axoflow/axosyslog/pull/1234))

## Notes to developers

  * `logproto`: replaced the handshake phase of `LogProtoServer` with a proto replacement from `fetch()`

    A server proto that wants to hand its work over to another one (transport
    auto-detection is the only such case today) now sets the new
    `proto_replacement` member during `fetch()` and returns `LPS_AGAIN` without
    a message.

    The previous half-broken LogProtoClient/LogProtoServer handshake()
    mechanisms are now gone.
    ([#1231](https://github.com/axoflow/axosyslog/pull/1231))



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

Andras Mitzki, Attila Szakacs-Bertok, Balazs Scheidler, Balint Ferencz, Hofi,
László Várady, Szilard Parrag, Tamás Kosztyu
