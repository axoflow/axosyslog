`opentelemetry-dest`: added a `prefix()` option, which tells the driver where to read the outgoing OTLP payload from,
replacing the default `.otel_raw` name-value prefix. This lets a configuration hand the destination a record it
assembled itself, for example with `${.axo.log} = otel_logrecord(...)` and `prefix(".axo")`.

The prefix must start with a `.`; a trailing dot is optional, so `prefix(".axo")` and `prefix(".axo.")` are equivalent.
The driver reads `<prefix>.type` (`log`, `metric` or `span`) plus `<prefix>.log` / `<prefix>.metric` / `<prefix>.span`,
`<prefix>.resource`, `<prefix>.scope` and the two `<prefix>.{resource,scope}_schema_url` pairs. The per-field
`.otel.*` fallback namespace is not affected, and `axosyslog-otlp()` does not accept the option.
