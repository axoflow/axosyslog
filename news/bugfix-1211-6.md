`s3`: fixed a hang on reload and shutdown after a failed part upload

A part upload that had to be retried was queued behind the task that completes the multipart upload,
and that task waited for the part uploads to finish while occupying one of the `upload-threads()`.
Once every thread waited this way, no retry could run, the affected objects were never uploaded and
`syslog-ng` never finished its reload or shutdown. The multipart upload is now completed by the last
part upload that finishes, so no upload thread waits for another one.
