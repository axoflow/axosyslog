`in` operator: fix crash, that happened, when the list was declared as an operand

Example:
```
if ("test" in ["foo", "bar", "test"]) {
	$MSG = "YES";
};
```
