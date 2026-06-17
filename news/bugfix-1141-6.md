`filterx`: fix stack overflow in `unset_empties()` on very wide or deeply nested input

The keys-to-unset list was a stack array sized by the dict, so a very wide dict
overflowed the stack, and recursion into nested dicts was unbounded. The buffer is now
bounded and falls back to the heap for wide dicts, and the recursion depth is capped.
