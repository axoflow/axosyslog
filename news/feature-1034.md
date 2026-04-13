`cache_json_file`: add deafult_value parameter to FilterX function

If the file is not present, or an error occurs when reading it, the default_value will be used, if provided.

Example:
```
cache_json_file("./test.json", default_value={"key": "value"});
```
