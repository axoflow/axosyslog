`unset_empties()`: Added advanced options.

`unset_empties` removes elements from the given dictionary or list that match
the empties set. If the `recursive` argument is provided, the function will
process nested dictionaries as well. The `replacement` argument allows
replacing target elements with a specified object, and the targets
argument customizes which elements are removed or replaced, overriding
the default empties set.

* Optional named arguments:
  * recursive: Enables recursive processing of nested dictionaries. default: `true`
  * ignorecase: Enables case-insensitive matching. default: `true`
  * replacement: Specifies an object to replace target elements instead of removing them.
    default: nothing (remove)
  * targets: A list of elements to identify for removal or replacement, clearing the default empty set.
    default: `["", null, [], {}]`

Example usage:
```
unset_empties(js1, targets=["foo", "bar", null, "", [], {}], ignorecase=false, replacement="N/A", recursive=false);
```
