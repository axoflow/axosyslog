`syslog-ng-ctl stats prometheus`: show orphan metrics

Stale counters will be shown in order not to lose information, for example,
when messages are sent using short-lived connections and metrics are scraped in
minute intervals.

We recommend using `syslog-ng-ctl stats --remove-orphans` during each configuration reload,
but only after the values of those metrics have been scraped by all scrapers.
