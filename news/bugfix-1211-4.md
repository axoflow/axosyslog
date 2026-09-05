`s3`: fixed crashes and a race between message delivery and the periodic flush

Delivering a message to an object that the flush timer finished at the same moment raised an unhandled
exception, and the timer and the delivery path accessed the shared object bookkeeping without a lock,
which could abort the flush timer with a dictionary iteration error. The two paths are now
synchronized, the flush timer no longer runs after shutdown and a message racing with a flush is
retried instead of crashing.
