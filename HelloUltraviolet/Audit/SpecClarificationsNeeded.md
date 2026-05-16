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

### Overloaded Procedure Symbol Identity

- Obligations: `Docs/Audit/UltravioletObligations.csv:3298`,
  `Docs/Audit/UltravioletObligations.csv:3301`,
  `Docs/Audit/UltravioletObligations.csv:3302`, and
  `Docs/Audit/UltravioletObligations.csv:5551`.
- SPEC anchors: `SPECIFICATION.md:14739-14752`,
  `SPECIFICATION.md:14756-14760`, and
  `SPECIFICATION.md:27857-27880`.
- Current reading: free-procedure overload resolution selects a unique
  declaration before lowering, and lowering consumes that selected declaration
  identity. For same-name overloads without an explicit external link name,
  the backend must preserve declaration identity in its internal symbol table
  so the selected overload body is the body that executes.
- Connected construct reading: Chapter 15 permits same-name overloads whose
  erased parameter signatures differ and requires no runtime overload search.
  Chapter 24 defines ordinary procedure item paths as module path plus name.
  Taken literally, that item path is insufficient to identify multiple
  same-module procedure declarations with the same name, so the implementation
  needs either a specified overload symbol component or an explicit statement
  that the selected symbol is a declaration identity rather than only
  `PathOfModule(ModuleOf(proc)) ++ [name]`.
- Clarification requested: specify the canonical internal symbol identity for
  overloaded free procedures, especially whether non-exported overload symbols
  include an overload-set component derived from the selected declaration while
  `[[mangle]]`, `[[export]]`, `[[host_export]]`, and `main` keep their existing
  ABI-facing names.

### `rule.18.BlockInfo-Res-Err`

- Obligation: `Docs/Audit/UltravioletObligations.csv:4143`.
- SPEC anchor: `SPECIFICATION.md:19091-19121`.
- Current reading: `BlockInfo-Res-Err` is intended to reject a block expression
  when statement typing contributes a non-empty `Res` set whose result entries
  have no common type.
- Connected construct reading: the inspected Chapter 18 statement rules produce
  `Res = []` for let, var, assignment, expression, defer, region, frame, return,
  continue, and unsafe statements; `break` values flow through `Brk`, not `Res`;
  and `CtStmt` is expanded away before it contributes a runtime statement. The
  SPEC does not currently expose a statement rule that appends a non-`!` type to
  `Res`.
- Bootstrap evidence: `TypeBlockInfo` in
  `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/stmt_common.cpp` checks
  `ResType(stmts_typed.flow.results)` and can emit `BlockInfo-Res-Err` when
  `flow.results` is heterogeneous. The source-facing bootstrap producers
  inspected so far append to `flow.results` only when a nested statement-block
  body has type `!`: `TypeScopedStmtBody`, `unsafe_block_stmt.cpp`, and
  `key_block_stmt.cpp`. Expression-statement flow collection recurses into
  nested block expressions only to forward nested statement flow; ordinary tail
  expression values do not populate `Res`.
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
  defining record, enum, or modal declaration, and `SPECIFICATION.md:13116`
  says standalone extension implementation blocks are outside the language
  surface. Under that surface, ordinary source can express the valid
  cross-assembly case where a local record, enum, or modal type implements an
  imported class; it cannot spell a rejecting case where both the implementing
  type and the class are foreign.
  `Fixtures/AcceptedProjects/CrossAssemblyImplementation` exercises the valid
  cross-assembly source surface for all three implementer forms.
- Clarification requested: identify an existing source spelling that presents a
  foreign implementing type and foreign class to the current assembly, or mark
  the rejecting orphan rule as an internal/imported-metadata consistency
  diagnostic with its own non-source fixture class.

### `rule.24.LowerIR-Err`

- Obligation: `Docs/Audit/UltravioletObligations.csv:5457`.
- SPEC anchor: `SPECIFICATION.md:27271-27279`.
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
- Bootstrap owner path:
  `LLVMBootstrap/cursive/src/06_driver/pipeline.cpp` records `LowerIR-Err`
  when module materialization fails before an LLVM module is available.

### `rule.24.EmitObj-Err`

- Obligation: `Docs/Audit/UltravioletObligations.csv:5464`.
- SPEC anchor: `SPECIFICATION.md:27297-27307`.
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
- Bootstrap owner path:
  `LLVMBootstrap/cursive/src/06_driver/pipeline.cpp` records `EmitObj-Err`
  for LLVM module verification failure, target-machine creation failure, and
  object-emission pass setup failure after a module exists.

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

