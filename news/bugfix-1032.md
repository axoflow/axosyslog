`opentelemetry()` source: Fixed `$SOURCEIP` for IPv6 peers.

Newer gRPC versions percent-encode the peer address, which broke
the source address extraction, so IPv6 clients showed up with the
`127.0.0.1` fallback address.
