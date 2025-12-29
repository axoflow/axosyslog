Memory queues: reduce the amount of memory we use to store queued messages
around. Savings can be as much as 50%, or 8GB less memory use to represent
1M messages (e.g. 8GB instead of 15GB, as measured by RSS).
