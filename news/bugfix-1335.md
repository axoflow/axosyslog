`file()`: Fixed `fsync(yes)` not syncing the file after a partially written batch, and fixed losing the messages a
`file()` writer had not written yet when it was freed.
