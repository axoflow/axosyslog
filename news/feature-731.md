`http`: Added templating support to `body-prefix()`

In case of batching the templates in `body-prefix()` will be calculated
from the first message. Make sure to use `worker-partition-key()` to
group similar messages together.

Example usage:
```
http(
  ...
  body-prefix('{"log_type": "$log_type", "entries": [')
  body('"$MSG"')
  delimiter(",")
  body-suffix("]}")

  batch-lines(1000)
  batch-timeout(1000)
  worker-partition-key("$log_type")
);
```
