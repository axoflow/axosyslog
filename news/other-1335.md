`network()`, `syslog()`, `file()`: Lowered the CPU usage of destinations that write formatted lines, by reusing their
buffers, sending a batch with a single write and caching the formatted timestamp. The destination thread uses ~30% less
CPU per message.
