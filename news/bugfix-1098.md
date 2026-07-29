comparisons in filterx: when comparing values with different types as
strings, use the result of the string() cast instead of relying on the
marshalled format.  This is an incompatible change when comparing null() and
datetime() objects against strings, but comparing those to strings is rare
and this was considered to be a more intuitive behaviour.
