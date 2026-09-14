`syslog-ng-ctl stop --force`: added a forced variant of the stop command, which terminates the process
without waiting for the worker threads and without tearing down the configuration.

Use it when a shutdown never completes because a worker never finishes its job, in which case both
`syslog-ng-ctl stop` and `syslog-ng-ctl reload` are ignored. In-memory queue contents are lost, disk-buffer
contents stay on disk. The process exits with status 1.
