`build`: `--with-llvm-linking=shared|static` selects how the filterx JIT links LLVM

`static` links the component archives into libsyslog-ng instead of depending on the shared libLLVM; the default stays
whatever `llvm-config` reports. An installation without the libLLVM dylib is detected and linked statically as well.
