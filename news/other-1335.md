`network()`, `syslog()`, `file()`, `pipe()`, `program()`, `stdout()`: Lowered the CPU usage of destinations that write
formatted lines, by reusing their buffers and writing up to `flush-lines()` messages with a single write. `pipe()`,
`program()` and `stdout()` now batch their writes like `file()` does, instead of writing each message separately.
