`s3`: Bugfix for `s3` destination driver

New `object_key_suffix` option.
Fixing a bug, where empty chunks were being uploaded, causing errors.

Example:
```
s3(
	url("http://localhost:9000")
	bucket("testbucket")
	object_key("testobject")
	access_key("<ACCESS_KEY_ID>")
	secret_key("<SECRET_ACCESS_KEY>")
	object_key_suffix(".log")
);
