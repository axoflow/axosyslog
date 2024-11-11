`parse_cef()`, `parse_leef()`: Added CEF and LEEF parsers.

* The first argument is the raw message.
* Optionally `pair_separator` and `value_separator` arguments
  can be set to override the respective extension parsing behavior.

Example usage:
```
my_structured_leef = parse_leef(leef_message);
my_structured_cef = parse_cef(cef_message);
```
