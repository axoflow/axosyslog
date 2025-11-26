`http()` and other threaded destinations: add `worker-partition-autoscaling(yes)`

When `worker-partition-key()` is used to categorize messages into different batches,
the messages are - by default - hashed into workers, which prevents them from being distributed across workers
efficiently, based on load.

The new `worker-partition-autoscaling(yes)` option uses a 1-minute statistic to help distribute
high-traffic partitions among multiple workers, allowing each worker to maximize its batch size.

When using this autoscaling option, it is recommended to oversize the number of workers: set it higher than the
expected number of partitions.
