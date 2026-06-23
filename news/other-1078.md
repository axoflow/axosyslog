`parallelize()`: improve throughput by avoiding batches that are too large.
Also set the default batch-size() parameter to 100, both in case of
partition based and round robin batching.
