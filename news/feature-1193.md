`filterx`: add the `aggregate()` function

`aggregate(key=, values=, timeout=, [close=], [aggregators=])` groups messages by `key` and merges each
message's `values` dict into a per-key running total, returning it once the group is closed explicitly (via
`close=`) or times out (via `timeout=`, in seconds). Per-field merge behavior is configurable through
`aggregators=`: `sum`, `count`, `min`, `max`, `average`, `replace`, plus `_as_number` variants of the numeric
ones that coerce non-numeric input (e.g. a string field) into a number instead of failing to merge past the
first message.

Typical uses: reassembling a Cisco ISE event that arrives as several syslog fragments back into a single
message, or squashing repeated firewall DROP log lines from the same retried connection attempt (e.g. TCP SYN
retransmits to a closed port) into one summary line with aggregated packet and byte counters.
