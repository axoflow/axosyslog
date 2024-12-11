4.99.0
======

## Highlights

<Fill this block manually from the blocks below>

## Features

  * `syslog()` source driver: add support for RFC6587 style auto-detection of
    octet-count based framing to avoid confusion that stems from the sender
    using a different protocol to the server.  This behaviour can be enabled
    by using `transport(auto)` option for the `syslog()` source.
    ([#4814](https://github.com/axoflow/axosyslog/pull/4814))

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

  * `google-pubsub-grpc()`: Added a new destination that sends logs to Google Pub/Sub via the gRPC interface.

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


## Bugfixes

  * `axosyslog-otlp()` destination: Fixed a crash.
    ([#384](https://github.com/axoflow/axosyslog/pull/384))


## FilterX features

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

  * `load_vars()`: Added new function to load variables from a dict.

    Inverse of `vars()`.

    Note: FilterX level variables are loaded and `declare`d.
    ([#393](https://github.com/axoflow/axosyslog/pull/393))

  * Metrics for FilterX expression execution

    Metrics `syslogng_fx_*_evals_total` are available on `stats(level(3))`.
    They can be used to gain insight on how FilterX expressions are executed on
    different messages and paths and to find potential bottlenecks.
    ([#398](https://github.com/axoflow/axosyslog/pull/398))

  * `vars()`: `$` is now prepended for the names of message variables.
    ([#393](https://github.com/axoflow/axosyslog/pull/393))

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

  * `strftime()`: Added new filterx function to format datetimes.

    Example usage:
    ```
    $MSG = strftime("%Y-%m-%dT%H:%M:%S %z", datetime);
    ```

    Note: `%Z` currently does not respect the datetime's timezone,
    usage of `%z` works as expected, and advised.
    ([#402](https://github.com/axoflow/axosyslog/pull/402))


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


