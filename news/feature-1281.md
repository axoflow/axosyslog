`syslog-ng --list-files`: print the files that the configuration refers to, without starting syslog-ng.
It reads the configuration given by `-f` (or the default one) and does not initialize it, so it does not
interfere with a running instance. The output is the same as the output of `syslog-ng-ctl list-files`.
