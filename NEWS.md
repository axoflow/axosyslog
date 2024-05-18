4.99.99
=======

## Highlights

<Fill this block manually from the blocks below>

## Bugfixes

  * `csv-parser()`: fix escape-backslash-with-sequences dialect on ARM

    `csv-parser()` produced invalid output on platforms where char is an unsigned type.
    ([#4947](https://github.com/axoflow/axosyslog/pull/4947))


## Other changes

  * packages/dbld: add support for Ubuntu 24.04 (Noble Numbat)
    ([#4925](https://github.com/axoflow/axosyslog/pull/4925))

  * `syslog-ng-ctl`: do not show orphan metrics for `stats prometheus`

    As the `stats prometheus` command is intended to be used to forward metrics
    to Prometheus or any other time-series database, displaying orphaned metrics
    should be avoided in order not to insert new data points when a given metric
    is no longer alive.

    In case you are interested in the last known value of orphaned counters, use
    the `stats` or `query` subcommands.
    ([#4921](https://github.com/axoflow/axosyslog/pull/4921))

  * `s3()`: new metric `syslogng_output_event_bytes_total`
    ([#4958](https://github.com/axoflow/axosyslog/pull/4958))

  * `bigquery()`, `loki()`, `opentelemetry()`, `cloud-auth()`: C++ modules can be compiled with clang

    Compiling and using these C++ modules are now easier on FreeBSD and macOS.
    ([#4933](https://github.com/axoflow/axosyslog/pull/4933))


## Credits

AxoSyslog is developed as a community project, and as such it relies
on volunteers, to do the work necessary to produce AxoSyslog.

Reporting bugs, testing changes, writing code or simply providing
feedback is an important contribution, so please if you are a user
of AxoSyslog, contribute.

We would like to thank the following people for their contribution:

Arpad Kunszt, Attila Szakacs, Balazs Scheidler, Ferenc HERNADI,
Gabor Kozma, Hofi, Kristof Gyuracz, László Várady, Mate Ory, Máté Őry,
Robert Fekete, Szilard Parrag, Wolfram Joost, shifter
