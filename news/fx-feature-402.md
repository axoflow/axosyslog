`strftime()`: Added new filterx function to format datetimes.

Example usage:
```
$MSG = strftime("%Y-%m-%dT%H:%M:%S %z", datetime);
```

Note: `%Z` currently does not respect the datetime's timezone,
usage of `%z` works as expected, and advised.
