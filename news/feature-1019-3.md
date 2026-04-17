FilterX: Added `utf8_validate()` and `utf8_sanitize()` string functions.

* `utf8_validate()` checks whether the string contains valid UTF-8 sequences and returns a boolean
* `utf8_sanitize()` replaces invalid byte sequences with their `\xNN` escaped representation
