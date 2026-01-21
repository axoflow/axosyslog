FilterX `move()` function: tells filterx that the variable/expression
specified as argument can be moved to its new location, instead of copying
it. While FilterX optimizes most copies using its copy-on-write mechanism,
some cases can be faster by telling it that the old location is not needed
anymore. `move()` is equivalent to an `unset()` but is more explicit and
returns the moved value, unlike `unset()`.
