filterxjitincludedir = ${pkgincludedir}/filterx/jit

EXTRA_DIST += lib/filterx/jit/CMakeLists.txt

filterxjitinclude_HEADERS = \
	lib/filterx/jit/jit.h \
	lib/filterx/jit/jit-private.h \
	lib/filterx/jit/ffi.h

filterxjit_sources = \
	lib/filterx/jit/jit.c \
	lib/filterx/jit/ffi.c \
	lib/filterx/jit/jit-runtime.c

filterxjit_bitcode_sources = \
	lib/filterx/jit/jit-runtime.c

if ENABLE_JIT

filterx_bitcode_units = $(filterx_bitcode_sources:.c=.bc)

filterx_bitcode_compile = \
	$(LLVM_CLANG) \
	$(DEFS) $(DEFAULT_INCLUDES) \
	$(AM_CPPFLAGS) $(CPPFLAGS) \
	$(filter-out -fsanitize=%,$(AM_CFLAGS) $(CFLAGS)) \
	-emit-llvm -O3 --no-warnings

$(filterx_bitcode_units): %.bc: %.c
	$(AM_V_GEN)$(filterx_bitcode_compile) -c $< -o $@

lib/filterx/jit/libfilterx.bc: $(filterx_bitcode_units)
	$(AM_V_GEN)$(LLVM_LINK) -o $@ $^

CLEANFILES += \
	$(filterx_bitcode_units) \
	lib/filterx/jit/libfilterx.bc

endif
