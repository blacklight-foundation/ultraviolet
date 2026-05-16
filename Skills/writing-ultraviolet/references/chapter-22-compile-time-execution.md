# Chapter 22: Compile-Time Execution

## Load When

Use for `comptime`, compile-time capabilities, reflection, quote, splice, emission, derive targets, and compile-time diagnostics.

## Authoring Rules

- Use `comptime` only when code must execute during compilation.
- Keep compile-time capabilities explicit and bounded.
- Use quote/splice for generated source forms that remain inspectable.
- Use derive targets for structured generation tied to declarations.

## Syntax Forms

```ultraviolet
[[reflect]]
internal record ComptimeReflectedRecord {
    internal value: i32
}

internal procedure reflectedTypeNameExists() -> bool {
    let type_name_ok: bool =
        comptime { introspect~>type_name(Type::<ComptimeReflectedRecord>) != "" }
    return type_name_ok
}
```

```ultraviolet
[[emit]]
comptime {
    let return_type: Ast::Type = quote type { i32 }
    let return_expr: Ast::Expr = quote { 19 + 23 }
    let ast: Ast::Item = quote {
        internal procedure emittedSplicedReferenceValue() -> $(return_type) {
            return $(return_expr)
        }
    }
    emitter~>emit(ast)
}
```

## Static Semantics

Compile-time code runs after required earlier translation steps and before dependent later checks. Reflection and emission have explicit capability and type constraints.

## Runtime and Lowering Notes

Compile-time execution produces compile-time effects and generated declarations; it should not hide runtime authority or dynamic behavior.

## Diagnostics

Use for invalid compile-time context, missing compile-time capability, invalid reflection target, malformed quote/splice, and derive contract errors.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Comptime/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `22` for compile-time execution.
