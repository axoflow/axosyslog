`clickhouse-destination()`: new `json-var()` directive

The new `json-var()` option accepts either a JSON template or a variable containing a JSON string, and sends it to the ClickHouse server in Protobuf/JSON mixed mode (JSONEachRow format). In this mode, type validation is performed by the ClickHouse server itself, so no Protobuf schema is required for communication.

This option is mutually exclusive with `proto-var()`, `server-side-schema()`, `schema()`, and `protobuf-schema()` directives.

example:
```
   destination {
      clickhouse (
      ...
      json-var("$json_data");
or
      json-var(json("{\"ingest_time\":1755248921000000000, \"body\": \"test template\"}"))
      ...
      };
   };
```
