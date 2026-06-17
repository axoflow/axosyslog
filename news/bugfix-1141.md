`filterx`: fix out-of-bounds write when parsing a subnet with a negative prefix

A `subnet()` CIDR with a negative prefix such as `::/-100` slipped past the prefix
bounds check and produced a negative netmask length, writing past the end of the
buffer. Negative prefixes are now rejected.
