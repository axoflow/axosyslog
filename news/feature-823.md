Stats: expose `window_capacity` and `window_available` metrics via the
prometheus interface on `stats(level(3))`.  These metrics show the
log-iw-size() value and the current state of the source window,
respectively.  The PR also adds a `window_full_total` counter that
tracks how many times the window was completely full.  This counter will
increase any time the destination causes the source to be throttled.
