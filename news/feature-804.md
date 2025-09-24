FilterX `parse_csv()`: add `quote_pairs` parameter

For example:

```
filterx {
  str = "sarga,[bogre],'gorbe'";
  $MSG = parse_csv(str, quote_pairs=["[]", "'"]);
};
```
