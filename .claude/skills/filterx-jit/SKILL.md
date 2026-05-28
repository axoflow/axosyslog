---
name: filterx-jit
description: Write or modify LLVM IR emitting code for FilterX expressions in AxoSyslog - implementing `compile()` methods, calling `fx_jit_emit_*` helpers, building IR sequences, declaring runtime FFI calls. Use this whenever editing `lib/filterx/` JIT code, working with `FilterXJIT`/`FilterXIRValue`/`FilterXIRBuilder`, emitting LLVM IR via `LLVMBuild*`, adding new FFI runtime callbacks, or debugging JIT-compiled FilterX.
---

# FilterX JIT — Writing LLVM IR for FilterX Expressions

This skill captures how `compile()` methods on FilterX expressions emit LLVM IR. Read it before adding or editing JIT
code under `lib/filterx/`.

The two important units to go through when encountering FilterX JIT code are `lib/filterx/jit/jit.h` and
`lib/filterx/jit/ffi.h`.

## The compile() method

Every FilterX expression implements the `compile()` method (`lib/filterx/filterx-expr.h`):

```c
FilterXIRValue (*compile)(FilterXExpr *self, FilterXJIT *jit);
```

- `FilterXIRValue` return value - representing a `FilterXObject*` pointer at runtime. The caller owns this reference.
  - `NULL` signals "evaluation failed" - an error is pushed in such cases.
- To compile a child expression, never call `child->compile` directly. Use `filterx_expr_compile_or_eval()` or its typed
  version.

## IR Builder, FFI

These are almost always needed at the top of `compile()`:

```c
FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
FilterXIRValue block = filterx_jit_ir_get_current_block(jit);
```

`FilterXJITFFI` (`lib/filterx/jit/ffi.h`) holds frequently-used LLVM types (`ptr_ty`, `i32_ty`, `i64_ty`, `void_ty`) and
FFI method calls (`fx_jit_emit_*()`). Always use the cached types and calls whenever available. If a direct FFI call is
not available, `fx_jit_emit_extern_call()` can be used instead.

`FilterXIRBuilder` is the LLVM IR code builder, pass this to `LLVMBuild*()` and other functions producing LLVM IR code.

`filterx_jit_ir_get_current_block()` queries the currently compiled FilterX block. It is needed when building different
IR code control flows: `FilterXIRSequence`, etc. For anything beyond a flat call sequence - conditionals, loops, error
paths - use the sequence API of FilterXJIT (FilterXIRSequence).

## Examples

When writing IR code generation, take a look at examples to follow the current patterns and conventions. The following
units contain good examples:
- `lib/filterx/expr-compound.c`
- `lib/filterx/expr-condition.c`
- `lib/filterx/expr-arithmetic-operators.c`
- `lib/filterx/expr-assign.c`

## Interpreted and JIT-compiled execution

The interpreted code (`eval()`) and the JIT-compiled code (`compile()`) are intended to coexist long-term. The most
common development workflow is for the developer to first implement the interpreted version, after which an LLM or the
developer writes equivalent LLVM IR code based on it.

Since the two implementations must always remain in sync, every round of LLVM IR code generation begins with a
refactoring pass (see the example expressions), which proceeds as follows:

The evaluation of the expression is decoupled from error handling and from the actual execution of the operation. As a
result, the `eval` methods become short and uniform: they evaluate the child expressions and pass the resulting
`FilterXObject`s to a function, which is responsible for both error handling and carrying out the operation. These
functions usually consume the FilterXObject arguments, and produce a new FilterXObject result.

This refactor/extraction simplifies the `compile` method: rather than emitting a large amount of IR, it only needs to
call into the extracted C functions. These functions must be wrapped in an `__attribute__((used))` `fx_jit_*()` function
so that they can be invoked from IR code.

If you need a helper like `_do_substraction()` and it's only called from one expression, add it to the given
expression's unit, do not add it to `ffi.h`.

## Error propagation

Errors are pushed to a thread-local stack — they are **orthogonal to the return value**. A failed expression returns
`NULL` *and* has pushed an error. You can use `fx_jit_emit_eval_push_*_error_*()` methods to push an error from IR code,
but you should always prefer what is described in the `Interpreted and JIT-compiled execution` section in order to keep
the interpreted and JIT-compiled code close to each other.
