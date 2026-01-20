http: add force-content-compression option

Usage:

```
destination {
  http(
    url("http://example.com/endpoint")
    content-compression("gzip")
    force-content-compression(yes)
  );
}
```
