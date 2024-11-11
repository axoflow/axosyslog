4.9.0
=====

AxoSyslog is binary-compatible with syslog-ng [[1]](#r1) and serves as a drop-in replacement.

Explore and learn more about the new features in our [release announcement blog post](https://axoflow.com/axosyslog-release-4-9/).

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/pkgs/container/axosyslog) and Helm charts.

Packages are available for Debian and Ubuntu from our APT repository.
RPM packages are available in the Assets section (we’re working on an RPM repository as well, and hope to have it up and running for the next release).

FilterX (AxoSyslog's advanced parsing and filtering language) became a [publicly available feature](https://axoflow.com/filterx-introduction/) in AxoSyslog after the 4.8 release.
As it is currently under heavy development, FilterX related news entries can be found in separate sections.
Please note that although its syntax approaches its final form, it may break in subsequent releases.

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Highlights

### Sending data to ClickHouse

The new `clickhouse()` destination uses ClickHouse's [gRPC](https://clickhouse.com/docs/en/interfaces/grpc)
interface to insert logs.

Please note, that as of today, ClickHouse Cloud does not support
the gRPC interface. The `clickhouse()` destination is currently
only useful for self hosted ClickHouse servers.

If you would like to send logs to ClickHouse Cloud, gRPC support
can be requested from the ClickHouse Cloud team or a HTTP based
driver can be implemented in AxoSyslog.

Example config:
```
clickhouse(
  database("default")
  table("my_first_table")
  user("default")
  password("pw")
  schema(
    "user_id" UInt32 => $R_MSEC,
    "message" String => "$MSG",
    "timestamp" DateTime => "$R_UNIXTIME",
    "metric" Float32 => 3.14
  )
  workers(4)
  batch-lines(1000)
  batch-timeout(1000)
);
```
([#354](https://github.com/axoflow/axosyslog/pull/354))

## Features

  * `opentelemetry()`, `loki()` destination: Added support for templated `header()` values.
    ([#334](https://github.com/axoflow/axosyslog/pull/334))

  * `opentelemetry()`, `axosyslog-otlp()`: Added `keep-alive()` options.

    Keepalive can be configured with the `time()`, `timeout()`
    and `max-pings-without-data()` options of the `keep-alive()` block.

    ```
    opentelemetry(
        ...
        keep-alive(time(20000) timeout(10000) max-pings-without-data(0))
    );
    ```
    ([#276](https://github.com/axoflow/axosyslog/pull/276))

  * `bigquery()`: Added `auth()` options.

    Similarly to other gRPC based destination drivers, the `bigquery()`
    destination now accepts different authentication methods, like
    `adc()`, `alts()`, `insecure()` and `tls()`.

    ```
    bigquery (
        ...
        auth(
            tls(
                ca-file("/path/to/ca.pem")
                key-file("/path/to/key.pem")
                cert-file("/path/to/cert.pem")
            )
        )
    );
    ```
    ([#276](https://github.com/axoflow/axosyslog/pull/276))

  * `loki()`: Added `batch-bytes()` and `compression()` options.
    ([#276](https://github.com/axoflow/axosyslog/pull/276))

  * socket based sources: Added a new option called `idle-timeout()`.

    Setting this option makes AxoSyslog close the client connection
    if no data is received for the set amount of seconds.
    ([#355](https://github.com/axoflow/axosyslog/pull/355))

  * socket based sources: Added new flag, called `exit-on-eof`.

    Setting this flag to a source makes AxoSyslog stop,
    when EOF is received.
    ([#351](https://github.com/axoflow/axosyslog/pull/351))

  * `syslog-ng-ctl`: Added `attach` subcommand.

    With `attach`, it is possible to attach to the
    standard IO of the `syslog-ng` proccess.

    Example usage:
    ```
    # takes the stdio fds for 10 seconds and displays syslog-ng output in that time period
    $ syslog-ng-ctl attach stdio --seconds 10
    ```
    ```
    # steal trace level log messages for 10 seconds
    $ syslog-ng-ctl attach logs --seconds 10 --log-level trace
    ```
    ([#326](https://github.com/axoflow/axosyslog/pull/326))


## Bugfixes

  * Config `@version`: Fixed compat-mode inconsistencies when `@version`
    was not specified at the top of the configuration file or was not specified at all.
    ([#312](https://github.com/axoflow/axosyslog/pull/312))

  * `s3()`: Eliminated indefinite memory usage increase for each reload.

    The increased memory usage is caused by the `botocore` library, which
    caches the session information. We only need the Session object, if
    `role()` is set. The increased memory usage still happens with that set,
    currently we only fixed the unset case.
    ([#318](https://github.com/axoflow/axosyslog/pull/318))

  * `opentelemetry()`, `axosyslog-otlp()` sources: Fixed source hang-up on flow-controlled paths.
    ([#314](https://github.com/axoflow/axosyslog/pull/314))

  * `opentelemetry()`, `axosyslog-otlp()` sources: Fixed a crash when `workers()` is set to `> 1`.
    ([#310](https://github.com/axoflow/axosyslog/pull/310))

  * `file()`, `wildcard-file()`: Fixed a crash and persist name collision issues.

    If multiple `wildcard-file()` sources or a `wildcard-file()` and a `file()` source were
    reading the same input file, it could result in log loss, log duplication, and various crashes.
    ([#291](https://github.com/axoflow/axosyslog/pull/291))

  * `wildcard-file()`: Fixed a crash that occurs after config reload when the source is flow-controlled.
    ([#293](https://github.com/axoflow/axosyslog/pull/293))

  * `file()`, `stdout()`: Fixed log sources getting stuck.

    Due to an acknowledgment bug in the `file()` and `stdout()` destinations,
    sources routed to those destinations may have gotten stuck as they were
    flow-controlled incorrectly.

    This issue occured only in extremely rare cases with regular files, but it
    occured frequently with `/dev/stderr` and other slow pseudo-devices.
    ([#303](https://github.com/axoflow/axosyslog/pull/303))

  * metrics: `syslog-ng-ctl --reset` will no longer reset Prometheus metrics
    ([#370](https://github.com/axoflow/axosyslog/pull/370))

  * `stats`: Fixed `free_window` counters.
    ([#296](https://github.com/axoflow/axosyslog/pull/296))


## FilterX features

  * Added new filterx code flow controls.

    * `drop`: Drops the currently processed message and returns success.
    * `done`: Stops the processing and returns success.
    ([#269](https://github.com/axoflow/axosyslog/pull/269))

  * `update_metric()`: Added a new function similar to `metrics-probe` parser.

    Example usage:
    ```
    update_metric("filterx_metric", labels={"msg": $MSG, "foo": "foovalue"}, level=1, increment=$INCREMENT);
    ```
    ([#220](https://github.com/axoflow/axosyslog/pull/220))

  * `startswith()`, `endswith()`, `includes()`: Added string matching functions.

    * First argument is the string that is being matched.
    * Second argument is either a single substring or a list of substrings.
    * Optionally the `ignorecase` argument can be set to configure case sensitivity
      * default: `false`

    Example usage:
    ```
    startswith(string, prefix, ignorecase=false);
    startswith(string, [prefix_1, prefix_2], ignorecase=true);

    endswith(string, suffix, ignorecase=false);
    endswith(string, [suffix_1, suffix_2], ignorecase=true);

    includes(string, substring, ignorecase=false);
    includes(string, [substring_1, substring_2], ignorecase=true);
    ```
    ([#297](https://github.com/axoflow/axosyslog/pull/297))

  * `parse_xml()`: Added new function to parse XMLs.

    Example usage:
    ```
    my_structured_data = parse_xml(raw_xml);
    ```

    Converting XML to a dict is not standardized.

    Our intention is to create the most compact dict as possible,
    which means certain nodes will have different types and
    structures based on a number of different qualities of the
    input XML element.

    The following points will demonstrate the choices we made in our parser.
    In the examples we will use the JSON dict implementation.

    1. Empty XML elements become empty strings.
    ```
      XML:  <foo></foo>
      JSON: {"foo": ""}
    ```

    2. Attributions are stored in `@attr` key-value pairs,
       similarly to some other converters (e.g.: python xmltodict).
    ```
      XML:  <foo bar="123" baz="bad"/>
      JSON: {"foo": {"@bar": "123", "@baz": "bad"}}
    ```

    3. If an XML element has both attributes and a value, 
       we need to store them in a dict, and the value needs a key.
       We store the text value under the #text key.
    ```
      XML:  <foo bar="123">baz</foo>
      JSON: {"foo": {"@bar": "123", "#text": "baz"}}
    ```

    4. An XML element can have both a value and inner elements.
       We use the `#text` key here, too.
    ```
      XML:  <foo>bar<baz>123</baz></foo>
      JSON: {"foo": {"#text": "bar", "baz": "123"}}
    ```

    5. An XML element can have multiple values separated by inner elements.
       In that case we concatenate the values.
    ```
      XML:  <foo>bar<a></a>baz</foo>
      JSON: {"foo": {"#text": "barbaz", "a": ""}}
    ```
    ([#251](https://github.com/axoflow/axosyslog/pull/251))

  * `parse_windows_eventlog_xml()`: Added a new function to parse Windows EventLog XMLs.

    This parser is really similar to `parse_xml()` with
    a couple of small differences:

    1. There is a quick schema validation.
    2. The `Event`->`EventData` field automatically handles named `Data` elements.
    ([#282](https://github.com/axoflow/axosyslog/pull/282))

  * `parse_cef()`, `parse_leef()`: Added CEF and LEEF parsers.

    * The first argument is the raw message.
    * Optionally `pair_separator` and `value_separator` arguments
      can be set to override the respective extension parsing behavior.

    Example usage:
    ```
    my_structured_leef = parse_leef(leef_message);
    my_structured_cef = parse_cef(cef_message);
    ```
    ([#324](https://github.com/axoflow/axosyslog/pull/324))

  * `flatten()`: Added new function to flatten dicts and lists.

    The function modifies the object in-place.
    The separator can be set with the `separator` argument,
    which is `.` by default.

    Example usage:
    ```
    flatten(my_dict_or_list, separator="->");
    ```
    ([#221](https://github.com/axoflow/axosyslog/pull/221))

  * Added new RFC5424 SDATA related functions.

    All of the functions require traditional syslog parsing beforehand.

    * `has_sdata()`
      * Returns whether the current log has SDATA information.
      * Example: `sdata_avail = has_sdata(;)`
    * `is_sdata_from_enterprise()`
      * Checks if there is SDATA that corresponds to the given enterprise ID.
      * Example: `sdata_from_6876 = is_sdata_from_enterprise("6876");`
    * `get_sdata()`
      * Returns a 2 level dict of the available SDATAs.
      * Example: `sdata = get_sdata();`
      * Returns: `{"Originator@6876": {"sub": "Vimsvc.ha-eventmgr", "opID": "esxui-13c6-6b16"}}`
    ([#242](https://github.com/axoflow/axosyslog/pull/242))

  * `regexp_subst()`: Added various pcre flags.

    * `jit`:
      * enables or disables JIT compliling
      * default: `true`
    * `global`:
      * sets whether all found matches should be replaced
      * default: `false`
    * `utf8`:
      * enables or disables UTF-8 validation
      * default: `false`
    * `ignorecase`
      * sets case sensitivity
      * default: `false` (case-sensitive)
    * `newline`
      * configures the behavior of end of line finding
      * `false` returns end of line when CR, LF and CRLF characters are found
      * `true` makes the matcher process CR, LF, CRLF characters
      * default: `false`
    ([#203](https://github.com/axoflow/axosyslog/pull/203))

  * `unset_empties()`: Added advanced options.

    `unset_empties` removes elements from the given dictionary or list that match
    the empties set. If the `recursive` argument is provided, the function will
    process nested dictionaries as well. The `replacement` argument allows
    replacing target elements with a specified object, and the targets
    argument customizes which elements are removed or replaced, overriding
    the default empties set.

    * Optional named arguments:
      * recursive: Enables recursive processing of nested dictionaries. default: `true`
      * ignorecase: Enables case-insensitive matching. default: `true`
      * replacement: Specifies an object to replace target elements instead of removing them.
        default: nothing (remove)
      * targets: A list of elements to identify for removal or replacement, clearing the default empty set.
        default: `["", null, [], {}]`

    Example usage:
    ```
    unset_empties(js1, targets=["foo", "bar", null, "", [], {}], ignorecase=false, replacement="N/A", recursive=false);
    ```
    ([#275](https://github.com/axoflow/axosyslog/pull/275))

  * Added `+` operator.
    ([#217](https://github.com/axoflow/axosyslog/pull/217))

  * Added `!~` operator as the negated `=~` operator.
    ([#238](https://github.com/axoflow/axosyslog/pull/238))

  * `unset()`: Now accepts any number of variables to unset.
    ([#215](https://github.com/axoflow/axosyslog/pull/215))

  * Use `json` and `json_array` as default types for dict and list literals.

    This is now a valid config and creates `json` and `json_array` objects:
    ```
    my_json_object = {"foo": "bar"};
    my_json_array = ["foo", "bar"];
    ```
    ([#255](https://github.com/axoflow/axosyslog/pull/255))

  * `datetime`: `datetime` objects can now be cased to `integer` and `double`.
    ([#284](https://github.com/axoflow/axosyslog/pull/284))

  * `datetime`: 0 valued `datetime` objects are now falsy.
    ([#283](https://github.com/axoflow/axosyslog/pull/283))

  * `parse_csv()`: Changed strip whitespace default to `false`.
    ([#219](https://github.com/axoflow/axosyslog/pull/219))

  * `parse_csv()`: Renamed `strip_whitespaces` argument to `strip_whitespace`.
    ([#219](https://github.com/axoflow/axosyslog/pull/219))

  * Declared variables now can be set with dict and list literals.
    ([#287](https://github.com/axoflow/axosyslog/pull/287))


## FilterX bugfixes

  * Fixed LogMessage -> FilterX variable synchronization.
    ([#333](https://github.com/axoflow/axosyslog/pull/333))

  * `parse_csv()`: Fixed a race condition.
    ([#249](https://github.com/axoflow/axosyslog/pull/249))

  * `parse_csv()`: Fixed an invalid read.
    ([#287](https://github.com/axoflow/axosyslog/pull/287))

  * `format_csv()`: Fixed delimiter formatting.
    ([#218](https://github.com/axoflow/axosyslog/pull/218))

  * `json_array`: Fixed failing to return `null` values.
    ([#273](https://github.com/axoflow/axosyslog/pull/273))

  * Fixed race conditions in several functions.
    ([#257](https://github.com/axoflow/axosyslog/pull/257))

  * `json`: Fixed race condition in marshalling.
    ([#258](https://github.com/axoflow/axosyslog/pull/258))

  * `json`: Fixed a crash that occured when doubles were stored and accessed.
    ([#230](https://github.com/axoflow/axosyslog/pull/230))

<a id="r1">[1]</a> syslog-ng is a trademark of One Identity.

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

Alex Becker, Andras Mitzki, Attila Szakacs, Balazs Scheidler, Hofi,
Kovacs, Gergo Ferenc, László Várady, Mate Ory, Sergey Fedorov,
Szilard Parrag, shifter
