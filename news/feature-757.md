`parallelize()`: add `batch-size()` option and other perf improvements

The `batch-size()` option specifies, for each input thread, how many consecutive
messages should be processed by a single `parallelize()` worker.

This ensures that this many messages preserve their order on the output side
and also improves `parallelize()` performance. A value around 100 is recommended.
