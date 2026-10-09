`light`: add a crash-recovery test for the reliable disk-buffer

`SyslogNg.kill()` takes syslog-ng down with SIGKILL, and the new test checks that a restart replays the backlog
a killed instance left in its reliable disk-buffer.
