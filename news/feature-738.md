`cloud-auth()`: Added `scope()` option for `gcp(service-account())`

Can be used for authentications using scope instead of audiance.
For more info, see: https://google.aip.dev/auth/4111#scope-vs-audience

Example usage:
```
http(
  ...
  cloud-auth(
    gcp(
      service-account(
        key("/path/to/secret.json")
        scope("https://www.googleapis.com/auth/example-scope")
      )
    )
  )
);
```
