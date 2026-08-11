4.27.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.


## Features

  * `http()`: add response-adapter(openobserve) support, which processes HTTP
    responses from OpenObserve backends. OpenObserve normally returns HTTP 200
    OK even for requests that fail partially, and this setting will turn that
    into an actual error, so it can be retried.
    ([#1103](https://github.com/axoflow/axosyslog/pull/1103))

  * `http()`: errors reported by a response-adapter() are now retried as many
    times as retries() allows and the batch is dropped afterwards.
    ([#1103](https://github.com/axoflow/axosyslog/pull/1103))

  * `splunk-hec-event()`: the destination now enables response-adapter(splunk)
    by default. When Splunk rejects an event of a batch, the offending message
    is logged and the batch is retried. Other HTTP errors keep their previous
    handling.
    ([#1103](https://github.com/axoflow/axosyslog/pull/1103))

  * `network()`, `tcp()`: honor `flush-lines()` by coalescing writes

    The `network()` and `tcp()` destinations now coalesce up to `flush-lines()` formatted messages into a
    single write instead of writing each message separately, cutting system call overhead and improving
    throughput.

    This covers stream and TLS transports. UDP and other datagram transports still write one message per
    packet, and `syslog()` is unaffected because it frames each message individually.

    `flush-lines()` previously had no effect on these destinations. Starting with configuration version
    `@config: 4.26` it defaults to 100, enabling coalescing, while older configurations keep the previous
    one-write-per-message behavior.
    ([#1133](https://github.com/axoflow/axosyslog/pull/1133))

  * `network()` and `syslog()` destinations: improve performance of `spoof-source()`
    
    The sending of packets is now running in a destination thread (instead of
    piggybacking it to the source thread), which also allowed to eliminate the
    per-destination lock protecting the send operation.

    This helps for scalability
    ([#1064](https://github.com/axoflow/axosyslog/pull/1064))

  * `filterx` add new type called `tuple()`
  
    This is a read-only, list-like data type, that can only be initialized once,
    and then remains read-only until the end of its lifecycle.
    Patterned after the Python type of the same name.

      t = ();        # empty tuple
      t = ("foo",);  # singleton
      t = (1,2,3);   # a tuple of 3 elements
    ([#1047](https://github.com/axoflow/axosyslog/pull/1047))

  * `filterx`: add the `uuid7()` function

    `uuid7()` generates RFC 9562 UUIDv7 identifiers, which embed a millisecond-precision Unix timestamp, so
    they sort lexically by creation time.

    For consistency with `uuid7()`, `uuid()` now has an alias: `uuid4()`.
    ([#1168](https://github.com/axoflow/axosyslog/pull/1168))

  * `filterx` error handling improvements
  
    Filterx errors were made more concise.
    Removed multiple layers of errors for simple statements, and
    instead we use a single, but more precise error entry.

    The single error entry points to the statement, instead of the
    sub-expression, and the error message explains what failed.

    Example, previously:

    FILTERX ERROR; err_idx='[1/2]', expr='syslog-ng.conf:12:13|	$PID', error='Variable is unset: "$PID"'
    FILTERX ERROR; err_idx='[2/2]', expr='syslog-ng.conf:12:4|	d[string($PID)]', error='Failed to get-subscript from object: Failed to evaluate key'

    now:

    FILTERX ERROR; err_idx='[1/1]', expr='syslog-ng.conf:12:4|	d[string($PID)]', error='Variable is unset: "$PID"'
    ([#1121](https://github.com/axoflow/axosyslog/pull/1121))


## Bugfixes

  * `filterx`: fix crash constant-folding an invalid `+` expression
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * `filterx`: cap recursion depth in `flatten()`
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * `syslog-parser()`, `date-parser()`: Fixed some options getting lost when the parser is referenced from multiple log paths.
    ([#1174](https://github.com/axoflow/axosyslog/pull/1174))

  * `filterx`: guard integer overflow in the addition operator
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * `filterx`: JSON parsing (`parse_json()`, `cache_json_file()`) no longer fails on JSON documents that need more
    than 65536 jsmn tokens. The token array is now grown dynamically until parsing succeeds or memory is
    actually exhausted, instead of being capped at a hardcoded token count.
    ([#1181](https://github.com/axoflow/axosyslog/pull/1181))

  * `filterx`: fix crash calling `isset()` on a macro
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * comparisons in filterx: when comparing values with different types as
    strings, use the result of the string() cast instead of relying on the
    marshalled format.  This is an incompatible change when comparing null() and
    datetime() objects against strings, but comparing those to strings is rare
    and this was considered to be a more intuitive behaviour.
    ([#1098](https://github.com/axoflow/axosyslog/pull/1098))

  * `filterx`: fix undefined behavior in `subnet()` with a `/0` prefix
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * `filterx`: guard integer overflow in the subtraction, multiplication and unary minus operators
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * `filterx`: fix out-of-bounds read on a list subscript with a large negative index
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * `filterx`: reject non-finite and out-of-range doubles when converting to datetime
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * `filterx`: reject non-finite (Inf/NaN) results in double arithmetic
    ([#1163](https://github.com/axoflow/axosyslog/pull/1163))

  * `disk-buffer()`: fix leaking a file descriptor per disk-buffer file allocation
    ([#1188](https://github.com/axoflow/axosyslog/pull/1188))

  * `secure-logging`: replaced the pseudo-random function used for key derivation

    The old PRF fed only 16 bytes of its input to AES-CMAC and let an attacker tell it apart from a real
    random function with crafted inputs. Key derivation now uses an AES-CMAC key expansion over the full
    input, following the scheme of NIST SP 800-108r1.

    Every derived key changes, so keys and logs produced by earlier versions cannot be verified by this
    version.

    Key file handling and MAC verification are fixed as well: a key file whose CMAC does not match is now
    rejected instead of being loaded.
    ([#1109](https://github.com/axoflow/axosyslog/pull/1109))


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

Airbus Commercial Aircraft, Andras Mitzki, Attila Szakacs-Bertok,
Balazs Scheidler, Balint Ferencz, László Várady, Peter Krawczyk,
Szilard Parrag, Vadivelan
