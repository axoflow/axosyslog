`basicfuncs`: Added a non-sticky, round robin mode to the `$(bucket)` template function.

`$(bucket <bucket-count>)` and `$(bucket --weights W1,W2,...)`, that is `$(bucket)` without
value arguments to hash, deal messages out to the buckets in turn, driven by a per-thread counter
that starts from zero, so each thread cycles through the buckets on its own (with `--weights`,
visiting each bucket as many times as its weight). The `--batch N` option (default: 100) keeps
`N` consecutive messages of a thread in the same bucket before moving on to the next one.
