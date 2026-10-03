`build`: one way to pick the C++ linker for a libtool library, one for a program

Every library whose C++ comes from a convenience library or from a static LLVM sets `--tag=CXX` in its per-target
`LIBTOOLFLAGS`; the `nodist_EXTRA` fake C++ source is gone from the libraries. `CCLD` is the C++ driver only in
monolithic builds, where the C++ objects end up inside the programs; in a dynamic build the modules carry their own
libstdc++ dependency and the syslog-ng binary no longer depends on it itself.
