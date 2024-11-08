`syslog-ng-ctl`: Added `attach` subcommand.

With `attach`, it is possible to attach to the
standard IO of the `syslog-ng` proccess.

Example usage:
```
# takes the stdio fds for 10 seconds and displays syslog-ng output in that time period
$ syslog-ng-ctl attach stdio --seconds 10
```
```
# steal trace level log messages for 10 seconds
$ syslog-ng-ctl attach logs --seconds 10 --level trace
```
