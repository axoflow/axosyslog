`splunk-hec()` source: added a Splunk HTTP Event Collector source

A new `splunk_hec()` source (an SCL wrapper around the `http()` source) listens on the
standard Splunk HEC endpoints (`/services/collector`, `/services/collector/event`,
`/services/collector/event/1.0`), parses the HEC JSON event format and promotes the
`event` field to the message, also extracting the `host` and `time` envelope fields.
Multiple events may be batched as newline-delimited JSON objects. Example:

```
source s_hec {
    splunk_hec(port(8088));
};
```
