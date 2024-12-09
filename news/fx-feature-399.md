`regex_search()`: Function Reworked

The `regex_search()` function has been updated to simplify behavior and enhance configurability:

- **Consistent Return Type**:
  The legacy behavior of changing the return type (`dict` or `list`) based on the presence of named match groups has been removed. The function now always returns a `dict` by default.

- **Override with `list_mode`**: Use the `list_mode` optional named argument flag to explicitly return a `list` of match groups instead.

    **Example**:
    ```python
    result = regex_search("24-02-2024", /(?<date>(\d{2})-(\d{2})-(\d{4}))/)
    result = regex_search("24-02-2024", /(?<date>(\d{2})-(\d{2})-(\d{4}))/, list_mode=True)
    ```

- **Result Type from Existing Objects**:
  If `result` is an existing `filterx` object with a specific type (`dict` or `list`), the function respects the type of the object, independent of the `list_mode` flag.

- **Match Group 0 Handling**:
  Match group `0` is now excluded from the result by default (since it is rarely used), unless it is the only match group. To include match group `0` in the result, use the `keep_zero` optional named argument flag.

    **Example**:
    ```python
    result = regex_search("24-02-2024", /(?<date>(\d{2})-(\d{2})-(\d{4}))/, keep_zero=True)
    ```
