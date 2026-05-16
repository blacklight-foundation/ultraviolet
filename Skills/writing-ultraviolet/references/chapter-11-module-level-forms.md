# Chapter 11: Module-Level Forms

## Load When

Use for imports, using declarations, static declarations, extern block shells, and module/file aggregation.

## Authoring Rules

- Directories define modules; multiple `.uv` files in the same directory share the module.
- Use `import Module::Path as Alias` for module aliases.
- Use exact `using` declarations in public API surfaces.
- Keep facade `Api.uv` files thin.
- Treat statics as module-level declarations with initialization and cleanup implications.

## Syntax Forms

```ultraviolet
import HelloUltraviolet::Reference::Modules::AggregationSubmodule as ImportedSubmodule
using HelloUltraviolet::Reference::DataTypes::{
    RecordReference as ModuleRecord,
    recordReference as makeModuleRecord,
}

internal let DEFAULT_LIMIT: i32 = 64

internal procedure moduleLevelReference() -> bool {
    let contribution: ImportedSubmodule::SubmoduleAggregationContribution =
        ImportedSubmodule::submoduleAggregationContribution(8, 13)
    let reference: ModuleRecord = makeModuleRecord("module-using", 12usize, true)
    return DEFAULT_LIMIT == 64 &&
        ImportedSubmodule::submoduleAggregationValue(contribution) == 21 &&
        reference.count == 12usize
}
```

## Static Semantics

Module aggregation combines files under the same directory module. Public re-exports must respect visibility. Extern block shells group foreign declarations but FFI semantics live in chapter 23.

## Runtime and Lowering Notes

Statics participate in initialization, cleanup, symbol generation, and hosted-library state behavior.

## Diagnostics

Use for invalid imports/usings, duplicate module items, aggregation conflicts, static initialization errors, extern shell misuse, and public visibility violations.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Modules/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `11` for import, using, static, extern shell, and aggregation rules.
