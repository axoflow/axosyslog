filterx `in` operator supports dict keys for membership check

```
my_dict = {"foo": "foovalue", "bar": "barvalue"};
my_needle = "foobar";

if (my_needle in my_dict) {
  $MSG = "Found: " + my_dict[my_needle];
} else {
  $MSG = "Not Found";
};
```
