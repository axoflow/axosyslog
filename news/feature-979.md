`get_timezone_source()`: new filterx function

Returns a string indicating where the timezone information originates from.
Possible values:
 - `"parsed"`: the timezone info is explicitly parsed from the timestamp
 - `"assumed"`: the timestamp didn't contain timezone info, default used (currently local timezone)
 - `"fixed"`: the timezone info was set using `fix_timezone()` or `set_timezone()` functions
 - `"guessed"`: the timezone info was set using `guess_timezone()` function

Example usage:
```
if (get_timezone_source(timestamp) === "assumed") {
  timestamp = fix_timezone(timestamp, "CET");
};
```
