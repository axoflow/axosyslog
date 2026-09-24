filterxjitincludedir = ${pkgincludedir}/filterx/jit

EXTRA_DIST += lib/filterx/jit/CMakeLists.txt

filterxjitinclude_HEADERS = \
	lib/filterx/jit/jit.h \
	lib/filterx/jit/jit-private.h \
	lib/filterx/jit/bc-loader.h \
	lib/filterx/jit/ffi.h

filterxjit_sources = \
	lib/filterx/jit/jit.c \
	lib/filterx/jit/ffi.c \
	lib/filterx/jit/bc-loader.c \
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
	-emit-llvm -O3 --no-warnings -mno-outline-atomics

# The bitcode gets inlined into JIT compiled code, so it has to match the
# headers exactly (struct layouts, flag bits): track the headers each unit
# includes, the way automake does for objects, via compiler generated
# dependency files.  A unit built before this tracking existed has no
# dependency file yet and is not rebuilt for a header change until its
# source changes or the tree is cleaned once.
filterx_bitcode_deps = $(filterx_bitcode_units:.bc=.bc.d)

$(filterx_bitcode_units): %.bc: %.c
	$(AM_V_GEN)$(filterx_bitcode_compile) -MMD -MP -MT $@ -MF $@.d -c $< -o $@

-include $(filterx_bitcode_deps)

lib/filterx/jit/libfilterx.bc: $(filterx_bitcode_units)
	$(AM_V_GEN)$(LLVM_LINK) -o $@ $^

CLEANFILES += \
	$(filterx_bitcode_units) \
	$(filterx_bitcode_deps) \
	lib/filterx/jit/libfilterx.bc

endif
