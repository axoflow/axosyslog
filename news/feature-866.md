New metrics: `syslogng_output_workers` and `syslogng_output_active_worker_partitions`

Using the new `worker-partition-autoscaling(yes)` option allows producing partition metrics, which can be used
for alerting: if the number of active partitions remains higher than the configured number of workers,
it may indicate that events are not being batched properly, which can lead to performance degradation.
