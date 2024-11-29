`set_fields()`: Added new function to set a dict's fields with overrides and defaults.

A recurring pattern in FilterX is to take a dict and set multiple
fields in it with overrides or defaults.

`set_fields()` takes a dict as the first argument and `overrides`
and `defaults` as optional parameters.

`overrides` and `defaults` are also dicts, where the key is
the field's name, and the value is either an expression, or
a list of expressions. If a list is provided, each expression
will be evaluated, and the first successful, non-`null` one will
be used to set the respective field's value. This is similar to
chaining null-coalescing (`??`) operators, but is more performant.

`overrides` are always processed for each field. The `defaults`
for a field are only processed, if the field does not already
have a value set.

Example usage:
```
set_fields(
  my_dict,
  overrides={
    "foo": [invalid_expr, "foo_override"],
    "baz": "baz_override",
    "almafa": [invalid_expr_1, null],  # No effect
  },
  defaults={
    "foo": [invalid_expr, "foo_default"],
    "bar": "bar_default",
    "almafa": "almafa_default",
    "kortefa": [invalid_expr_1, null],  # No effect
    }
);
```
