Support string slicing

For example:
```
filterx {
    str = "example";
    idx = 3;
    str[idx..5] == "mp";
    str[..idx] == "exa";
    str[idx..] == "mple";
};
```
