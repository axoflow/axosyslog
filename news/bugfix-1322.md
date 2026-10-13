`syslogng_events_allocated_bytes`: Fixed the gauge wrapping below zero when messages are read back from a disk buffer.

Deserialization recomputed a message's byte tally without adding the difference to the gauge, and freeing the
message subtracted the whole tally, so every message replayed from disk pushed the gauge down by the uncounted part.
