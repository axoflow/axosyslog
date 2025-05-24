`log-flow-control(yes/no)` global option

This option allows enabling flow control for all log paths. When set to yes,
flow control is globally enabled, but it can still be selectively disabled
within individual log paths using the `no-flow-control` flag.

WARNING: Enabling global flow control can cause the `system()` source to block.
As a result, if messages accumulate at the destination, applications that log
through the system may become completely stalled, potentially halting their
operation. We don't recommend enabling flow control in log paths that
include the `system()` source.

For example,

```
options {
  log-flow-control(yes);
};

log {
  source { system(); };
  destination { network("server" port(5555)); };
  flags(no-flow-control);
};

log { ... };
```
