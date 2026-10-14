`disk-buffer`: Fixed losing messages from disk-buffer files written by syslog-ng or by AxoSyslog 4.11 and older.
A message that filled its name-value table exactly could not be read back, and in the memory part of a
non-reliable disk-buffer every message after it was lost too.
