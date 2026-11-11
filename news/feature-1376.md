`network()`, `syslog()`, `tcp()` destinations: Added the `server-name()` TLS option.

The server certificate is verified against this name instead of the address the destination connects to (also for
failover servers), so a destination can connect to an IP address or a load balancer and still verify the name of the
server. With `sni(yes)`, the name is also sent as SNI; an empty value means the option is not set.

```
destination { network("10.0.0.5" transport("tls") tls(ca-file("ca.crt") server-name("logs.example.com"))); };
```
