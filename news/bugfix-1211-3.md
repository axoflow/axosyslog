`s3`: fixed the `canned-acl()` option being rejected by the S3 API

The configured value was passed to the S3 API as a tuple instead of a string, which raised an unhandled
exception whenever `canned-acl()` was set.
