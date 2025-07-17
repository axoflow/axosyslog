Add `str_replace()` function

For example:
```
filterx {
    dal = "érik a szőlő, hajlik a vessző";
    str_replace(dal, "a", "egy") == "érik egy szőlő, hegyjlik egy vessző";

    dal = "érik a szőlő, hajlik a vessző";
    str_replace(dal, "a", "egy", 1) == "érik egy szőlő, hajlik a vessző";
};
```
