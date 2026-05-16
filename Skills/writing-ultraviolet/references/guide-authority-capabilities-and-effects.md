# Guide: Authority, Capabilities, and Effects

## Load When

Use for `Context`, filesystem/network/system/time authority, capability attenuation, host effects, effect gating, and public framing of UV authority.

## Core Model

- UV uses no ambient authority.
- Ordinary runtime capability roots come through `Context`.
- Capability-bearing operations are explicit method calls on capability values.
- Capability attenuation must narrow or preserve authority, never expand it.
- Observable effects include host effects, FFI effects, panic effects, drop effects, and key effects.

## Capability Roots

`Context` carries filesystem, network, heap allocator, reactor, execution domain, system, and time authority. Pass only the capability a helper uses.

```ultraviolet
internal procedure writeMessage(fs: $FileSystem) -> bool {
    let output: Outcome<(), IoError> = fs~>write_stdout("message\n")
    return if output is {
        @Value {
            true
        }
        @Error {
            false
        }
    }
}

public procedure main(context: Context) -> i32 {
    if writeMessage(context.fs)
        return 0
    return 1
}
```

## Access Syntax

- `context.fs` is runtime field access.
- `context.fs~>write_stdout(...)` is a method call.
- `FileSystem::restrict` or `Region::new_scoped` is type/static qualification.

## Common Fixes

- Replace hidden global effects with explicit capability parameters.
- Narrow a broad context parameter to `FileSystem`, `Network`, `Time`, or the exact capability used.
- Use `~>` for capability methods.
- Keep FFI effects behind capability-aware wrappers.

## Related Chapters

- `chapter-06-abstract-machine-responsibility-authority-memory.md`
- `chapter-15-procedures-and-contracts.md`
- `chapter-16-expressions.md`
- `chapter-23-ffi.md`
- `chapter-24-lowering-lifecycle-and-backend.md`
