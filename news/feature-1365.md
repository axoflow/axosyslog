`memory queues`: Improved message latency and performance of memory queues
with a log-fetch-limit() value larger than 1024.  The handover between
source and destination threads is now triggered once the batch reaches 1024
messages.  This change has two effets, 1) it reduces latency, and 2) it
limits the amount of memory (and thus cache) used by a single batch. This
means the batch fits completely in the cache, significantly improving
performance of the destination thread.
