New filterx types `subnet()` and `ip()`: these new types encapsulate an
IPv4/IPv6 subnet (in CIDR notation) or a single IP address. Both types takes
their string representation and will return an ERROR if the format cannot be
parsed.

Example configuration:

    a = subnet("192.168.0.0/24");

    "192.168.0.5" in a;
    "192.168.1.5" in a or true;
    ip("192.168.0.11") in a;

    a6 = subnet("DEAD:BEEF::1/64");

    "DEAD:BEEF::2" in a6;
    "DEAD:BABE::1" in a6 or true;
    ip("DEAD:BEEF::00ac") in a6;
