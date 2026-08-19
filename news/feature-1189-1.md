`syslog-ng`: added a new `--print-ast` command line option, which parses the configuration, prints its abstract
syntax tree to the standard output as JSON and exits, without initializing the configuration. The dump lists every
root level log expression (`source`, `destination`, `filter`, `parser`, `rewrite` and `log` statements) in source
order, the expression tree of each one and the parsed `filterx` expressions inside them, so that a generated
configuration can be validated against what the parser actually built.
