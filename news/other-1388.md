`memory`: Cloned messages are now also freed in the thread that allocated them, which improves throughput when log
paths fork and modify messages in their branches.
