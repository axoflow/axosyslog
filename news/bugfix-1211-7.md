`s3`: fixed messages being written to an object that was already finished

When the periodic flush finished an object right after it had closed a chunk, the object stayed
writable. The next message opened a new part on it that was never uploaded, and the destination
logged "Part uploads still pending" in a loop until shutdown. Such a message is now retried on a new
object instead.
