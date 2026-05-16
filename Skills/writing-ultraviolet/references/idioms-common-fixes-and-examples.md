# Idioms, Common Fixes, and Examples

## Minimal Project

```toml
[[assembly]]
name = "HelloUltraviolet"
kind = "executable"
root = "Source"

[build]
incremental = true
progress = true
```

```ultraviolet
//! Executable entry point.

public procedure main(context: Context) -> i32 {
    let output: Outcome<(), IoError> = context.fs~>write_stdout("hello ultraviolet\n")
    return if output is {
        @Value {
            0
        }
        @Error {
            1
        }
    }
}
```

## Naming and Layout

- Assemblies, modules, directories, files, types, and enum variants use `PascalCase`.
- Procedures, methods, and transitions use `camelCase`.
- Locals, parameters, and fields use `snake_case`.
- Private fields use `_snake_case`.
- Constants use `SCREAMING_SNAKE`.
- Use 4 spaces, same-line braces, and explicit visibility.

## Syntax Defaults

```ultraviolet
let tuple_singleton: (i32;) = (1;)
let enum_value: EnumReference = EnumReference::Count(3)
let field_value: i32 = record_value.second
let child: CancelToken@Active = token~>child()
let consumed: Payload = consume(move payload)
```

## Common Fixes

- Use `::` for module/type/static/variant qualification.
- Use `.` for runtime fields and tuple indexes.
- Use `~>` for method calls.
- Use semicolons in generic parameter lists: `<TValue; TState>`.
- Use commas in generic argument lists: `<i32, bool>`.
- Add explicit return type to procedures.
- Add explicit `return` in non-unit procedures.
- Wrap unsafe operations in `unsafe { ... }`.
- Add key blocks around `shared` reads and writes.
- Prefer modal states over lifecycle booleans.
- Prefer contracts and invariants over comments for machine-checkable rules.

## Reference Examples

Use these local specimen folders before inventing syntax:

- Projects: `HelloUltraviolet/Source/Reference/Projects/**`
- Modules and names: `Modules/**`, `Names/**`
- Data types: `DataTypes/**`
- Polymorphism: `Polymorphism/**`
- Procedures and contracts: `Procedures/**`
- Expressions, patterns, statements: `Expressions/**`, `Patterns/**`, `Statements/**`
- Authority, permissions, keys: `Authority/**`, `Permissions/**`, `Keys/**`
- Async, parallelism, comptime, FFI, lowering: `Async/**`, `Parallelism/**`, `Comptime/**`, `FFI/**`, `Lowering/**`
