`multi-line`: Added `multi-line-mode(timestamp)`, which starts a new event
at every line that begins with a timestamp and treats every other line as a
continuation, the similar to how a Splunk indexer breaks events by default.
Timestamps are recognized by the built-in scanners (ISO 8601, BSD syslog,
Cisco, Linksys) and by the `strptime(3)` formats listed in the new
`timestamp-multi-line.formats` file, which can be extended.  A timestamp may
be enclosed in one of the `multi-line-timestamp-pairs()` character pairs,
`"[]"` by default.  Available wherever `multi-line-mode()` is: `file()`,
`wildcard-file()` and `transport(splunk-s2s(...))`.

Example:

```
source {
  file("/var/log/app.log" multi-line-mode(timestamp) multi-line-timestamp-pairs("[]()"));
};
```
