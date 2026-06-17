`filterx`: fix crash when moving a key out of an empty dict

Moving a key from an empty dict dereferenced its unallocated backing table. It now
reports the key as missing instead of crashing.
