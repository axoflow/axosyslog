`keys()`: Add keys Function to Retrieve Top-Level Dictionary Keys

This feature introduces the keys function, which returns the top-level keys of a dictionary. It provides a simple way to inspect or iterate over the immediate keys without manually traversing the structure.

- **Returns an Array of Keys**: Provides a list of dictionary keys as an array.
- **Current Level Only**: Includes only the top-level keys, ignoring nested structures.
- **Direct Index Access**: The resulting array supports immediate indexing for quick key retrieval.

**Example**:

```python
    dict = {"foo":{"bar":{"baz":"foobarbaz"}},"tik":{"tak":{"toe":"tiktaktoe"}}};
    # empty dictionary returns []
    empty = keys(json());

    # accessing the top level results ["foo", "tik"]
    a = keys(dict);

    # acccessing nested levels directly results ["bar"]
    b = keys(dict["foo"]);

    # directly index the result of keys() to access specific keys is possible (returns ["foo"])
    c = keys(dict)[0];
```
