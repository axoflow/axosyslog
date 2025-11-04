`opentelemetry()` source: add `keep-alive()` option

With this new option, client connections can be kept alive during reload,
avoiding unnecessary retry backoffs and other error messages on the client
side.

The default is `yes`.
