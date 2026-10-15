`filterx`: `break` now leaves the innermost enclosing `switch`, as in C, also from a nested block inside a case.
A `break` outside of a `switch` is now a configuration error ("break statement not within a switch"); it used to
leave the innermost block.
