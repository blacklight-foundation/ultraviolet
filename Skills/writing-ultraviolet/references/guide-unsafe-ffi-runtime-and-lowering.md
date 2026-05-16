# Guide: Unsafe, FFI, Runtime, and Lowering

## Load When

Use for unsafe blocks, raw pointers, FFI calls, ABI attributes, exported procedures, foreign contracts, runtime symbols, cleanup, drop, unwinding, and backend diagnostics.

## Core Model

- `unsafe` is a local boundary for operations safe UV cannot prove.
- FFI is isolated to boundary modules.
- Safe wrappers re-establish ownership, lifetime, thread, capability, and layout invariants.
- Runtime and lowering behavior should explain compiler defects without rewriting spec-valid source.

## Source Pattern

```ultraviolet
extern "C" {
    public procedure importedReferencePrimitive(value: i32) -> i32
}

[[export("C")]]
[[mangle("importedReferencePrimitive")]]
public procedure ffiImportedReferencePrimitiveProvider(value: i32) -> i32 {
    return value + 1
}

internal procedure callImportedReferencePrimitive(value: i32) -> i32 {
    return unsafe { importedReferencePrimitive(value) }
}
```

## Common Fixes

- Move raw foreign calls into a dedicated boundary module.
- Add `unsafe` around the smallest required expression or block.
- Check `FfiSafe` before passing values by foreign ABI.
- Treat unexpected runtime/drop/lowering output as a compiler owner question, then route through `compiler-mismatch-workflow.md`.

## Related Chapters

- `chapter-18-statements-and-blocks.md`
- `chapter-23-ffi.md`
- `chapter-24-lowering-lifecycle-and-backend.md`
