FilterX string slices: support negative indexing

For example,
```
filterx {
  str = "example";
  str[..-2] == "examp";
  str[-3..] == "ple";
};
```
