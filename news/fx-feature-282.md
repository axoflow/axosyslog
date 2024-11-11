`parse_windows_eventlog_xml()`: Added a new function to parse Windows EventLog XMLs.

This parser is really similar to `parse_xml()` with
a couple of small differences:

1. There is a quick schema validation.
2. The `Event`->`EventData` field automatically handles named `Data` elements.

