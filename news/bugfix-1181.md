`filterx`: JSON parsing (`parse_json()`, `cache_json_file()`) no longer fails on JSON documents that need more
than 65536 jsmn tokens. The token array is now grown dynamically until parsing succeeds or memory is
actually exhausted, instead of being capped at a hardcoded token count.
