`kafka()`: Added the `kafka-logging()` option to control what happens with librdkafka's own log messages.
`kafka` (the default) forwards them into syslog-ng's log: errors and warnings are always logged, notice and info
need `-v`, and debug needs `-d`. `disabled` turns librdkafka's logging off, `trace` logs every line at trace level.
