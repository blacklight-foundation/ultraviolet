# Chapter 24: Lowering, Lifecycle, and Backend

## Load When

Use for initialization lifecycle, layout, ABI, symbols, mangling, cleanup, drop, unwinding, runtime interface, LLVM/backend requirements, binding storage, validity, vtables, and poison instrumentation.

## Authoring Rules

- Model ownership and drop behavior correctly in source so cleanup lowering is predictable.
- Keep public ABI-facing types and procedures stable and explicit.
- Use modal/resource lifecycle types to make initialization and cleanup visible.
- When diagnosing compiler bugs, map symptoms to parser, resolver, typechecker, lowering, runtime, or backend owner.

## Syntax Forms

Source forms are owned by earlier chapters. This chapter explains how those forms lower.

## Static Semantics

Lowering consumes resolved, typed, permission-checked, provenance-checked, and verified source. Responsible bindings register cleanup; moved bindings skip drop; partially moved aggregates drop remaining children.

## Runtime and Lowering Notes

Runtime symbols include panic, managed string/bytes drop hooks, region operations, capability methods, and built-in modal procedures. Backend requirements cover ABI, pointer attributes, memory intrinsics, validity state, calls, vtables, literals, and poison instrumentation.

## Diagnostics

Use for initialization, layout, ABI, symbol, cleanup, runtime interface, backend, and output diagnostics.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Lowering/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `24` and Appendix D for lowering, runtime, layout, and ABI behavior.
