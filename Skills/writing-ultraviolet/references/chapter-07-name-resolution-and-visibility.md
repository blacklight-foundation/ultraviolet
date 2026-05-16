# Chapter 7: Name Resolution and Visibility

## Load When

Use for qualified paths, visibility, imports, using declarations, module aliases, shadowing, local scopes, and resolving values/types/classes/modules.

## Authoring Rules

- Use `::` for module, type, and static qualification.
- Use `.` for runtime field access.
- Write visibility explicitly where the language allows it.
- Keep public API imports exact; use wildcard `using` only in internal or implementation code.

## Syntax Forms

```ultraviolet
import HelloUltraviolet::Reference::Modules::AggregationSubmodule as ImportedSubmodule
using HelloUltraviolet::Reference::DataTypes::{ RecordReference, recordReference }

internal procedure resolveImportedNames() -> bool {
    let contribution: ImportedSubmodule::SubmoduleAggregationContribution =
        ImportedSubmodule::submoduleAggregationContribution(8, 13)
    let reference: RecordReference = recordReference("name-using", 21usize, true)
    return ImportedSubmodule::submoduleAggregationValue(contribution) == 21 &&
        reference.is_active
}
```

## Static Semantics

Resolution distinguishes value, type, class, and module-alias namespaces. Visibility is part of the API contract and controls what may be named across module boundaries.

## Runtime and Lowering Notes

Resolution determines declaration identity before type checking, lowering, static initialization, and symbol mangling.

## Diagnostics

Use for ambiguous names, inaccessible entities, unresolved paths, invalid public using of non-public items, and namespace-kind mismatches.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Names/**`
- `HelloUltraviolet/Source/Reference/Modules/Imports.uv`
- `HelloUltraviolet/Source/Reference/Modules/Usings.uv`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `7` for exact name lookup and visibility rules.
