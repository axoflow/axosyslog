`transport(proxied-tcp)`: Fix a HAProxy protocol v2 parsing issue that
caused a failed assertion.  This essentially triggers a crash with a SIGABRT
whenever a "LOCAL" command was sent in the HAProxy header without address
information.
