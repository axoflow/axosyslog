`http()`: relaxed the `worker-partition-key()` requirement for message-independent URL templates

The partition-key check previously fired for any `url()` containing a `$` sign, even when the template was
effectively constant (for example `$(url-encode literal)` after SCL backtick substitution). The check now walks the
compiled template and only requires `worker-partition-key()` if the `url()` or `body-prefix()` is genuinely
message-dependent.
