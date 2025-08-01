`parse_cef()`, `parse_leef()`: Renamed some parsed header fields.

**These are breaking changes**

The motivation behind the renaming is that these names were too
generic, and there is a chance to find them in the extensions.

`parse_cef()`:
  * `version` -> `cef_version`
  * `name` -> `event_name`

`parse_leef()`:
  * `version` -> `leef_version`
  * `vendor` -> `vendor_name`
  * `delimiter` -> `leef_delimiter`
