`google-pubsub()`, `logscale()`, `openobserver()`, `splunk()`, and other batching destinations: fix slowdown

The default value of `batch-timeout()` is now 0.
This prevents artificial slowdowns in destinations when flow control is enabled
and the `log-iw-size()` option of sources is set to a value lower than `batch-lines()`.

If you enable `batch-timeout()`, you can further improve batching performance,
but you must also adjust the `log-iw-size()` option of your sources accordingly:

`log-iw-size / max-connections >= batch-lines * workers`
