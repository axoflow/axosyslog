set(FILTERX_JIT_HEADERS
    filterx/jit/jit.h
    filterx/jit/jit-private.h
    filterx/jit/bc-loader.h
    filterx/jit/ffi.h
    PARENT_SCOPE
    )

set(FILTERX_JIT_SOURCES
    filterx/jit/jit.c
    filterx/jit/bc-loader.c
    filterx/jit/jit-runtime.c
    filterx/jit/ffi.c
    PARENT_SCOPE
    )

set(FILTERX_JIT_BITCODE_SOURCES
    jit/jit-runtime.c
    PARENT_SCOPE
    )
