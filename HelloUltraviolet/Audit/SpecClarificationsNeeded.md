# SPEC Clarifications Needed

Objective: record obligation rows whose source-level exercise path is unclear
while building `HelloUltraviolet` as the reference corpus.

## Current Items

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
