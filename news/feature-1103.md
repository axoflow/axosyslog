`http()`: add response-adapter(openobserve) support, which processes HTTP
responses from OpenObserve backends. OpenObserve normally returns HTTP 200
OK even for requests that fail partially, and this setting will turn that
into an actual error, so it can be retried.
