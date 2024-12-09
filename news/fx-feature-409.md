`regex_subst()`: Function Reworked

The `regex_subst()` function has been updated to enhance functionality:

- **Extended Match Group Support**:
  Replacement strings can now resolve match group references up to 999 groups.

- **Optional Disabling**:
  The feature can be disabled using the `groups` named argument flag.

- **Leading Zero Support**:
  Match group references with leading zeros (e.g., `\01`, `\002`) are now correctly interpreted. This prevents ambiguity when parsing group IDs, ensuring that shorter IDs like `\1` are not mistakenly interpreted as part of larger numbers like `\12`.

**Example**:

```python
result = regex_subst("baz,foo,bar", /(\w+),(\w+),(\w+)/, "\\2 \\03 \\1")

# Force disable this feature
result = regex_subst("baz,foo,bar", /(\w+),(\w+),(\w+)/, "\\2 \\03 \\1", groups=false)

# Handling leading zeros
result = regex_subst("baz,foo,bar", /(\w+),(\w+),(\w+)/, "\\0010") # returns `baz0`
```
