`filterx`: Fixed a lost write into a dict literal that has a runtime member.

`d = {"user": {"name": v}}; d.user.name = "X";` kept the old value with no error.
Subscript writes, `dpath()` targets and `declare`d variables were affected the same way.
