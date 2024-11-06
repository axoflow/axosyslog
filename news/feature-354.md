`clickhouse()`: Added a new destination that sends logs to ClickHouse.

The `clickhouse()` destination uses ClickHouse's [gRPC](https://clickhouse.com/docs/en/interfaces/grpc)
interface to insert logs.

Please note, that as of today, ClickHouse Cloud does not support
the gRPC interface. The `clickhouse()` destination is currently
only useful for self hosted ClickHouse servers.

If you would like to send logs to ClickHouse Cloud, gRPC support
can be requested from the ClickHouse Cloud team or a HTTP based
driver can be implemented in AxoSyslog.

Example config:
```
clickhouse(
  database("default")
  table("my_first_table")
  user("default")
  password("pw")
  schema(
    "user_id" UInt32 => $R_MSEC,
    "message" String => "$MSG",
    "timestamp" DateTime => "$R_UNIXTIME",
    "metric" Float32 => 3.14
  )
  workers(4)
  batch-lines(1000)
  batch-timeout(1000)
);
```
