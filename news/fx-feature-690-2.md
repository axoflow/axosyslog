`parse_cef()`, `parse_leef()`: Extensions are no longer put under the `extensions` inner dict

By default now they get placed on the same level as the headers.
The new `separate_extensions=true` argument can be used for the
old behavior.
