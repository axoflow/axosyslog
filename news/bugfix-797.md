`s3`: Bugfixes and general stability improvements for the `s3` destination driver

Refactored the python `s3` destination driver to fix a major bug causing data loss if multithreaded upload was
enabled via the `upload-threads` option.

This pull request also
* Adds a new suffix option to the `s3` destination driver. The default suffix is `.log`, denoting file extension.
* Removes more than 600 lines of superfluous code.
* Brings major stability improvements to the `s3` driver.
* Fixes bug where in certain conditions finished object buffers would fail to upload.

**Important:**
* This change affects the naming of multipart objects, as the sequence index is moved in front of the suffix.
* The `upload-threads` option is changed to act on a per-object basis, changing the maximum thread count
dependent on `max-pending-uploads * upload-threads`.

`s3`: Added two new options

* `content-type()`: users now can change the content type of the objects uploaded by syslog-ng.
* `use_checksum()`: This option allows the users to change the default checksum settings for
S3 compatible solutions that don't support checksums. Requires botocore 1.36 or above. Acceptable values are
`when_supported` (default) and `when_required`.

Example:
```
s3(
	url("http://localhost:9000")
	bucket("testbucket")
	object_key("testobject")
	access_key("<ACCESS_KEY_ID>")
	secret_key("<SECRET_ACCESS_KEY>")
	content_type("text/plain")
	use_checksum("when_required")
);
```
