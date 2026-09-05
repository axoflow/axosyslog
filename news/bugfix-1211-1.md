`s3`: fixed a crash on shutdown when a multipart upload could not be completed

The destination crashed with an `AssertionError` when finishing an S3 object whose multipart upload had
been started but had no successfully uploaded parts, for example when the first part upload failed with a
client error. Such an upload is now aborted instead of triggering the assertion.
