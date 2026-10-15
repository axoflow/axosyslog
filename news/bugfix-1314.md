`filterx`: Fixed `set_timestamp()` and `set_pri()` leaking their change into sibling log paths and branches
when the message was shared between them.
