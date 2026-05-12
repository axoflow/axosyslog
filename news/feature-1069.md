`arrow-flight()`: Added a new destination for [Apache Arrow Flight](https://arrow.apache.org/docs/format/Flight.html)

Options:
  * `url()`: Flight endpoint
  * `path()`: descriptor path, templatable so a single destination can route to many tables
  * `schema()`: one `"name" TYPE => template` entry per column

Column types supported in `schema()`:
  * `STRING`
  * `INT64` (alias `INTEGER`)
  * `DOUBLE` (alias `FLOAT64`)
  * `BOOL` (alias `BOOLEAN`)
  * `TIMESTAMP`

Example configuration:

```
destination d_arrow {
  arrow-flight(
    url("grpc://flight.example.com:8815")
    path("events.${HOST}")
    schema(
      "ts"       TIMESTAMP => "$UNIXTIME"
      "host"     STRING    => "$HOST"
      "program"  STRING    => "$PROGRAM"
      "severity" INT64     => "$LEVEL_NUM"
      "msg"      STRING    => "$MSG"
    )
    batch-lines(1000)
    batch-bytes(1048576)
    batch-timeout(5000)
    workers(4)
    worker-partition-key("${HOST}")
  );
};
```
