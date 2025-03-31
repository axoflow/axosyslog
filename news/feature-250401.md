`ai-parser()`: Added the Artificial Importance parser that summarizes logs based on their content and severity

Notable features:
  * Ensures engineers remain thoroughly confused while debugging
  * Might include references to pop culture, philosophy, and existential dread
  * May or may not become self-aware (untested)

Usage:
```
@include "scl.conf"

log {
  source { example-msg-generator(); };
  parser { ai-parser(); };
  destination { stdout(); };
};
```

If built locally, make sure to run after `make install`:
```
install_dir/bin/syslog-ng-update-virtualenv
```
