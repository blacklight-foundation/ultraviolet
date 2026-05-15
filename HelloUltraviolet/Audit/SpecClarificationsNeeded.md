# SPEC Clarifications Needed

Objective: record obligation rows whose source-level exercise path is unclear
while building `HelloUltraviolet` as the reference corpus.

## Current Items

### Const Permission Mutation Diagnostic Ownership

- Obligations: `Docs/Audit/UltravioletObligations.csv:1773` and the Chapter 18
  assignment diagnostic rows for `Assign-Const-Err`.
- SPEC anchors: `SPECIFICATION.md:7428` and
  `SPECIFICATION.md:19604-19608`.
- Current reading: `E-TYP-1601` is the Chapter 10 permission-admissibility
  diagnostic for mutation through an aggregate permission-qualified path such
  as `cell.value = 2` where `cell` has `const PermissionDiagnosticCell`.
  `E-SEM-3132` is the Chapter 18 statement diagnostic for assigning directly
  to a root binding whose own type is `const`, such as `value = 2` where
  `value: const i32`.
- Connected construct reading: Chapter 10 owns the permission regime and
  admissibility matrix, while Chapter 18 owns assignment statement typing.
  Splitting root binding assignment from aggregate path mutation preserves both
  diagnostic surfaces and keeps the permission-specific fixture sourceable.
- Clarification requested: state whether this split is the intended diagnostic
  ownership boundary, or whether one of `E-TYP-1601` / `E-SEM-3132` should be
  retired or restricted to a narrower condition.

### Generic Record Literal Construction

- Obligation: accepted-source coverage for generic nominal type application and
  record construction in the Chapter 14/Chapter 16 intersection.
- SPEC anchors: `SPECIFICATION.md:12686-12718` and
  `SPECIFICATION.md:16710-16723`.
- Current reading: generic type application is a type form
  (`generic_type_use ::= type_path generic_args`), while record literal
  construction is currently specified as `identifier "{" field_init_list "}"`
  or a state-specific type followed by a field initializer list. The grammar
  therefore clearly admits `GenericCarrier<i32>` in type positions such as
  `sizeof(GenericCarrier<i32>)`, and admits the bare record-literal spelling
  `GenericCarrier { value: 1, tag: 2 }`. The current reference corpus exercises
  that bare spelling under explicit expected types `GenericCarrier<i32>` and
  `GenericCarrier<i32, bool>`.
- Connected construct reading: the intended language appears to construct
  instantiated generic records by using the bare record identifier and
  receiving type arguments from the expected type. What remains unclear is
  whether an explicit type-argument spelling such as
  `GenericCarrier<i32> { value: 1, tag: 2 }` is intentionally outside the
  non-modal record literal grammar or should be admitted.
- Clarification requested: specify the canonical source spelling and typing
  rule for constructing generic record, enum-record, and modal-state values
  whose nominal type has explicit generic arguments.

### `rule.18.BlockInfo-Res-Err`

- Obligation: `Docs/Audit/UltravioletObligations.csv:4143`.
- SPEC anchor: `SPECIFICATION.md:19106-19113`.
- Current reading: `BlockInfo-Res-Err` is intended to reject a block expression
  when statement typing contributes a non-empty `Res` set whose result entries
  have no common type.
- Connected construct reading: the inspected Chapter 18 statement forms produce
  `Res = []` for expression, unsafe, region, and frame statements, and loop
  `break` values flow through `Brk`. The bootstrap owner path,
  `TypeBlockInfo` in
  `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/stmt_common.cpp`, forwards
  nested statement-block results of type `!` into `flow.results`; that gives a
  source path for `BlockInfo-Res`, but has not produced a heterogeneous
  non-empty `Res` set for `BlockInfo-Res-Err`.
- Clarification requested: identify the source construct that can create a
  heterogeneous non-empty `Res` set, or mark `BlockInfo-Res-Err` as an internal
  consistency diagnostic rather than a source-level diagnostic obligation.

