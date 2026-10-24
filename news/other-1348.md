`sources`: Lowered the per-message overhead of `$HOST_FROM` resolution.
The resolved peer hostname is now reused within the same second, and resolving it no longer forces an extra
clock read for every message.
