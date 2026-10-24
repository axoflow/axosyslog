`opentelemetry()` source: Fixed a crash at stop or after a reload.

AxoSyslog crashed at stop, or after a reload that changed or removed an `opentelemetry()` source, when the source
still had messages queued on a flow-controlled log path.