### `rule.14.Impl-Orphan-Err` and `req.14.ImplementationOrphanRequirement`

- Obligations: `Docs/Audit/UltravioletObligations.csv:2994` and
  `Docs/Audit/UltravioletObligations.csv:2999`.
- SPEC anchors: `SPECIFICATION.md:13104-13111` and
  `SPECIFICATION.md:13242-13257`.
- Current reading: the orphan requirement is a coherence rule over
  implementation relations, requiring at least one side of `T <: Cl` to be
  defined in the current assembly.
- Connected construct reading: the source syntax attaches implementation to the
  defining record, enum, or modal declaration, and `SPECIFICATION.md:13111`
  says standalone extension implementation blocks are outside the language
  surface. Under that surface, ordinary source can express the valid
  cross-assembly case where a local type implements an imported class; it
  cannot spell a rejecting case where both the implementing type and the class
  are foreign. `Fixtures/AcceptedProjects/CrossAssemblyImplementation`
  exercises the valid cross-assembly source surface.
- Clarification requested: identify an existing source spelling that presents a
  foreign implementing type and foreign class to the current assembly, or mark
  the rejecting orphan rule as an internal/imported-metadata consistency
  diagnostic with its own non-source fixture class.

### `rule.24.LowerIR-Err`

- Obligation: `Docs/Audit/UltravioletObligations.csv:5457`.
- SPEC anchor: `SPECIFICATION.md:27263-27270`.
- Current reading: `LowerIR-Err` is the backend owner for a failed
  `LowerIRDecl(d_i)` while lowering module IR to LLVM IR.
- Connected construct reading: a semantically valid source program should
  lower to valid LLVM IR for the selected target profile. When spec-valid
  source reaches `LowerIR-Err`, the bootstrap compiler has failed to lower a
  valid IR declaration and the canonical lowerer should be repaired. Invalid
  source that leaves `ErrorExpr` or `ErrorStmt` in the AST can also reach this
  owner, but that does not exercise the language construct as a spec-valid
  source specimen.
- Clarification requested: classify this obligation as an internal IR
  lowering failure fixture, or identify a spec-defined source-level condition
  that is semantically valid and is nevertheless required to report
  `LowerIR-Err`.

### `rule.24.EmitObj-Err`

- Obligation: `Docs/Audit/UltravioletObligations.csv:5464`.
- SPEC anchor: `SPECIFICATION.md:27291-27300`.
- Current reading: `EmitObj-Err` owns failure of `LLVMEmitObj_21(LLVMIR)` after
  a module has already been lowered to LLVM IR.
- Connected construct reading: the SPEC target profiles are
  `x86_64-sysv`, `x86_64-win64`, and `aarch64-aapcs64`, and the bootstrap
  compiler now initializes native x86-64 and AArch64 target support for those
  profiles. Under that target set, a valid module should produce object bytes.
  Tool-resolution failures, IR-render failures, and object-file write failures
  are owned by adjacent output rules rather than `EmitObj-Err`.
- Clarification requested: define a deterministic conformance fixture class for
  `LLVMEmitObj_21` failure, or mark `EmitObj-Err` as an internal backend
  failure obligation that is not sourceable from conforming source under the
  supported target profiles.

### `rule.20.WorkgroupSize-Err`

- Obligation: `Docs/Audit/UltravioletObligations.csv:4630`.
- SPEC anchors: `SPECIFICATION.md:21531-21545` and
  `SPECIFICATION.md:21831-21837`.
- Current reading: `WorkgroupSize-Err` rejects GPU dispatch or parallel
  topology whose workgroup size exceeds the device maximum. The reference
  corpus uses explicit `(1024usize, 1usize, 1usize)` workgroups for accepted
  GPU specimens because `MAX_WORKGROUP_SIZE = 1024`.
