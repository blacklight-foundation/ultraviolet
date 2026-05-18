# SPEC Clarifications Resolved

Objective: record the approved specification clarification decisions that closed
the `HelloUltraviolet` reference-corpus exercise blockers.

All items from the former active clarification surface have an approved decision
and a conformance route in the specification, obligation ledger, reference
corpus, or bootstrap compiler implementation.

## Resolved Items

- Const Permission Mutation Diagnostic Ownership:
  Assignment to an immutable binding root is a binding-state violation. Mutation
  rejected by an effective `const` permission remains a permission violation.
- Generic Record Literal Construction:
  Expected `TypeApply` instantiates generic record declarations, enum record
  payload literals, and modal state record literals under defaulted generic
  arguments.
- Overloaded Procedure Symbol Identity:
  Overload resolution records the selected declaration identity. Lowering
  mangles or exports the selected declaration.
- `rule.18.BlockInfo-Res-Err`:
  Classified as internal correctness, parser recovery, and compiler AST
  validation evidence; reference-model evidence closes the row.
- `rule.14.Impl-Orphan-Err`:
  Classified as project/import metadata or internal model evidence for foreign
  implementation relations owned by a foreign assembly.
- `req.14.ImplementationOrphanRequirement`:
  Accepted evidence uses local record, enum, and modal declarations implementing
  imported classes.
- `rule.24.LowerIR-Err`:
  Classified as backend evidence after semantic success using deterministic
  backend harness or injected invalid IR declaration evidence.
- `rule.24.EmitObj-Err`:
  Reserved for verifier, target-machine, or object-emission failure after an
  LLVM module or LLVM text exists.
- `rule.20.WorkgroupSize-Err`:
  `TopologyValid` requires positive workgroup dimensions, product at most
  `MAX_WORKGROUP_SIZE`, and global size derived from workgroup size multiplied
  by workgroup count.
- Fresh Async Creation Permission:
  Fresh async creation may check against an expected permission-qualified async
  modal state when ordinary permission introduction allows the fresh value at
  that permission.
- Region Lifecycle Transition Permission:
  Unique region lifecycle transitions return the target region state at
  `unique` permission and preserve region authority.
- Contract Predicate Comptime Procedure Context:
  Contract predicate checking is a static verification context that may evaluate
  pure compile-time procedures with compile-time-valid argument and result
  types when the predicate result is `bool`.
- `WF-Union-TooFew`:
  Classified as AST, recovery, or reference-model evidence.
- Hex Digit Underscores Adjacent To `E`:
  Exponent-adjacent underscore rules apply only to exponent components of
  exponent-bearing literals; `E` in a hexadecimal integer core is a digit.
- `TupleIndex-NonConst`:
  Classified as AST, recovery, or reference-model evidence.
- `Enum-Disc-NotInt`:
  Classified as AST, recovery, or reference-model evidence.
- `Enum-Disc-Negative`:
  Classified as AST, recovery, or reference-model evidence under the current
  unsigned discriminant grammar.
- `TypeAlias-Recursive-Err`:
  Public spelling is `TypeAlias-Recursive-Err`, mapped to `E-TYP-1506`.
- Qualified Record Pattern Versus Enum Record Payload Pattern:
  Resolution selects record patterns for record type paths and enum record
  payload patterns for enum variants; overlapping availability is ambiguous.
- Empty Tuple Pattern Unit Type:
  `TuplePattern([])` matches `TypePrim("()")` and binds no names.
- Static Rule Diagnostic Code Assignment:
  Missing explicit return type uses `E-TYP-1508`; non-boolean contract
  predicates use `E-SEM-2808`; unsafe transmute remains `E-MEM-3030`.
- Direct Shared Mutation And Implicit Key Acquisition:
  Direct shared-place mutation is valid when Chapter 19 forms a write key path
  and no key-scope, escape, or conflict rule rejects it; ordinary access
  lowering participates in the same acquire/wait model.
- Procedure Call Postcondition Facts:
  Statically resolved calls add callee postconditions to the caller proof
  context on normal return when substitutions are stable.
- Generic Variance Source And Diagnostic Codes:
  User generic parameters default invariant; built-in async variance is owned by
  dedicated subtyping rules; `E-TYP-1520` is invariant exact-match failure and
  `E-TYP-1521` is covariance or contravariance failure.
- Noncapturing Closure Function Type And Closure Value Representation:
  Noncapturing closures synthesize `TypeFunc` without a closure expected type or
  under a function-shaped expected type, and materialize empty-environment
  `TypeClosure` values under an explicit closure expected type.
