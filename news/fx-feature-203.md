`regexp_subst()`: Added various pcre flags.

* `jit`:
  * enables or disables JIT compliling
  * default: `true`
* `global`:
  * sets whether all found matches should be replaced
  * default: `false`
* `utf8`:
  * enables or disables UTF-8 validation
  * default: `false`
* `ignorecase`
  * sets case sensitivity
  * default: `false` (case-sensitive)
* `newline`
  * configures the behavior of end of line finding
  * `false` returns end of line when CR, LF and CRLF characters are found
  * `true` makes the matcher process CR, LF, CRLF characters
  * default: `false`
