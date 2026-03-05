`fix-timestamp()`, `guess_timestamp()` and `set_timestamp()`: new filterx functions.

Example usage:
```
datetime = strptime("2000-01-01T00:00:00 +0200", "%Y-%m-%dT%H:%M:%S %z");
timezone = "CET";
fixed_datetime = fix_timezone(datetime, timezone);
guessed_datetime = guess_timezone(datetime);
set_datetime = set_timezone(datetime, "CET");
```
