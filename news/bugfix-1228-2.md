`s3`: fixed an `AssertionError` when the flush timer finished an object during a write

The flush timer could close the chunk while a message was being written to it. The write then
failed with an assertion that was logged as an unhandled exception and the message was retried.
