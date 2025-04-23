`stats()`: `freq()` now defaults to 0

Internal statistic messages were produced every 10 minutes by default.
Metrics are available through `syslog-ng-ctl`, we believe modern monitoring and
observability render this periodic message obsolete.
