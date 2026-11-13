`ehttp()`: The response to a request is now sent once every message of the request has been acknowledged in the
pipeline, instead of right after the messages were queued.

When the acknowledgement arrives depends on the log path and the destination. Without `flags(flow-control)` the
destination drivers acknowledge a message as they queue it, so the response timing of such paths is unchanged. With
flow-control a memory queue acknowledges once the message is delivered, so the HTTP response now follows the delivery.
A disk-buffer acknowledges when it stores the message, as before, except that a reliable one within its
`flow-control-window-bytes()` holds the acknowledgement until the delivery. A dropped message is acknowledged as
processed, so its request still gets an OK response.

A client with a short deadline may time out while the delivery is held back by backpressure and resend the request,
which produces duplicates at the destination. The same happens to a request still waiting when AxoSyslog stops: its
connection is closed, while the messages stay queued and may still be delivered. Through a reload the request keeps
waiting. A request whose messages were discarded without an acknowledgement, for example when a reload removed their
destination, or whose acknowledgement does not arrive within `idle-timeout()` (900 seconds by default) is answered with
`503 Service Unavailable` and its connection is closed.
