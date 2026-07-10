`filterx`: add the `uuid7()` function

`uuid7()` generates RFC 9562 UUIDv7 identifiers, which embed a millisecond-precision Unix timestamp, so
they sort lexically by creation time.

For consistency with `uuid7()`, `uuid()` now has an alias: `uuid4()`.
