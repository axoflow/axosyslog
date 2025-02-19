4.10.1
======

_These are news entries of AxoSyslog 4.10.0.
4.10.1 fixed two crashes related to FilterX strings and JSON objects._

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

Explore and learn more about the new features in our [release announcement blog post](https://axoflow.com/axosyslog-release-4-10/).

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages)
repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.

## Highlights

### Google Pub/Sub gRPC destination

Sending logs to Google Pub/Sub via the gRPC interface.

Example config:
```
google-pubsub-grpc(
  project("my_project")
  topic($topic)

  data($MESSAGE)
  attributes(
    timestamp => $S_ISODATE,
    host => $HOST,
  )

  workers(4)
  batch-timeout(1000) # ms
  batch-lines(1000)
);
```

The `project()` and `topic()` options are templatable.
The default service endpoint can be changed with the `service_endpoint()` option.

([#373](https://github.com/axoflow/axosyslog/pull/373))

### Azure Monitor destination

Sending logs to Azure Monitor using OAuth 2 authentication.

Example config:
```
azure-monitor-custom(
  table-name("table")
  dcr-id("dcr id")
  dce-uri("https://dce-uri.ingest.monitor.azure.com")

  auth(tenant-id("tenant id") app-id("app id") app-secret("app secret"))

  workers(4)
  batch_timeout(1000) # ms
  batch_lines(5000)
  batch_bytes(4096KiB)
);
```

Note: Table name should not contain the trailing "_CL" string for custom tables.

([#457](https://github.com/axoflow/axosyslog/pull/457))

## Features

  * `syslog()` source driver: add support for RFC6587 style auto-detection of
    octet-count based framing to avoid confusion that stems from the sender
    using a different protocol to the server.  This behaviour can be enabled
    by using `transport(auto)` option for the `syslog()` source.
    ([#4814](https://github.com/axoflow/axosyslog/pull/4814))

  * `syslog(transport(proxied-*))` and `network(transport(proxied-*))`: changed
    where HAProxy transport saved the original source and destination addresses.
    Instead of using dedicated `PROXIED_*` name-value pairs, use the usual
    `$SOURCEIP`, `$SOURCEPORT`, `$DESTIP` and `$DESTPORT` macros, making haproxy
    based connections just like native ones.

    `$SOURCEPORT`: added new macro which expands to the source port of the peer.
    ([#361](https://github.com/axoflow/axosyslog/pull/361))

  * `check-program`: Introduced as a flag for global or source options.

    By default, this flag is set to false. Enabling the check-program flag triggers `program` name validation for `RFC3164` messages. Valid `program` names must adhere to the following criteria:

    Contain only these characters: `[a-zA-Z0-9-_/().]`
    Include at least one alphabetical character.
    If a `program` name fails validation, it will be considered part of the log message.


    Example:

    ```
    source { network(flags(check-hostname, check-program)); };
    ```
    ([#380](https://github.com/axoflow/axosyslog/pull/380))

  * `s3` destination: Added `content-type()` option.
    ([#408](https://github.com/axoflow/axosyslog/pull/408))

  * `bigquery()`, `google-pubsub-grpc()`: Added `service-account()` authentication option

    Example usage:
    ```
    destination {
        google-pubsub-grpc(
            project("test")
            topic("test")
            auth(service-account(key ("path_to_service_account_key.json")))
        );
    };
    ```

    Note: In contrary to the `http()` destination's similar option,
    we do not need to manually set the audience here as it is
    automatically recognized by the underlying gRPC API.
    ([#412](https://github.com/axoflow/axosyslog/pull/412))

  * metrics: add `syslogng_stats_level` metric to monitor the current metric verbosity level
    ([#493](https://github.com/axoflow/axosyslog/pull/493))

  * `webhook()`,`opentelemetry()` sources: support `input_event_bytes` metrics
    ([#494](https://github.com/axoflow/axosyslog/pull/494))


## Bugfixes

  * `network()`, `syslog()` sources and destinations: fix TCP/TLS shutdown
    ([#420](https://github.com/axoflow/axosyslog/pull/420))

  * `network(), syslog()`: Fixed a potential crash for TLS destinations during reload

    In case of a TLS connection, if the handshake didn't happen before reloading AxoSyslog,
    it crashed on the first message sent to that destination.
    ([#418](https://github.com/axoflow/axosyslog/pull/418))

  * `axosyslog-otlp()` destination: Fixed a crash.
    ([#384](https://github.com/axoflow/axosyslog/pull/384))

  * `http`: Fixed a batching related bug that happened with templated URLs and a single worker.
    ([#464](https://github.com/axoflow/axosyslog/pull/464))

## Other changes

  * Crash report (backtrace) on x86-64 and ARM-based Linux systems
    ([#350](https://github.com/axoflow/axosyslog/pull/350))

  * FilterX and log path information for `perf` stackdumps
    ([#433](https://github.com/axoflow/axosyslog/pull/433))

## FilterX features

  * FilterX performance improvements
    ([#253](https://github.com/axoflow/axosyslog/pull/253),
    [#257](https://github.com/axoflow/axosyslog/pull/257),
    [#258](https://github.com/axoflow/axosyslog/pull/258),
    [#330](https://github.com/axoflow/axosyslog/pull/330),
    [#365](https://github.com/axoflow/axosyslog/pull/365),
    [#385](https://github.com/axoflow/axosyslog/pull/385),
    [#390](https://github.com/axoflow/axosyslog/pull/390),
    [#395](https://github.com/axoflow/axosyslog/pull/395),
    [#396](https://github.com/axoflow/axosyslog/pull/396),
    [#397](https://github.com/axoflow/axosyslog/pull/397),
    [#400](https://github.com/axoflow/axosyslog/pull/400),
    [#421](https://github.com/axoflow/axosyslog/pull/421),
    [#426](https://github.com/axoflow/axosyslog/pull/426),
    [#428](https://github.com/axoflow/axosyslog/pull/428),
    [#429](https://github.com/axoflow/axosyslog/pull/429),
    [#430](https://github.com/axoflow/axosyslog/pull/430),
    [#432](https://github.com/axoflow/axosyslog/pull/432),
    [#436](https://github.com/axoflow/axosyslog/pull/436),
    [#437](https://github.com/axoflow/axosyslog/pull/437),
    [#446](https://github.com/axoflow/axosyslog/pull/446),
    [#448](https://github.com/axoflow/axosyslog/pull/448),
    [#452](https://github.com/axoflow/axosyslog/pull/452),
    [#453](https://github.com/axoflow/axosyslog/pull/453),
    [#467](https://github.com/axoflow/axosyslog/pull/467),
    [#468](https://github.com/axoflow/axosyslog/pull/468),
    [#469](https://github.com/axoflow/axosyslog/pull/469),
    [#470](https://github.com/axoflow/axosyslog/pull/470),
    [#471](https://github.com/axoflow/axosyslog/pull/471),
    [#472](https://github.com/axoflow/axosyslog/pull/472),
    [#473](https://github.com/axoflow/axosyslog/pull/473),
    [#474](https://github.com/axoflow/axosyslog/pull/474),
    [#476](https://github.com/axoflow/axosyslog/pull/476),
    [#491](https://github.com/axoflow/axosyslog/pull/491))

  * `strftime()`: Added new filterx function to format datetimes.

    Example usage:
    ```
    $MSG = strftime("%Y-%m-%dT%H:%M:%S %z", datetime);
    ```

    Note: `%Z` currently does not respect the datetime's timezone,
    usage of `%z` works as expected, and advised.
    ([#402](https://github.com/axoflow/axosyslog/pull/402))

  * `keys()`: Add keys Function to Retrieve Top-Level Dictionary Keys

    This feature introduces the keys function, which returns the top-level keys of a dictionary. It provides a simple way to inspect or iterate over the immediate keys without manually traversing the structure.

    - **Returns an Array of Keys**: Provides a list of dictionary keys as an array.
    - **Current Level Only**: Includes only the top-level keys, ignoring nested structures.
    - **Direct Index Access**: The resulting array supports immediate indexing for quick key retrieval.

    **Example**:

    ```python
        dict = {"foo":{"bar":{"baz":"foobarbaz"}},"tik":{"tak":{"toe":"tiktaktoe"}}};
        # empty dictionary returns []
        empty = keys(json());

        # accessing the top level results ["foo", "tik"]
        a = keys(dict);

        # acccessing nested levels directly results ["bar"]
        b = keys(dict["foo"]);

        # directly index the result of keys() to access specific keys is possible (returns ["foo"])
        c = keys(dict)[0];
    ```
    ([#435](https://github.com/axoflow/axosyslog/pull/435))

  * Added support for switch cases.

    This syntax helps to organize the code for multiple
    `if`, `elif`, `else` blocks and also improves
    the branch finding performance.

    Cases with literal string targets are stored in a map,
    and the lookup is started with them.

    Other case targets can contain any expressions,
    and they are evaluated in order.

    Please note that although literal string and default
    target duplications are checked and will cause init failure,
    non-literal expression targets are not checked, and only
    the first maching case will be executed.

    Example config:
    ```
    switch ($MESSAGE) {
      case "foobar":
        $MESSAGE = "literal-case";
        break;
      case any_expression:
        $MESSAGE = "variable-case";
        break;
      default:
        $MESSAGE = "default";
        break;
    };
    ```
    ([#473](https://github.com/axoflow/axosyslog/pull/473))

  * `vars()`: add `exclude_msg_values` parameter
    ([#505](https://github.com/axoflow/axosyslog/pull/505))

  * `vars()`: `$` is now prepended for the names of message variables.
    ([#393](https://github.com/axoflow/axosyslog/pull/393))

  * `regex_search()`: Function Reworked

    The `regex_search()` function has been updated to simplify behavior and enhance configurability:

    - **Consistent Return Type**:
      The legacy behavior of changing the return type (`dict` or `list`) based on the presence of named match groups has been removed. The function now always returns a `dict` by default.

    - **Override with `list_mode`**: Use the `list_mode` optional named argument flag to explicitly return a `list` of match groups instead.

        **Example**:
        ```python
        result = regex_search("24-02-2024", /(?<date>(\d{2})-(\d{2})-(\d{4}))/)
        result = regex_search("24-02-2024", /(?<date>(\d{2})-(\d{2})-(\d{4}))/, list_mode=True)
        ```

    - **Result Type from Existing Objects**:
      If `result` is an existing `filterx` object with a specific type (`dict` or `list`), the function respects the type of the object, independent of the `list_mode` flag.

    - **Match Group 0 Handling**:
      Match group `0` is now excluded from the result by default (since it is rarely used), unless it is the only match group. To include match group `0` in the result, use the `keep_zero` optional named argument flag.

        **Example**:
        ```python
        result = regex_search("24-02-2024", /(?<date>(\d{2})-(\d{2})-(\d{4}))/, keep_zero=True)
        ```
    ([#399](https://github.com/axoflow/axosyslog/pull/399))

  * Metrics for FilterX expression execution

    Metrics `syslogng_fx_*_evals_total` are available on `stats(level(3))`.
    They can be used to gain insight on how FilterX expressions are executed on
    different messages and paths and to find potential bottlenecks.
    ([#398](https://github.com/axoflow/axosyslog/pull/398))

  * `=??` assignment operator

    Syntactic sugar operator, which could slightly improve performance as well.

    It can be used to assign a non-null value to the left-hand side.
    Evaluation errors from the right-hand side will be suppressed.

    For example,

    `resource.attributes['service.name'] =?? $PROGRAM;` can be used instead of:

    ```
    if (isset($PROGRAM)) {
      resource.attributes['service.name'] = $PROGRAM;
    };
    ```
    ([#395](https://github.com/axoflow/axosyslog/pull/395))

  * `regex_subst()`: Function Reworked

    The `regex_subst()` function has been updated to enhance functionality:

    - **Extended Match Group Support**:
      Replacement strings can now resolve match group references up to 999 groups.

    - **Optional Disabling**:
      The feature can be disabled using the `groups` named argument flag.

    - **Leading Zero Support**:
      Match group references with leading zeros (e.g., `\01`, `\002`) are now correctly interpreted. This prevents ambiguity when parsing group IDs, ensuring that shorter IDs like `\1` are not mistakenly interpreted as part of larger numbers like `\12`.

    **Example**:

    ```python
    result = regex_subst("baz,foo,bar", /(\w+),(\w+),(\w+)/, "\\2 \\03 \\1")

    # Force disable this feature
    result = regex_subst("baz,foo,bar", /(\w+),(\w+),(\w+)/, "\\2 \\03 \\1", groups=false)

    # Handling leading zeros
    result = regex_subst("baz,foo,bar", /(\w+),(\w+),(\w+)/, "\\0010") # returns `baz0`
    ```
    ([#409](https://github.com/axoflow/axosyslog/pull/409))

  * `set_fields()`: Added new function to set a dict's fields with overrides and defaults.

    A recurring pattern in FilterX is to take a dict and set multiple
    fields in it with overrides or defaults.

    `set_fields()` takes a dict as the first argument and `overrides`
    and `defaults` as optional parameters.

    `overrides` and `defaults` are also dicts, where the key is
    the field's name, and the value is either an expression, or
    a list of expressions. If a list is provided, each expression
    will be evaluated, and the first successful, non-`null` one will
    be used to set the respective field's value. This is similar to
    chaining null-coalescing (`??`) operators, but is more performant.

    `overrides` are always processed for each field. The `defaults`
    for a field are only processed, if the field does not already
    have a value set.

    Example usage:
    ```
    set_fields(
      my_dict,
      overrides={
        "foo": [invalid_expr, "foo_override"],
        "baz": "baz_override",
        "almafa": [invalid_expr_1, null],  # No effect
      },
      defaults={
        "foo": [invalid_expr, "foo_default"],
        "bar": "bar_default",
        "almafa": "almafa_default",
        "kortefa": [invalid_expr_1, null],  # No effect
        }
    );
    ```
    ([#397](https://github.com/axoflow/axosyslog/pull/397))

  * `metrics_labels()`: Added a new dict-like type to store metric labels directly.

    This dict converts the key-values to metric labels on the spot,
    so when it is used in multiple `update_metric()` function calls,
    no re-rendering takes place, which greatly improves performance.

    The stored labels are sorted alphabetically.

    Be aware, that this is a list of key-value pairs, meaning key collisions
    are not detected. Use the `dedup_metrics_labels()` function to deduplicate
    labels. However, this takes CPU time, so if possible, make sure not to
    insert a key multiple times so `dedup_metrics_labels()` can be omitted.
    ([#365](https://github.com/axoflow/axosyslog/pull/365))

  * `unset_empties()`: change the default for ignorecase to FALSE, remove utf8
    support.  UTF8 validation and case folding is expensive and most use-cases
    do not really need that. If there's a specific use-case, an explicit utf8
    flag can be added back.
    ([#452](https://github.com/axoflow/axosyslog/pull/452))

  * `load_vars()`: Added new function to load variables from a dict.

    Inverse of `vars()`.

    Note: FilterX level variables are loaded and `declare`d.
    ([#393](https://github.com/axoflow/axosyslog/pull/393))


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
