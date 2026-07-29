`http()`: errors reported by a response-adapter() are now retried as many
times as retries() allows and the batch is dropped afterwards.

`splunk-hec-event()`: the destination now enables response-adapter(splunk)
by default. When Splunk rejects an event of a batch, the offending message
is logged and the batch is retried. Other HTTP errors keep their previous
handling.
