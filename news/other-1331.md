`memory queues`, `file()`: Improved the throughput of configurations with several input threads by removing contention
between the input and output threads in memory queues, message tags and statistics, and by writing `file()`
destinations without locking when a single thread writes them. With 4 connections into one destination this is up to
12% faster.
