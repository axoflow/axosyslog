New metrics:

- `syslogng_event_processing_latency_seconds{measurement_point="input/output"}`
  - Histogram of the latency from message receipt to full processing, from the source or destination perspective.
- `syslogng_output_event_latency_seconds`
  - Histogram of the latency from message receipt to delivery, from the destination perspective.

`output_event_delay_sample_seconds` has been removed in favor of `output_event_latency_seconds`.
