`http()` and other threaded destinations: add `batch-idle-timeout()` option

While `batch-timeout()` defines the maximum amount of time a batch may wait before it is sent (starting from the first message),
the new `batch-idle-timeout()` measures the elapsed time since the last message was added to the batch.

Whichever timeout expires first will close and send the batch.

`batch-idle-timeout()` defaults to 0 (disabled).
