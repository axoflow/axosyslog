`splunk-s2s`: Added multi-line support to the `transport(splunk-s2s)`
protocol: the lines of the raw chunks a universal forwarder sends are merged
into events per channel with `multi-line-mode()`, `multi-line-prefix()`,
`multi-line-garbage()` and a new `multi-line-timeout()` option, inside
`transport(splunk-s2s(...))`.

Example, breaking events at timestamps the way an indexer does by default:

```
source {
  network(port(9997) transport(splunk-s2s(
    multi-line-mode(timestamp)
    multi-line-timeout(5)
  )));
};
```
