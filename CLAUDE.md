# AxoSyslog

## Building and testing

Build and test **only** through `.claude/bin/axosyslog-build` — do not run
`autogen.sh`/`configure`/`make`/`cmake`/`ninja`/`dbld/rules` directly. It builds
out-of-tree in `build/`, installs to `build/install/`, and runs the installed
binary. Full flag reference: `axosyslog-build -h`.

```sh
export PATH="$PWD/.claude/bin:$PATH"
axosyslog-build            # cmake + clang + lld + ninja + JIT + ASan (default)
axosyslog-build --check    # ... then the unit tests
axosyslog-build --light    # ... then the light/e2e suite
axosyslog-build autotools  # release/packaging build instead
```

- **Run the installed binary** `build/install/sbin/syslog-ng`, never the
  build-tree `build/syslog-ng/syslog-ng` — the latter misses runtime libraries
  and does not match how syslog-ng really executes (module path, persist/control
  sockets). `axosyslog-build` installs and runs from the prefix for you.
- **One build mode per tree.** Mixing modes regenerates `lib/ivykis` with a
  different libtool and corrupts it. `git status` and `git submodule status`
  both stay clean (the regenerated files are gitignored and `.gitmodules` sets
  `ignore = dirty`); the tell-tale is `lib/ivykis/configure~` and
  `config.h.in~` backup files. Switching modes needs `--clean`, which also
  runs the ivykis recovery.
