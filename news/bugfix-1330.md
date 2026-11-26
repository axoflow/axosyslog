`mainloop`: start the dedicated source/destination threads only after the configuration is fully initialized.

The worker threads of the threaded drivers were started from `cfg_init()`, before the configuration switch, the
application hooks and the stats registrations were done, so their first messages raced with the initialization:
the message memory stat could start negative and internal messages emitted by the new threads at startup were lost.
