`startswith()`, `endswith()`, `includes()`: Added string matching functions.

* First argument is the string that is being matched.
* Second argument is either a single substring or a list of substrings.
* Optionally the `ignorecase` argument can be set to configure case sensitivity
  * default: `false`

Example usage:
```
startswith(string, prefix, ignorecase=false);
startswith(string, [prefix_1, prefix_2], ignorecase=true);

endswith(string, suffix, ignorecase=false);
endswith(string, [suffix_1, suffix_2], ignorecase=true);

includes(string, substring, ignorecase=false);
includes(string, [substring_1, substring_2], ignorecase=true);
```
