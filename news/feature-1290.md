`syslog-ng --list-paths`: print the files and directories that the configuration refers to, without starting syslog-ng.
It reads the configuration given by `-f` (or the default one) and does not initialize it, so it does not
interfere with a running instance. The output has the same format as the output of `syslog-ng-ctl list-files`,
but it also lists directories, for example `ca-dir()`, `crl-dir()` and the disk-buffer `dir()`;
`syslog-ng-ctl list-files` lists only files.
It lists all files specified in the configuration, regardless of their availability. Sanitization on
consumer side is required.