- Connected construct reading: `TopologyValid(topo)` is currently written as
  `topo.WorkgroupSize.0 * topo.WorkgroupSize.1 * topo.WorkgroupSize.2 =
  MAX_WORKGROUP_SIZE`, while `DEFAULT_GPU_WORKGROUP = (64, 1, 1)`. Taken
  literally, the default topology is invalid even though the surrounding
  dynamic semantics define it as the fallback for omitted workgroup options.
- Clarification requested: specify whether topology validity requires
  workgroup volume to be less than or equal to `MAX_WORKGROUP_SIZE`, or whether
  the default GPU workgroup should be changed to a volume of 1024.

### Fresh Async Creation Permission

- Obligation: accepted-source coverage for `Async@Suspended.resume` and manual
  stepping in Chapter 21.
- SPEC anchors: `SPECIFICATION.md:22695-22734`,
  `SPECIFICATION.md:23718`, and `SPECIFICATION.md:24369-24373`.
- Current reading: `Async@Suspended.resume` is a state method with a `unique`
  receiver, and async procedure calls allocate a fresh async frame with no
  aliases. A freshly-created async value can therefore materialize at an
  explicitly expected `unique Async<...>` type, matching the existing
  fresh-literal permission materialization rule used for aggregate values.
- Connected construct reading: without this materialization rule, the specified
  manual stepping surface cannot be written for an async value produced by an
  ordinary async call, because unqualified types default to `const` and
  permission regimes are not implicitly coerced.
- Clarification requested: state directly whether `AsyncCreateExpr` may
  materialize at an expected permission-qualified async type, especially
  `unique Async<...>`, or whether `resume` should use a different receiver
  permission.

### Region Lifecycle Transition Permission

- Obligations: `Docs/Audit/UltravioletObligations.csv:2494`,
  `Docs/Audit/UltravioletObligations.csv:2495`, and
  `Docs/Audit/UltravioletObligations.csv:2496`.
- SPEC anchors: `SPECIFICATION.md:10881-10883`,
  `SPECIFICATION.md:10892-10895`, and `SPECIFICATION.md:11364`.
- Current reading: `Region::reset_unchecked`, `Region::freeze`, and
  `Region::thaw` are lifecycle transitions on a unique region receiver, but
  their formal `RegionProcSig` return types are bare modal-state types rather
  than `unique Region@...`.
- Connected construct reading: follow-up region lifecycle operations require a
  unique receiver, and the region arena requirements say active or frozen
  regions must be freed exactly once. With the bare return types read
  literally, accepted source can create and free an active scoped region, but
  cannot write a full reset/freeze/thaw/free chain that both typechecks and
  preserves the required cleanup authority.
- Clarification requested: specify whether these consumed unique region
  lifecycle transitions preserve unique permission on their returned target
  state, or whether the built-in receiver/return signatures should be changed
  so the full lifecycle chain is sourceable.

### Contract Predicate Comptime Procedure Context

- Obligations: accepted-source coverage for `rule.15.Pure-Comptime` inside
  ordinary procedure contract predicates.
- SPEC anchors: `SPECIFICATION.md:14931-14934`,
  `SPECIFICATION.md:24725-24728`, and `SPECIFICATION.md:24749-24794`.
- Current reading: an ordinary contract predicate is a static verification
  context. `Pure-Comptime` therefore permits a contract predicate to call a
  compile-time procedure when the call is used only to verify the contract
  predicate; runtime procedure bodies still follow the Chapter 22 restriction
  on naming or calling compile-time procedures.
- Connected construct reading: Chapter 22 says `CtProc` declarations are
  Phase 2 bindings and do not survive into the expanded Phase 3 module set.
  Chapter 15 nevertheless gives `Pure-Comptime` as an ordinary contract
  purity rule. The intended implementation model is to remove the compile-time
  procedure from runtime items while retaining enough compile-time procedure
  metadata for contract purity and contract expression typechecking.
- Clarification requested: state directly that contract predicate checking is
  an allowed static context for `Pure-Comptime`, and that implementations may
  retain non-runtime compile-time procedure metadata after expansion for that
  analysis.

