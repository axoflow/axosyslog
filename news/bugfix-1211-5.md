`s3`: avoid creating duplicate multipart uploads for the same object

When several parts of a freshly opened object were uploaded concurrently, each upload could start its
own multipart upload. This left an orphaned upload in the bucket and could fail the object with an
invalid part error. Multipart upload creation is now serialized.
