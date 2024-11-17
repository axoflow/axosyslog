`metrics_labels()`: Added a new dict-like type to store metric labels directly.

This dict converts the key-values to metric labels on the spot,
so when it is used in multiple `update_metric()` function calls,
no re-rendering takes place, which greatly improves performance.

The stored labels are sorted alphabetically.

Be aware, that this is a list of key-value pairs, meaning key collisions
are not detected. Use the `dedup_metrics_labels()` function to deduplicate
labels. However, this takes CPU time, so if possible, make sure not to
insert a key multiple times so `dedup_metrics_labels()` can be omitted.
