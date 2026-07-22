`network()` and `syslog()` destinations: improve performance of the
spoof-soure(yes) feature by drastically improving scalability in this case.
The sending of packets is now running in a destination thread (instead of
piggybacking it to the source thread), which also allowed to eliminate the
per-destination lock protecting the send operation.
