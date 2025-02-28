`webhook()`: headers support

`include-request-headers(yes)` stores request headers under the `${webhook.headers}` key,
allowing further processing, for example, in FilterX:

```
filterx {
  headers = json(${webhook.headers});
  $type = headers["Content-Type"][-1];
};
```

`proxy-header("x-forwarded-for")` helps retain the sender's original IP and the proxy's IP address
(`$SOURCEIP`, `$PEERIP`).
