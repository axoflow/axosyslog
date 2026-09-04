`update_metric()`: added a new `set` parameter, which assigns an absolute value
to the metric instead of incrementing it, making the function usable for
gauge-like metrics:

    update_metric("demo_gauge", set=int($MSG), labels={"host": "demo"});

`set` and `increment` are mutually exclusive, specifying both is a configuration
error. The value must be non-negative. Negative values are rejected at evaluation
time and the metric is left unchanged.
