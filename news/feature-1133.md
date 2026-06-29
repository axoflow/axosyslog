`network()`, `tcp()`: honor `flush-lines()` by coalescing writes

The `network()` and `tcp()` destinations now coalesce up to `flush-lines()` formatted messages into a
single write instead of writing each message separately, cutting system call overhead and improving
throughput.

This covers stream and TLS transports. UDP and other datagram transports still write one message per
packet, and `syslog()` is unaffected because it frames each message individually.

`flush-lines()` previously had no effect on these destinations. Starting with configuration version
`@config: 4.26` it defaults to 100, enabling coalescing, while older configurations keep the previous
one-write-per-message behavior.
