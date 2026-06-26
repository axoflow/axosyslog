`http()` source: added a generic HTTP server source driver

A new `http()` source accepts `POST` requests on a configurable `port()` and
URL `path()`, splits the request body at newlines and posts each line as a
separate log message, tagging it with the client's address (`${SOURCEIP}`).
Multiple `http()` sources may share the same port, each registering its own
URL path.  Inactive connections are closed after `timeout()` seconds
(default 30, `0` disables).  Example:

```
source s_http {
    http(port(8080) path("/api/ingest"));
};
```
