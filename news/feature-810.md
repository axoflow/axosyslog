`dict_to_pairs()` FilterX function: Added a new function to convert dicts to list of pairs

Example usage:
```
dict = {
    "value_1": "foo",
    "value_2": "bar",
    "value_3": ["baz", "bax"],
};

list = dict_to_pairs(dict, "key", "value");
# Becomes:
# [
#   {"key":"value_1","value":"foo"},
#   {"key":"value_2","value":"bar"},
#   {"key":"value_3","value":["baz","bax"]}
# ]
```
