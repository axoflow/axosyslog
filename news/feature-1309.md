`filterx`: Added the `random()` and `random_bool()` functions.

`random()` returns a double in `[0, 1)`, `random(n)` an integer in `[0, n)`, and `random_bool(p)` returns `true`
with probability `p` (`0.5` by default). They use a fast per-thread generator and are meant for per-message
decisions: sampling (`if (not random_bool(0.01)) { drop; };` keeps about 1% of the messages), spreading the load
across destinations at random (`random(3)` picks one of three branches), or sending a share of the traffic to a
test pipeline. The values are not cryptographically secure.
