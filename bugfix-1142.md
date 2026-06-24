`proxy-protocol`: fix out-of-bounds read with a malformed PROXY protocol v2 datagram

A PROXY protocol v2 datagram received over UDP could declare a header length larger than
the bytes actually received, leading to a read past the end of the buffer. The declared
length is now validated against the received size.
