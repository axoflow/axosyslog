`logmsg`: fix crash when iterating name-value registry concurrently

The hash table mapping value names to handles was iterated without locking, which could crash AxoSyslog
when another thread registered new name-value pairs at the same time. This happened for example when the
Python `LogMessage.keys()` method ran while a `kv-parser()` was processing messages on a parallel path.
