`internal()`: Added a `severity` label to the `syslogng_internal_events_total` metric.

One series per syslog severity (`err`, `warning`, `notice`, `info`, `debug`, ...) counts every message
syslog-ng logs about itself, next to the existing `result` series. A rise in the `err` or `warning` series marks
the minute syslog-ng started to log errors, also for failures that have no dedicated counter.
