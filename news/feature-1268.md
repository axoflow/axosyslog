`lumberjack-proto`: added the Lumberjack (Beats) protocol receiver, `transport(lumberjack())` on the `network()` source

Lumberjack is the protocol Filebeat, the other Beats and Logstash speak.

Both protocol versions (v1 and v2) are accepted, and so are compressed
frames.  Every event lands in `$MESSAGE` as the JSON the sender wrote,
untouched by the syslog parser, and `${.lumberjack.version}` names the
protocol version it arrived in, so a `json-parser()` or filterx picks the
fields up from there.

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
  * max-inflated-size(67108864): the most bytes a single compressed frame may inflate to, 0 for unlimited

Normal options of the `network()` driver also apply: `tls()` runs TLS from the first octet as the protocol has it,
`log-msg-size()` bounds a single event, `idle-timeout()` bounds the wait for the next window.
