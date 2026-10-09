`disk-buffer`: `dqtool info` and the "Reliable disk-buffer state" log line now count the backlog records of a
reliable disk-buffer file in `number_of_messages`. These records were handed to the destination but not acked
yet, so they are still in the file and are delivered again after a restart.
