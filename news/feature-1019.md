FilterX: Added various crypto hash digest functions.

These functions compute the hash of a string or bytes and return the result as a hex string:
* `md5()`
* `sha1()`
* `sha256()`
* `sha512()`

The generic `digest(input, alg="sha256")` function accepts an optional algorithm
name and returns the raw hash as a bytes object.
