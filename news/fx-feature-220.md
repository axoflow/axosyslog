`update_metric()`: Added a new function similar to `metrics-probe` parser.

Example usage:
```
update_metric("filterx_metric", labels={"msg": $MSG, "foo": "foovalue"}, level=1, increment=$INCREMENT);
```
