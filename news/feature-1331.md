`stats`: Added the `tag-stats(yes|no|auto)` option to the `stats()` block to control the per-tag counters, like
`syslog-stats()`. `auto` keeps registering them at `stats(level(3))`, `yes` registers them regardless of the level and
`no` turns them off.
