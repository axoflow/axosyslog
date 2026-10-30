`parallelize()`: Fixed a crash with `file()`, `program()`, `kafka()`, `opentelemetry()` and the HTTP based sources.

AxoSyslog crashed as soon as the first message arrived when one of these sources was followed by
`parallelize()` with `workers()` set to 1 or more.
