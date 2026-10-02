`disk-buffer()`: Fixed a crash when a destination kept retrying the same message from a non-reliable disk-buffer.

Every retry allocated a new queue node for the message, and after 65535 retries AxoSyslog aborted with
`log_msg_alloc_queue_node: assertion failed: (cur_node <= LOGMSG_NODES_ASSERT)`.
