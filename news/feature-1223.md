`elasticsearch-bulk()`: Added a new source that implements the Elasticsearch Bulk API

Elastic Agent, Beats and other clients of the
[Elasticsearch Bulk API](https://www.elastic.co/docs/api/doc/elasticsearch/operation/operation-bulk)
can send their events to AxoSyslog by pointing their Elasticsearch output at this source. The document
of each index and create action becomes a log message and its action line is stored in `${.es_bulk.action}`.
Update and delete actions are acknowledged and skipped.

Options:
  * `auth-token()`: the full value of the `Authorization` header the clients send, for example `"ApiKey <key>"`
  * `version()`: the Elasticsearch version reported to the clients, defaults to `8.15.0`; Elastic clients refuse
    a version older than their own
  * the network and TLS options of `ehttp()`

The source answers the version and license probes of the Elastic clients and accepts gzip and deflate
compressed requests.

Example configuration:

```
source s_es {
  elasticsearch-bulk(
    port(9200)
    auth-token("ApiKey <key>")
    version("8.15.0")
  );
};
```
