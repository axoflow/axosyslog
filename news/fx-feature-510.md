`set_timestamp()`: Added new filterx function to set the message timestamps.

Example usage:
```
set_timestamp(datetime, stamp="stamp");
```

Note: Second argument can be "stamp" or "recvd", based on the timestamp to be set.
Default is "stamp".