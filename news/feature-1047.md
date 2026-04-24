`tuple()`: this is a read-only, list-like data type, that can only be
initialized once, and then remains read-only until the end of its lifecycle.
Patterned after the Python type of the same name.

  t = ();        # empty tuple
  t = ("foo",);  # singleton
  t = (1,2,3);   # a tuple of 3 elements