### `WF-Union-TooFew`

- Obligation: `Docs/Audit/UltravioletObligations.csv:2400`.
- SPEC anchor: `SPECIFICATION.md:10398-10401`.
- Current reading: `WF-Union-TooFew` is a semantic well-formedness rule for an
  already-formed `TypeUnion([T_1, ... T_n])` with fewer than two members.
- Connected construct reading: ordinary source union syntax is defined as
  `non_perm_type ("|" non_perm_type)+`, so source that parses as a union type
  already contains at least two member positions. A text such as `type T = i32 |`
  is a parse error before semantic union well-formedness receives a
  single-member `TypeUnion`.
- Clarification requested: identify the source or recovery path that should
  construct a fewer-than-two-member `TypeUnion`, or classify
  `WF-Union-TooFew` as an internal AST/recovery consistency diagnostic rather
  than an ordinary source-level diagnostic obligation.

### `TupleIndex-NonConst`

- Obligation: `Docs/Audit/UltravioletObligations.csv:2125`.
- SPEC anchor: `SPECIFICATION.md:8911`, `SPECIFICATION.md:9048-9052`, and
  `SPECIFICATION.md:16038`.
- Current reading: `TupleIndex-NonConst` is a semantic diagnostic for a tuple
  projection AST whose index is not a compile-time constant tuple index.
- Connected construct reading: ordinary source tuple projection grammar is
  `postfix_expr "." int_literal`, and the formal definition
  `ConstTupleIndex(i) <=> exists n in Z. i = n` makes every source-level tuple
  projection index constant by construction. Expressions such as `value.index`
  are field access forms, while `value.(index)` does not match the tuple
  projection grammar.
- Clarification requested: identify the source or recovery path that should
  construct a tuple projection with a non-constant index, or classify
  `TupleIndex-NonConst` as an internal AST/recovery consistency diagnostic
  rather than an ordinary source-level diagnostic obligation.

### `Enum-Disc-NotInt`

- Obligation: `Docs/Audit/UltravioletObligations.csv:2368`.
- SPEC anchor: `SPECIFICATION.md:2211`, `SPECIFICATION.md:10102-10105`,
  and `SPECIFICATION.md:10185-10189`.
- Current reading: `Enum-Disc-NotInt` is a semantic consistency diagnostic for
  an enum variant discriminant token whose kind is not `IntLiteral`.
- Connected construct reading: ordinary enum discriminant source parses through
  `Parse-VariantDiscriminantOpt-Yes`, and that rule consumes the token only
  when `t.kind = IntLiteral`. The lexical grammar for `integer_literal` has no
  non-integer alternative at this position. A source form such as `Case = name`
  therefore fails during parsing or recovery before semantic discriminant
  validation receives a non-int discriminant token.
- Clarification requested: identify the source or recovery path that should
  construct an enum discriminant token whose kind is not `IntLiteral`, or
  classify `Enum-Disc-NotInt` as an internal AST/recovery consistency
  diagnostic rather than an ordinary source-level diagnostic obligation.

### `Enum-Disc-Negative`

- Obligation: `Docs/Audit/UltravioletObligations.csv:2370`.
- SPEC anchor: `SPECIFICATION.md:2211`, `SPECIFICATION.md:10102-10105`,
  and `SPECIFICATION.md:10195-10199`.
- Current reading: `Enum-Disc-Negative` is a semantic consistency diagnostic
  for an enum variant discriminant token whose integer value is negative.
- Connected construct reading: ordinary source discriminants are parsed as
  `integer_literal`, and `integer_literal` has no leading sign. A text such as
  `Case = -1` is an operator token followed by an integer literal, not a
  single negative `IntLiteral` consumed by `Parse-VariantDiscriminantOpt-Yes`.
- Clarification requested: specify whether enum discriminants intentionally
  admit signed integer literals, or classify `Enum-Disc-Negative` as an
  internal AST/recovery consistency diagnostic rather than an ordinary
  source-level diagnostic obligation.
