# Chapter 23: FFI

## Load When

Use for `FfiSafe`, extern procedures, exports, hosted exports, FFI attributes, capability isolation, foreign contracts, and boundary unwinding.

## Authoring Rules

- Keep foreign interaction in dedicated boundary modules.
- Wrap unsafe foreign calls in safe APIs that restore project invariants.
- Use `FfiSafe` and ABI attributes only where the layout and ownership rules permit them.
- Document ownership, lifetime, thread affinity, and caller obligations at unsafe boundaries.

## Syntax Forms

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

## Static Semantics

FFI checks layout safety, ABI compatibility, exported symbol shape, capability isolation, foreign assumptions/ensures, and unwind behavior.

## Runtime and Lowering Notes

FFI effects are observable. Boundary calls interact with panic/unwind, cleanup, drop, ABI lowering, and capability isolation.

## Diagnostics

Use for non-`FfiSafe` by-value passing, invalid extern/export shape, bad ABI attribute, capability leak, invalid foreign contract, and boundary unwind mismatch.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/FFI/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `23` for FFI rules.
