`basicfuncs`: Added the `$(bucket)` template function.

`$(bucket <bucket-count> value...)` hashes its value arguments and returns a
deterministic bucket index in `[0, bucket-count)`, so a `filter { "$(bucket
...)" == N }` inside a `channel` can route messages down one of several log
paths based on message content (e.g. sharding by `$HOST`). The `--weights
W1,W2,...` option replaces the plain bucket count to give buckets unequal
shares, including a weight of `0` to drain a bucket entirely. The bucket count,
the number of weights and each weight are limited to 4096.
