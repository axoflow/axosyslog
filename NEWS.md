4.26.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Highlights

  * `arrow-flight()`: Added a new destination for [Apache Arrow Flight](https://arrow.apache.org/docs/format/Flight.html)

    Options:
      * `url()`: Flight endpoint
      * `path()`: descriptor path, templatable so a single destination can route to many tables
      * `schema()`: one `"name" TYPE => template` entry per column

    Column types supported in `schema()`:
      * `STRING`
      * `INT64` (alias `INTEGER`)
      * `DOUBLE` (alias `FLOAT64`)
      * `BOOL` (alias `BOOLEAN`)
      * `TIMESTAMP`
      * `MAP(STRING, STRING)`

    Example configuration:

    ```
    destination d_arrow {
      arrow-flight(
        url("grpc://flight.example.com:8815")
        path("events.${HOST}")
        schema(
          "ts"       TIMESTAMP => "$UNIXTIME"
          "host"     STRING    => "$HOST"
          "program"  STRING    => "$PROGRAM"
          "severity" INT64     => "$LEVEL_NUM"
          "msg"      STRING    => "$MSG"
        )
        batch-lines(1000)
        batch-bytes(1048576)
        batch-timeout(5000)
        workers(4)
        worker-partition-key("${HOST}")
      );
    };
    ```
    ([#1069](https://github.com/axoflow/axosyslog/pull/1069))

  * Performance improvements

    We've improved the source processing and FilterX performance of AxoSyslog even further.
    ([#925](https://github.com/axoflow/axosyslog/pull/925), [#1077](https://github.com/axoflow/axosyslog/pull/1077), [#1082](https://github.com/axoflow/axosyslog/pull/1082), [#1086](https://github.com/axoflow/axosyslog/pull/1086), [#1081](https://github.com/axoflow/axosyslog/pull/1081), [#1078](https://github.com/axoflow/axosyslog/pull/1078), [#1090](https://github.com/axoflow/axosyslog/pull/1090), [#1091](https://github.com/axoflow/axosyslog/pull/1091), [#1096](https://github.com/axoflow/axosyslog/pull/1096))

    An experimental feature, the FilterX compiler has been introduced, which will improve FilterX performance by
    compiling blocks instead of interpreting them. It can be enabled with the `filterx-jit(yes)` global option.
    For now, only a subset of expressions is compiled, so the performance gains are modest at this stage.
    ([#1061](https://github.com/axoflow/axosyslog/pull/1061), [#1049](https://github.com/axoflow/axosyslog/pull/1049), [#1055](https://github.com/axoflow/axosyslog/pull/1055), [#1067](https://github.com/axoflow/axosyslog/pull/1067), [#1065](https://github.com/axoflow/axosyslog/pull/1065), [#1080](https://github.com/axoflow/axosyslog/pull/1080))

## Features

  * Add `trusted-fingerprints()` option: this new option will allow you to trust
    X.509 certificates based on their fingerprints. The new option deprecates
    `trusted-keys()`, the major difference being that `trusted-fingerprints()`
    will accept X.509 certificates as valid, even if the normal X.509 validation
    fails, whereas `trusted-keys()` needed both the X.509 verification pass and
    the fingerprint checking to succeed. This feature also adds the capability
    to use a fingerprinting method other than SHA1, which is not considered safe
    anymore.

    Here's an example syntax:

    	tls(
    		...
    		trusted-fingerprints("SHA1:0C:EF:34:4D:0B:74:AE:03:72:9A:4E:68:AF:90:59:A9:EF:35:1F:AA",
    				     "SHA512:15:B3:C5:96:48:5B:F6:20:C3:86:47:78:99:E1:2B:F2:C4:A6:93:AE:E8:0A:B3:F7:78:39:66:B4:EF:4F:A5:47:2A:E0:4A:93:06:46:72:C0:15:6A:FC:59:10:25:37:60:E3:84:E9:EC:90:30:12:F5:27:EA:22:1F:55:9B:3B:97")
    	)

    NOTE: the fingerprinting method is the word before the first colon. The
    naming of the fingerprinting methods should match OpenSSL's supported digest
    algorithms.

    As of OpenSSL 3.0.10, the following digest algorithms are supported:

    Message Digest commands (see the `dgst' command for more details)
    blake2b512        blake2s256        md4               md5
    rmd160            sha1              sha224            sha256
    sha3-224          sha3-256          sha3-384          sha3-512
    sha384            sha512            sha512-224        sha512-256
    shake128          shake256          sm3

    To find out the fingerprint for a certificate, you can use this command:

    $ openssl x509 -sha512 -in <certificate file in pem> -fingerprint
    ([#137](https://github.com/axoflow/axosyslog/pull/137))

  * `switch`: add ranged case support for FilterX `switch`

    Example usage:
    ```
    selector = 5;
    switch (selector) {
        case 1..4:
            result = "below";
            break;
        case 5:
            result = "exact";
            break;
        default:
            result = "above";
            break;
    };
    ```
    ([#1093](https://github.com/axoflow/axosyslog/pull/1093))

  * `afuser`: add escaping() option to usertty() output

    The usertty() destination now supports an escaping() option, using the same
    template escaping behavior as templates.
    ([#1117](https://github.com/axoflow/axosyslog/pull/1117))


## Bugfixes

  * `proxy-protocol`: fix out-of-bounds read with a malformed PROXY protocol v2 datagram

    A PROXY protocol v2 datagram received over UDP could declare a header length larger than
    the bytes actually received, leading to a read past the end of the buffer. The declared
    length is now validated against the received size.
    ([#1142](https://github.com/axoflow/axosyslog/pull/1142))

  * `logmsg`: fix crash when iterating name-value registry concurrently

    The hash table mapping value names to handles was iterated without locking, which could crash AxoSyslog
    when another thread registered new name-value pairs at the same time. This happened for example when the
    Python `LogMessage.keys()` method ran while a `kv-parser()` was processing messages on a parallel path.
    ([#1122](https://github.com/axoflow/axosyslog/pull/1122))

  * `bigquery()`: Fixed a crash caused by the server closing the `AppendRows` stream.
    ([#1127](https://github.com/axoflow/axosyslog/pull/1127))

  * `logproto`: fix crash with `transport(auto)` sources across a config reload
    ([#1124](https://github.com/axoflow/axosyslog/pull/1124))

  * `afsql`: fix segfault after database error
    ([#1114](https://github.com/axoflow/axosyslog/pull/1114))

  * `java/hdfs`: fix unreleased lock in `send()` when file open fails

    If `getHdfsFile()` returned `null`, the lock acquired at the start of
    `send()` was never released, causing a permanent deadlock on all
    subsequent calls.
    ([#1108](https://github.com/axoflow/axosyslog/pull/1108))

  * `correlation`: fix radix parser end-of-input handling

    Fixes two related radix matcher edge cases at end of input.

    Parser scans now stop before '\0' to avoid reading past end-of-input and to keep captured lengths correct.
    Parser-node traversal now continues with empty remaining input, so OPTIONALSET children can still match.
    ([#1110](https://github.com/axoflow/axosyslog/pull/1110))

  * Fix `internal()` source infinitely looping debug/trace messages
    ([#1125](https://github.com/axoflow/axosyslog/pull/1125))

  * Fix Python LogMessage crash
    ([#1101](https://github.com/axoflow/axosyslog/pull/1101), [#1122](https://github.com/axoflow/axosyslog/pull/1122))

  * Fix a LogMessage memory allocation issue
    ([#1075](https://github.com/axoflow/axosyslog/pull/1075))

  * `filterx`: fix out-of-bounds write when parsing a subnet with a negative prefix

    A `subnet()` CIDR with a negative prefix such as `::/-100` slipped past the prefix
    bounds check and produced a negative netmask length, writing past the end of the
    buffer. Negative prefixes are now rejected.
    ([#1141](https://github.com/axoflow/axosyslog/pull/1141))

  * `filterx`: fix out-of-bounds read in `regexp_subst()` on an empty subject

    A zero-length match while substituting on an empty subject advanced past the end of
    the string, so the trailing copy was given a huge length. The copies are now bounded
    by the subject length.
    ([#1141](https://github.com/axoflow/axosyslog/pull/1141))

  * `filterx`: fix crash when moving a key out of an empty dict

    Moving a key from an empty dict dereferenced its unallocated backing table. It now
    reports the key as missing instead of crashing.
    ([#1141](https://github.com/axoflow/axosyslog/pull/1141))

  * `filterx`: fix crash on integer division or modulo by zero

    Dividing or taking the modulo of an integer by zero raised SIGFPE, and `INT64_MIN`
    divided by `-1` overflowed. Both now raise a filterx error, including when the
    operands are folded to a constant at startup.
    ([#1141](https://github.com/axoflow/axosyslog/pull/1141))

  * `filterx`: fix crash in `set_pri()` when its argument cannot be evaluated

    A `set_pri()` argument that produced no value, for example `int()` of a non-numeric
    string, was dereferenced. The evaluation error is now propagated.
    ([#1141](https://github.com/axoflow/axosyslog/pull/1141))

  * `filterx`: fix stack overflow in `unset_empties()` on very wide or deeply nested input

    The keys-to-unset list was a stack array sized by the dict, so a very wide dict
    overflowed the stack, and recursion into nested dicts was unbounded. The buffer is now
    bounded and falls back to the heap for wide dicts, and the recursion depth is capped.
    ([#1141](https://github.com/axoflow/axosyslog/pull/1141))

  * `filterx`: fix stack overflow when parsing deeply nested JSON

    Deeply nested JSON passed to `json()` recursed once per nesting level with no limit and
    could exhaust the stack. The nesting depth is now capped.
    ([#1141](https://github.com/axoflow/axosyslog/pull/1141))

  * `filterx`: Fixed a bug where a value read from a log message field could be corrupted when that field was
    overwritten later in the same flow.
    ([#1123](https://github.com/axoflow/axosyslog/pull/1123))

  * Fix `--check-startup`
    ([#1152](https://github.com/axoflow/axosyslog/pull/1152))


## Other changes

  * Ubuntu 26.04, Fedora 44, and AlmaLinux 10 packages
    ([#1068](https://github.com/axoflow/axosyslog/pull/1068), [#1070](https://github.com/axoflow/axosyslog/pull/1070))

  * Monolithic build option

    The new `--with-linking-mode=monolithic` configure option can be used to produce a syslog-ng binary containing
    all modules (statically linked).

    This generally improves the performance of AxoSyslog.
    ([#1016](https://github.com/axoflow/axosyslog/pull/1016))

  * Decouple `repr()` and `string()`: previously the repr() of an object was
    typically the same as str, did not include type-related hints in its output.
    This makes repr() usage less useful, especially as we are adding more types
    to filterx. Starting with this version, repr() includes a format similar to
    Python's repr. This is an incompatible change for uses where repr() was
    directly used in an output, but normally its intended has always been the
    debug log of filterx. If your use-case relies on the current repr() format,
    you should explicitly cast your object to `string()` instead.
    ([#1033](https://github.com/axoflow/axosyslog/pull/1033))

  * `parallelize()`: improve throughput by avoiding batches that are too large.
    Also set the default batch-size() parameter to 100, both in case of
    partition based and round robin batching.
    ([#1078](https://github.com/axoflow/axosyslog/pull/1078))

  * `parallelize()`: add `syslogng_parallelized_batch_size` and
    `syslogng_parallelized_input_batch_size` histograms to the prometheus style
    stats output, at stats(level(4)).
    ([#1077](https://github.com/axoflow/axosyslog/pull/1077))

  * `syslog()`, `network()` sources: add `syslogng_input_transport_errors_total` metric

    It shows `invalid-frame-header` or `tls-handshake` related transport errors.
    ([#1026](https://github.com/axoflow/axosyslog/pull/1026))

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

Andras Mitzki, Attila Szakacs-Bertok, Balazs Scheidler, Balint Ferencz,
Bence Csati, Hofi, Jon Polom, László Várady, Szilard Parrag, Tamás Kosztyu,
engzaz
