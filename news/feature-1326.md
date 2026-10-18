`syslog-ng-ctl reload`: report configuration errors that cause the reload to
fail, both syntax and initialization errors are reported.

Example syntax error:

```
$ syslog-ng-ctl  reload
Syntax error parsing configuration file, previous config remained intact

Error parsing log statement: syntax error, unexpected LL_IDENTIFIER, expecting '}'

In /etc/syslog-ng.conf:9:2-9:6:
4
5       log {
6
7       	source { stdin(); };
8       	destination { network("0" port(1234) disk-buffer(capacity-bytes(1MiB))); };
9-----> 	well
9-----> 	^^^^
10      };


```

Example initialization error:
```
$ syslog-ng-ctl  reload
Config reload failed, reverted to previous config

Error binding socket; addr='AF_INET(99.99.99.99:514)', error='Cannot assign requested address (99)'
Error initializing message pipeline; plugin_name='tcp', location='etc/syslog-ng-diskq.conf:8:11'
Error initializing new configuration, reverting to old config;
```
