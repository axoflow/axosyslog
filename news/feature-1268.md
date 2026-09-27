`lumberjack-proto`: added the Lumberjack (Beats) protocol receiver, `transport(lumberjack())` on the `network()` source

Lumberjack is the protocol Filebeat, the other Beats and Logstash speak: a sender announces a window of N
events, sends them as JSON frames numbered 1..N, usually inside one zlib-compressed frame, and waits for a
single acknowledgement of the whole window.  Both protocol versions are accepted, and so are compressed frames,
so Beats work with their default `compression_level: 3`; the key/value pairs of a version 1 frame arrive as a
flat JSON object.
Every event lands in `$MESSAGE` as the JSON the sender wrote, untouched by the syslog parser, and
`${.lumberjack.version}` names the protocol version it arrived in, so a `json-parser()` or filterx picks the
fields up from there.

A window is acknowledged once its last event was acknowledged by the pipeline: with `flags(flow-control)` and a
`disk-buffer()` downstream the acknowledgement means stored, without flow control it means received, which is
what Logstash does.  A slow pipeline is never a reason to drop the connection, the receiver keeps the sender
waiting with keepalives instead.  An event larger than `log-msg-size()` is dropped and still counted toward the
acknowledgement, so a sender does not resend the same window forever; the limit applies to the JSON as delivered,
so a version 1 frame that only outgrows it once escaped is dropped as well.  Sequence numbers are not enforced: a
sender that numbers its frames from a counter running across windows (logstash-forwarder, ferro-lumberjack)
is acknowledged with the last sequence number it sent, which is what it expects.

Example:
    source s_beats {
      network(port(5044) transport(lumberjack(max-window-size(2048)))
              tls(key-file("/etc/syslog-ng/beats.key") cert-file("/etc/syslog-ng/beats.crt")
                  peer-verify(optional-untrusted)));
    };
    log { source(s_beats); destination(d_buffered); flags(flow-control); };

Options of `lumberjack()`:
  * max-window-size(10000): the largest window accepted, 0 for unlimited; align it with the sender's batch size
  * keepalive-interval(5): seconds between keepalives while a window waits for its acknowledgement, 0 disables
  * window-timeout(30): seconds a window may stall mid-way before the connection is closed, 0 disables

Normal options of the `network()` driver also apply: `tls()` runs TLS from the first octet as the protocol has it,
`log-msg-size()` bounds a single event, `idle-timeout()` bounds the wait for the next window.
