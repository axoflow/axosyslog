`memory queues`: Lowered the CPU cost of acknowledging messages in the destination thread, and fixed a throughput drop
with a large `log-fetch-limit()`: the messages of a source are now handed over to the destination every 1024 messages,
not only at the end of a fetch batch.
