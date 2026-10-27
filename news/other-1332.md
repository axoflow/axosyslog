`memory`: Messages are now freed in the thread that allocated them, which improves throughput with several input
threads. The `syslogng_events_allocated_bytes` gauge (`msg_allocated_bytes`) is now only registered at
`stats(level(3))`, as updating it from every thread was a bottleneck.
