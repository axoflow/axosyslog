`switch`: add ranged case support for FilterX `switch`

Example usage:
```
switch (${values.int}) {
    case 1..4:
        result = "below";
        break;
    case 5:
        result = "exact";
        break;
    default:
        result = "above";
        break;
};
```

Note: `case 1..4:` and `case 4..1:` behave the same.
