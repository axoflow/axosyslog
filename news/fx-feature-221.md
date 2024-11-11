`flatten()`: Added new function to flatten dicts and lists.

The function modifies the object in-place.
The separator can be set with the `separator` argument,
which is `.` by default.

Example usage:
```
flatten(my_dict_or_list, separator="->");
```
