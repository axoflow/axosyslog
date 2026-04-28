4.25.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.


## Features

  * `syslog()`/`network()`: HAProxy protocol v2 support over UDP

    When `transport(proxied-udp)` is configured, the original client address and port are available as `${SOURCEIP}`,
    `${SOURCEPORT}`, `${DESTIP}`, and `${DESTPORT}`.
    ([#987](https://github.com/axoflow/axosyslog/pull/987))

  * FilterX: Added new function: `uuid()` that generates a random UUID v4 string.
    ([#1018](https://github.com/axoflow/axosyslog/pull/1018))

  * FilterX: Added various crypto hash digest functions.

    These functions compute the hash of a string or bytes and return the result as a hex string:
    * `md5()`
    * `sha1()`
    * `sha256()`
    * `sha512()`

    The generic `digest(input, alg="sha256")` function accepts an optional algorithm
    name and returns the raw hash as a bytes object.
    ([#1019](https://github.com/axoflow/axosyslog/pull/1019))

  * FilterX: Added `utf8_validate()` and `utf8_sanitize()` string functions.

    * `utf8_validate()` checks whether the string contains valid UTF-8 sequences and returns a boolean
    * `utf8_sanitize()` replaces invalid byte sequences with their `\xNN` escaped representation
    ([#1019](https://github.com/axoflow/axosyslog/pull/1019))

  * FilterX: Added various encoding/decoding functions.

    * `base64_encode()`/`base64_decode()` (bytes <-> string)
    * `urlencode()`/`urldecode()` (string <-> string)
    * `hex_encode()`/`hex_decode()` (bytes <-> string)
    ([#1019](https://github.com/axoflow/axosyslog/pull/1019))

  * New FilterX types `subnet()` and `ip()`: these new types encapsulate an
    IPv4/IPv6 subnet (in CIDR notation) or a single IP address. Both types takes
    their string representation and will return an ERROR if the format cannot be
    parsed.

    Example configuration:

        a = subnet("192.168.0.0/24");

        "192.168.0.5" in a;
        "192.168.1.5" in a or true;
        ip("192.168.0.11") in a;

        a6 = subnet("DEAD:BEEF::1/64");

        "DEAD:BEEF::2" in a6;
        "DEAD:BABE::1" in a6 or true;
        ip("DEAD:BEEF::00ac") in a6;
    ([#1021](https://github.com/axoflow/axosyslog/pull/1021))

  * FilterX `glob_match()` function: this function will match a filename against
    a single-, or a list of glob-style patterns.

    Example:

        glob_match(filename, "*.zip");
        glob_match(filename, ["*.zip", "*.7z"]);
    ([#1039](https://github.com/axoflow/axosyslog/pull/1039))

  * FilterX `cache_json_file`: add deafult_value parameter to FilterX function

    If the file is not present, or an error occurs when reading it, the default_value will be used, if provided.

    Example:
    ```
    cache_json_file("./test.json", default_value={"key": "value"});
    ```
    ([#1034](https://github.com/axoflow/axosyslog/pull/1034))


## Bugfixes

  * `disk-buffer()`: keep the queue alive during reload

    Keeping the disk-buffer alive on reload fixes a bug, where a full disk-buffer can
    grow infinitely by reloading. It can also cause significant reload speedup.
    ([#1030](https://github.com/axoflow/axosyslog/pull/1030))

  * `disk-buffer()`: fix message ordering issue when a message batch failed to be delivered
    ([#1005](https://github.com/axoflow/axosyslog/pull/1005))

  * CR (`\r`) characters are now removed from line endings, empty UDP datagrams are dropped
    ([#1000](https://github.com/axoflow/axosyslog/pull/1000))

  * FilterX `parse_xml()`: fix crash in case of invalid XML
    ([#1041](https://github.com/axoflow/axosyslog/pull/1041))

  * FilterX: fix crash when using the `+` operator on dicts
    ([#1040](https://github.com/axoflow/axosyslog/pull/1040))

  * FilterX: fix error handling of the `=??` operator
    ([#1053](https://github.com/axoflow/axosyslog/pull/1053))


## Other changes

  * `opentelemetry()`, `axosyslog-otlp`, `loki()`, `google-pubsub`, `clickhouse`, `bigquery` : improve performance by using gRPC arenas for allocation
    ([#1015](https://github.com/axoflow/axosyslog/pull/1015))

  * New metrics: `syslogng_input_transport_errors_total` for syslog framing and TLS errors
    ([#1026](https://github.com/axoflow/axosyslog/pull/1026))

  * `http()`: error reporting improvements of batched sending
    ([#1001](https://github.com/axoflow/axosyslog/pull/1001))

  * Network sources: `transport(auto)` detections became more robust
    ([#1013](https://github.com/axoflow/axosyslog/pull/1013))

  * `syslog-ng --interactive`: new debugger commands - step, continue, follow, trace
    ([#340](https://github.com/axoflow/axosyslog/pull/340))

  * FilterX performance optimizations

  * Contribution Guide: added section on how to contribute AI-assisted code
    ([#1043](https://github.com/axoflow/axosyslog/pull/1043))



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
Szilard Parrag, Tamás Kosztyu