### Hex Digit Underscores Adjacent To `E`

- Obligation: accepted-source literal lexing coverage for
  `hex_integer`, `hex_digit`, `NumericUnderscoreOk`, and
  `Lex-Numeric-Err`.
- SPEC anchors: `SPECIFICATION.md:2111`,
  `SPECIFICATION.md:2211-2215`, and
  `SPECIFICATION.md:2272-2276`.
- Current reading: uppercase `A` through `F` are valid `hex_digit` characters,
  and underscores are valid separators between based-integer digits. The
  `AdjacentExponentUnderscore` predicate is intended to protect decimal float
  exponent spelling, not to make valid uppercase hex digit sequences such as
  `0xDE_AD` malformed.
- Connected construct reading: the literal grammar and `HexRun` both treat
  uppercase `E` as a hex digit in based integer context. Applying
  `AdjacentExponentUnderscore` to every numeric lexeme makes `E` act as an
  exponent marker even when the scanner is inside a `0x` based integer, which
  conflicts with the based-integer grammar. The compiled reference source uses
  `0xCA_FEu32` to exercise uppercase grouped hex without relying on the
  disputed adjacency.
- Clarification requested: scope `AdjacentExponentUnderscore` to decimal
  float exponent cores, or state directly that based integer underscores are
  forbidden next to the hex digits `e` and `E` despite the `hex_integer`
  grammar.

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

### `TypeAlias-Recursive-Err`

- Obligation: `Docs/Audit/UltravioletObligations.csv:2442`.
- SPEC anchor: `SPECIFICATION.md:10644-10652` and
  `Docs/Internal/UltravioletSpecification.obligations.md:41505-41518`.
- Current reading: recursive type aliases are sourceable through ordinary
  `type` declarations and report `TypeAlias-Recursive-Err`, whose diagnostic
  code is `E-TYP-1506` through the Chapter 8 core type diagnostics table.
- Connected construct reading: the public SPEC rule conclusion currently says
  `Code(TypeAlias-Reultraviolet-Err)`, while the extracted internal
  obligation ledger and CSV row consistently use `TypeAlias-Recursive-Err`.
  The intended rule identity is `TypeAlias-Recursive-Err`; the public spelling
  appears to be a mechanical replacement typo in the generated SPEC text.
- Clarification requested: correct the public SPEC spelling from
  `TypeAlias-Reultraviolet-Err` to `TypeAlias-Recursive-Err` so the public
  rule name matches the obligation ledger and the diagnostic fixture.

### Qualified Record Pattern Versus Enum Record Payload Pattern

- Obligation: accepted-source coverage for `record_pattern`, `enum_pattern`,
  `Parse-Pattern-Record`, and `Parse-Pattern-Enum`.
- SPEC anchor: `SPECIFICATION.md:18216-18234`,
  `SPECIFICATION.md:18379-18394`, and `SPECIFICATION.md:30517-30521`.
- Current reading: `QualifiedDataTypes::RecordReference { ... }` is a record
  pattern when the joined `type_path` names a record. `EnumType::Variant { ... }`
  is an enum pattern when the prefix resolves to an enum type and the final
  identifier names a variant.
- Connected construct reading: both record patterns and enum record-payload
  patterns can have the token shape `A::B { ... }`. The parser cannot fully
  disambiguate that spelling from tokens alone when `A` might be a module
  alias or a type name. The intended reading follows the language's qualified
  disambiguation design: resolution decides whether the prefix is an enum type
  or whether the joined path is a record type.
- Clarification requested: state the disambiguation rule for `A::B { ... }`
  in pattern position directly, including module aliases, record type paths,
  and enum variant record payloads.

### Empty Tuple Pattern Unit Type

- Obligations: `Docs/Audit/UltravioletObligations.csv:3968`,
  `Docs/Audit/UltravioletObligations.csv:3985`, and
  `Docs/Audit/UltravioletObligations.csv:3992`.
- SPEC anchors: `SPECIFICATION.md:9020`,
  `SPECIFICATION.md:18216-18242`, and `SPECIFICATION.md:18317-18364`.
- Current reading: `()` in pattern position is the empty tuple pattern and is
  the pattern form for the unit value whose source expression type is
  `TypePrim("()")`.
