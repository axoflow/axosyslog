`opentelemetry()` destination: Fixed a crash when a batch held more than one signal type, for example a log and a
metric. The batch reused one gRPC client context for its logs, metrics and traces requests, which gRPC does not
allow, so the process aborted on the second request.
