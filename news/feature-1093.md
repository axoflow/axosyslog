`switch`: add ranged case support for FilterX `switch`

Example usage:
```
selector = 5;
switch (selector) {
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
