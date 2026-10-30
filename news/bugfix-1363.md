`opentelemetry()` source: Fixed the loss of `schema_url` in `mode(filterx-dict)`. The resource and scope
`schema_url` are now available as the `resource_schema_url` and `scope_schema_url` string variables, next to
`resource`, `scope` and `log`. They are set even if empty.
