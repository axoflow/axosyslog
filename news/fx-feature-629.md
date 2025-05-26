Failure information tracking

The following functions have been added to allow tracking failures in FilterX code:

- `failure_info_enable()`, optional parameter: `collect_falsy=true/false`, defaults to `false`:
  Enable failure information collection from this point downwards through all branches of the pipeline.

- `failure_info_clear()`:
  Clear accumulated failure information

- `failure_info_meta({})`:
  Attach metadata to the given section of FilterX code. The metadata remains in effect until the next call
  or until the end of the enclosing FilterX block, whichever comes first.

- `failure_info()`:
  Return failure information as a FilterX dictionary. This should ideally be called as late as possible, for example, in the last log path of your configuration or within a fallback path.

Example output:

```
[
  {
    "meta": {
      "step": "Setting common fields"
    },
    "location": "/etc/syslog-ng/syslog-ng.conf:33:7",
    "line": "nonexisting.key = 13;",
    "error": "No such variable: nonexisting"
  }
]
```
