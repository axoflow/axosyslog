`filterx`: fix crash on integer division or modulo by zero

Dividing or taking the modulo of an integer by zero raised SIGFPE, and `INT64_MIN`
divided by `-1` overflowed. Both now raise a filterx error, including when the
operands are folded to a constant at startup.
