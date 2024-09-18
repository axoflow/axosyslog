`file()`, `wildcard-file()`: fix crash and persist name collision issues

If multiple `wildcard-file()` sources or a `wildcard-file()` and a `file()` source were reading the same input file,
it could result in log loss, log duplication, and various crashes.