- Connected construct reading: source has no separate unit-pattern production.
  `Parse-TuplePatternElems-Empty` creates `TuplePattern([])` for `()`, and
  `T-Tuple-Unit` types the corresponding empty tuple expression as the unit
  primitive. The intended design therefore treats `TuplePattern([])` against
  `TypePrim("()")` as the zero-element tuple-pattern case, with
  `Match-Tuple` binding the empty environment.
- Clarification requested: add an explicit static rule for
  `TuplePattern([]) ◁ TypePrim("()")`, or state a different source spelling for
  matching unit values if empty tuple patterns are intended to exclude unit.

### Static Rule Diagnostic Code Assignment

- Obligations: `Docs/Audit/UltravioletObligations.csv:3192`,
  `Docs/Audit/UltravioletObligations.csv:3198`,
  `Docs/Audit/UltravioletObligations.csv:3314`, and
  `Docs/Audit/UltravioletObligations.csv:3662`.
- SPEC anchors: `SPECIFICATION.md:14263`,
  `SPECIFICATION.md:14289-14293`, `SPECIFICATION.md:14821-14825`,
  `SPECIFICATION.md:16574-16578`, `SPECIFICATION.md:15523-15547`, and
  `SPECIFICATION.md:4640-4650`.
- Current reading: missing procedure return annotations and non-boolean
  contract predicates are sourceable static rejections with required
  diagnostics, but the Chapter 15 diagnostic supplement does not assign a
  concrete diagnostic code to either condition. These remain uncoded static
  diagnostics in the reference corpus until the SPEC assigns a code.
  `Transmute-Unsafe-Err` is sourceable and its rule asks for
  `Code(Transmute-Unsafe-Err)`; the connected Chapter 6 unsafe-operation
  diagnostic `E-MEM-3030` is the intended code because the rejection is use of
  an unsafe operation outside an unsafe span.
- Connected construct reading: Chapter 15 already assigns separate codes for
  non-unit procedures lacking explicit trailing `return`, contract proof
  failures, and contract purity. Reusing those rows for missing annotations or
  non-boolean predicates would collapse distinct obligations. Chapter 16 owns
  transmute size, alignment, invalid-target warnings, and invalid casts, while
  Chapter 6 owns the generic unsafe-operation boundary.
- Clarification requested: assign explicit codes for
  `WF-ProcedureDecl-MissingReturnType`, `ReturnAnnOk`, and the non-boolean
  `WF-Contract` failure, and state whether `Code(Transmute-Unsafe-Err)` is
  `E-MEM-3030` or a newly assigned expression diagnostic.

### Direct Shared Mutation And Implicit Key Acquisition

- Obligations: `Docs/Audit/UltravioletObligations.csv:1773`,
  `Docs/Audit/UltravioletObligations.csv:4394`,
  `Docs/Audit/UltravioletObligations.csv:4409`,
  `Docs/Audit/UltravioletObligations.csv:4410`, and
  `Docs/Audit/UltravioletObligations.csv:4461`.
- SPEC anchors: `SPECIFICATION.md:7431`,
  `SPECIFICATION.md:20380-20386`, `SPECIFICATION.md:20528-20534`,
  and `SPECIFICATION.md:20786-20792`.
- Current reading: an ordinary direct field mutation through a `shared` path
  is permitted when `KeyPath(e)` and `RequiredMode(e)` are defined and no
  Chapter 19 scope or escape rule forbids the access. If the path is not
  already covered by a held key, `Lower-KeyAccess-Uncovered` establishes an
  implicit acquisition in the current scope.
- Connected construct reading: Chapter 10 says shared-operation admissibility
  determines whether the operation may proceed to key-mediated access, while
  Chapter 19 defines that key-mediated access and states that being outside an
  explicit `#` block does not itself make ordinary `shared` access invalid.
  The most coherent reading is that `E-TYP-1604` applies only when a direct
  shared field mutation cannot establish a valid key-mediated write at all,
  or when a more specific key-system rule such as read-then-write rejection
  owns the failure. It should not mean "outside an explicit key block" when an
  implicit acquisition is otherwise valid.
- Clarification requested: specify whether `E-TYP-1604` rejects all direct
  shared field mutation without a pre-existing covering write key, or only
  rejects direct shared field mutation for which Chapter 19 cannot establish a
  valid key context.

### Procedure Call Postcondition Facts

