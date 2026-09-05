`logproto`: replaced the handshake phase of `LogProtoServer` with a proto replacement from `fetch()`

A server proto that wants to hand its work over to another one (transport
auto-detection is the only such case today) now sets the new
`proto_replacement` member during `fetch()` and returns `LPS_AGAIN` without
a message.

The previous half-broken LogProtoClient/LogProtoServer handshake()
mechanisms are now gone.
