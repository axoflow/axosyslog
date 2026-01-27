Add `trusted-fingerprints()` option: this new option will allow you to trust
X.509 certificates based on their fingerprints. The new option deprecates
`trusted-keys()`, the major difference being that `trusted-fingerprints()`
will accept X.509 certificates as valid, even if the normal X.509 validation
fails, whereas `trusted-keys()` needed both the X.509 verification pass and
the fingerprint checking to succeed. This feature also adds the capability
to use a fingerprinting method other than SHA1, which is not considered safe
anymore.

Here's an example syntax:

	tls(
		...
		trusted-fingerprints("SHA1:0C:EF:34:4D:0B:74:AE:03:72:9A:4E:68:AF:90:59:A9:EF:35:1F:AA",
				     "SHA512:15:B3:C5:96:48:5B:F6:20:C3:86:47:78:99:E1:2B:F2:C4:A6:93:AE:E8:0A:B3:F7:78:39:66:B4:EF:4F:A5:47:2A:E0:4A:93:06:46:72:C0:15:6A:FC:59:10:25:37:60:E3:84:E9:EC:90:30:12:F5:27:EA:22:1F:55:9B:3B:97")
	)

NOTE: the fingerprinting method is the word before the first colon. The
naming of the fingerprinting methods should match OpenSSL's supported digest
algorithms.

As of OpenSSL 3.0.10, the following digest algorithms are supported:

Message Digest commands (see the `dgst' command for more details)
blake2b512        blake2s256        md4               md5
rmd160            sha1              sha224            sha256
sha3-224          sha3-256          sha3-384          sha3-512
sha384            sha512            sha512-224        sha512-256
shake128          shake256          sm3

To find out the fingerprint for a certificate, you can use this command:

$ openssl x509 -sha512 -in <certificate file in pem> -fingerprint
