`format_syslog_5424()`: Added new FilterX function for syslog formatting

Usage:
```
format_syslog_5424(
  message,
  add_octet_count=false,
  pri=expr,
  timestamp=expr,
  host=expr,
  program=expr,
  pid=expr,
  msgid=expr
)
```

`message` is the only mandatory argument.

Fallback values will be used instead of the named arguments
if they are not set, or their evaluation fails.
