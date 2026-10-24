`network()`, `syslog()` and `file()`: Significantly reduce the overhead of
per message statistics, improving performance by about 15-20%.  Message
sizes are now recorded in histograms, showing more detailed statistics about
the message size distribution.  You can find these histograms in the
new-style metrics `input_event_size_bytes` for sources and the
`output_event_size_bytes` for destinations.

NOTE: The legacy `msg_size_max` and `msg_size_avg` stat counters were moved
to `stats(level(2))`.
