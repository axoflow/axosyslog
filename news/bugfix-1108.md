`java/hdfs`: fix unreleased lock in `send()` when file open fails

If `getHdfsFile()` returned `null`, the lock acquired at the start of
`send()` was never released, causing a permanent deadlock on all
subsequent calls.
