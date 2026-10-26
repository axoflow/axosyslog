`log`: Added the experimental `flags(destination-failover)` log statement flag.

It routes messages over an ordered set of destination paths: when a destination is unreachable, messages go to the
next path, and they return to the first one as soon as it becomes available again. The last path never rejects
messages, so when every destination is unreachable, it buffers them as it would without the flag. The flag goes on
the enclosing `log {}` statement and applies to everything nested in it; the alternatives inside it need
`flags(final)`, or use `if {} else {}`, which implies it:

```
log {
    log { destination(primary); flags(final); };
    log { destination(secondary); flags(final); };
    flags(destination-failover);
};
```

```
log {
    if {
        destination(primary);
    } else {
        destination(secondary);
    };
    flags(destination-failover);
};
```

Network (`network()`, `tcp()`, `udp()`, ...), threaded (e.g. `http()`, `python()`) and `file()` destinations report
themselves unreachable while they cannot connect or cannot open their file. A destination reference is handled as a
unit, so a message dropped inside it (e.g. by `filterx`) also triggers the failover.
