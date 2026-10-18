FilterX: Added `format_sdata()`, which formats a dict of the form `{"SD-ID": {"PARAM": value}}` as RFC5424
structured data, escaped the same way as the `$SDATA` macro.

`format_syslog_5424()` got a new `sdata` argument. A string is used as the structured data verbatim, a dict is
formatted the way `format_sdata()` does it.

`format_syslog_5424()` no longer renders the message's own structured data on its own. Pass `sdata=$SDATA` to keep it.
