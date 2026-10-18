`@define`, `@include`: Fixed `include-path` and the other path globals being reset to their defaults.

A template using `$(if)` or `$(grep)` restored the default `include-path`, so every later `@include` glob silently
matched nothing. `scl.conf` contains such a template, so the includes placed after it were lost.
