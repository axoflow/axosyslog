`=??` assignment operator

Syntactic sugar operator, which could slightly improve performance as well.

It can be used to assign a non-null value to the left-hand side.
Evaluation errors from the right-hand side will be suppressed.

For example,

`resource.attributes['service.name'] =?? $PROGRAM;` can be used instead of:

```
if (isset($PROGRAM)) {
  resource.attributes['service.name'] = $PROGRAM;
};
```
