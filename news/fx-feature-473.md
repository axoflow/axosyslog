Added support for switch cases.

This syntax helps to organize the code for multiple
`if`, `elif`, `else` blocks and also improves
the branch finding performance.

Cases with literal string targets are stored in a map,
and the lookup is started with them.

Other case targets can contain any expressions,
and they are evaluated in order.

Please note that although literal string and default
target duplications are checked and will cause init failure,
non-literal expression targets are not checked, and only
the first maching case will be executed.

Example config:
```
switch ($MESSAGE) {
  case "foobar":
    $MESSAGE = "literal-case";
    break;
  case any_expression:
    $MESSAGE = "variable-case";
    break;
  default:
    $MESSAGE = "default";
    break;
};
```
