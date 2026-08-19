`filterx`: added a new `--filterx-only` command line option, which compiles the file specified with `--cfgfile` as a
standalone filterx script instead of a syslog-ng configuration, then exits. The entire file is parsed as filterx code,
as if its contents were the body of a `filterx {}` block, which makes it possible to validate filterx code without
wrapping it into a full configuration. Combined with `--print-ast`, the abstract syntax tree of the compiled script is
printed to the standard output as JSON.
