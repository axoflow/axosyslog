`stats`: Lowered the per-message overhead of statistics and of `$HOST_FROM` resolution in sources and destinations.
Message counters and the `msg_size_*` metrics are now updated once per read batch or flush, so they lag behind by at
most a few milliseconds.
