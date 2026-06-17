`filterx`: fix crash in `set_pri()` when its argument cannot be evaluated

A `set_pri()` argument that produced no value, for example `int()` of a non-numeric
string, was dereferenced. The evaluation error is now propagated.
