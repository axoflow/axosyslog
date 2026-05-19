Decouple `repr()` and `string()`: previously the repr() of an object was
typically the same as str, did not include type-related hints in its output.
This makes repr() usage less useful, especially as we are adding more types
to filterx. Starting with this version, repr() includes a format similar to
Python's repr. This is an incompatible change for uses where repr() was
directly used in an output, but normally its intended has always been the
debug log of filterx. If your use-case relies on the current repr() format,
you should explicitly cast your object to `string()` instead.