- Obligations: `Docs/Audit/UltravioletObligations.csv:3341`,
  `Docs/Audit/UltravioletObligations.csv:3352`,
  `Docs/Audit/UltravioletObligations.csv:3357`, and
  `Docs/Audit/UltravioletObligations.csv:3363`.
- SPEC anchors: `SPECIFICATION.md:15043-15057`,
  `SPECIFICATION.md:15286-15349`, and `SPECIFICATION.md:15418-15421`.
- Current reading: a statically verified procedure or method call is checked
  against the callee precondition, and the callee body is checked against its
  own postcondition, but the call expression does not automatically add the
  callee postcondition to `ProofContextAt(S)` for the caller.
- Connected construct reading: §15.8 defines `ProofContextAt(S)` as flow facts
  plus enclosing contract precondition facts, and it explicitly states that
  successful dynamic checks inject verification facts. It does not list static
  callee postconditions as generated facts after call or method-call
  expressions. The intended reading therefore appears to be that postcondition
  facts are local obligations unless a separate fact-generation rule is added.
- Clarification requested: state whether successful statically verified calls
  generate caller-visible verification facts from their callee postconditions,
  including dynamic class method calls whose selected implementation satisfies
  behavioral subtyping.

### Generic Variance Source And Diagnostic Codes

- Obligations: `Docs/Audit/UltravioletObligations.csv:1505`,
  `Docs/Audit/UltravioletObligations.csv:1506`,
  `Docs/Audit/UltravioletObligations.csv:1507`,
  `Docs/Audit/UltravioletObligations.csv:1508`,
  `Docs/Audit/UltravioletObligations.csv:1509`,
  `Docs/Audit/UltravioletObligations.csv:1510`,
  `Docs/Audit/UltravioletObligations.csv:1511`, and
  `Docs/Audit/UltravioletObligations.csv:1512`.
- SPEC anchors: `SPECIFICATION.md:6107-6133`,
  `SPECIFICATION.md:12475-12531`, `SPECIFICATION.md:12617-12619`,
  and `SPECIFICATION.md:6474-6480`.
- Current reading: `VarianceOf(path, i)` uses the effective semantic
  variance of the generic declaration parameter. Source generic parameter
  syntax does not provide `+`, `-`, `=`, or `±` markers, and
  `Parse-TypeParam` produces variance `⊥`; therefore the intended reading is
  that semantic analysis computes the variance from member positions before
  applying `Sub-Generic`.
- Connected construct reading: the formal `Sub-Generic-Invariant-Err`,
  `Sub-Generic-Covariant-Err`, and `Sub-Generic-Contravariant-Err` rules assign
  `E-TYP-1520` to invariant mismatch and `E-TYP-1521` to covariant or
  contravariant mismatch. The core type diagnostics table describes
  `E-TYP-1520` as the general variance-instantiation violation and
  `E-TYP-1521` as the invariant exact-match condition. The reference corpus
  follows the formal rules because those rules are closer to the checked
  subtyping judgment.
- Clarification requested: state whether variance is source-annotated or
  inferred, define where `params[i].variance` is populated, and align the
  `E-TYP-1520`/`E-TYP-1521` diagnostic table conditions with the formal
  subtyping error rules.

### Noncapturing Closure Function Type And Closure Value Representation

- Obligations: `Docs/Audit/UltravioletObligations.csv:3877`,
  `Docs/Audit/UltravioletObligations.csv:3919`, and
  `Docs/Audit/UltravioletObligations.csv:3928`.
- SPEC anchors: `SPECIFICATION.md:17699-17704`,
  `SPECIFICATION.md:17874-17894`, `SPECIFICATION.md:17960-17963`,
  and `SPECIFICATION.md:18007-18025`.
- Current reading: a noncapturing closure expression may type as `TypeFunc`
  in direct function contexts, while lowering still materializes it as a
  closure value with null environment when the expected or annotated target is
  `TypeClosure`.
- Connected construct reading: §16.9 assigns `TypeFunc` to noncapturing
  closures, but the runtime and lowering rules describe `ClosureVal(null, sym)`
  and `Lower-Closure-Call` for closure values. The coherent reading is that
  expression typing may expose the function type where no closure value is
  required, while checked conversion to a closure type keeps the closure pair
  representation and closure-call ABI.
- Clarification requested: state whether noncapturing closure expressions are
  always function-typed, contextually closure-typed when checked against
  `TypeClosure`, or function-typed with an explicit conversion rule to
  `ClosureVal(null, sym)`.
