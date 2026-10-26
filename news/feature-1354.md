`logthrdest`: fill the first worker of a partition before spreading the messages over the others

With `worker-partition-autoscaling(yes)` a message now goes to the lowest worker of the partition's range whose queue
holds fewer than `batch-lines()` messages, and only a range where every worker is that full falls back to the previous
spread. A low message rate thus batches on one worker instead of timing out nearly empty batches on all of them.
Autoscaling without a `worker-partition-key()`, previously a no-op, applies the same fill-first routing to all workers.
