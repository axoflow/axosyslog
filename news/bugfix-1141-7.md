`filterx`: fix stack overflow when parsing deeply nested JSON

Deeply nested JSON passed to `json()` recursed once per nesting level with no limit and
could exhaust the stack. The nesting depth is now capped.
