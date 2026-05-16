# HelloUltraviolet Completion Audit

Objective: execute `.agents/plans/HelloUltravioletReferenceCorpus.md`.

## Current Checkpoint

- `Docs/Audit/UltravioletObligations.csv`,
  `HelloUltraviolet/Audit/UltravioletObligations.csv`, and the generated
  catalog currently contain 6,045 primary obligation rows.
- `HelloUltraviolet/Source/Reference` currently contains 154 `.uv` files,
  187 public `run...Reference` procedures, and 0 public `run...Reference`
  bodies whose implementation is only `return true`.
- `Source/Main.uv` executes `runReferenceCorpus(context)`, and `Source/Api.uv`
  directly calls all 187 public reference runners.
- `CatalogSourcePaths.uv` indexes 595 unique catalog source targets, and
  `CatalogSymbols.uv` indexes 602 compiled symbol target tuples. The async
  composition map, filter, take, fold, chain, and until obligations now name
  dedicated reference runners; aggregate combinator obligations name
  `runAsyncCompositionCombinatorsReference`.
- The generated catalog now records the exercise kind for each primary
  obligation row: 5,517 accepted-source rows, 45 accepted-project rows,
  438 rejected-source rows, 26 diagnostic-source rows, 11 artifact-behavior
  rows, and 8 reference-model rows. Fixture-backed rows point at compiled
  fixture validators instead of accepted-source reference runners.
- This pass corrected catalog targets for primitive, record, and union data types,
  remapped section-specific catalog entries to their dedicated reference files
  where those files already existed, and added row-level mappings for attribute
  grammar, using/import name references, and logical-line source-text rules.
- The catalog generator now preserves the split audit-group layout for 61 CSV
  membership groups and 61 primary-reference ordering groups, and generated
  symbol execution now passes `Context` to context-taking reference runners.
- The key-path reference source now includes a record field declared with a
  `#` key boundary, shared access through that field, a shared safe-pointer
  dereference key boundary, and an accepted `shared $Class` method call where
  every vtable-eligible method has a const receiver. The catalog rows for
  `Parse-KeyBoundaryOpt-Yes`, `Parse-KeyBoundaryOpt-No`, and
  `requirement.19.FieldKeyBoundary` point at `runKeysKeyPathsReference`; the
  same executable runner now directly covers the sourceable forms for
  `requirement.19.PointerDereferenceKeyAccess`,
  `requirement.19.SharedDynamicClassObjects`,
  `rule.19.K-Witness-Shared-WF`, and
  `def.19.SharedDynamicMethodCallKeyPath`.
- The key-system reference source now also exercises default read-mode key
  blocks, canonical sorting from an intentionally unsorted key-path list,
  key-block cleanup through direct `return`, dynamic and ordered same-base
  multi-index reads under `[[dynamic]]`, compound read-modify-write under a
  covering write key, release-write nested inside an outer read key,
  reentrant shared-parameter callee summary coverage, and a dedicated
  `keyModeContextValue` specimen for read/write context classification over a
  shared record path.
- Key memory-ordering source now exercises both expression-level memory-order
  overrides and key-block default memory-order attributes for `[[relaxed]]`,
  `[[acquire]]`, `[[release]]`, `[[acqrel]]`, and `[[seqcst]]`, plus
  `fence(acquire)`, `fence(release)`, and `fence(seqcst)`.
- The catalog generator now derives fixture-backed obligation targets from
  `Source/Fixtures/AcceptedProjects`, `Source/Fixtures/RejectedSource`,
  `Source/Fixtures/DiagnosticSource`, `Source/Fixtures/OutputDiagnostics`, and
  `Source/Fixtures/ArtifactProjects`, then emits the matching catalog helper
  and import surface for each generated catalog submodule.
- `Source/Audit/SpecClarifications.uv` now compiles a twenty-four-row clarification
  ledger for sourceability-limited, permission-clarity, and backend identity
  obligations. Six rows
  with no current primary source exercise are cataloged as `@ReferenceModel`;
  the orphan requirement row keeps its accepted-project source exercise and is
  also represented in the compiled clarification ledger. The workgroup-size
  row records the SPEC tension between `DEFAULT_GPU_WORKGROUP = (64, 1, 1)` and
  the formal `TopologyValid` equality against `MAX_WORKGROUP_SIZE = 1024`.
  The fresh async creation row records the intended reading that an async call
  creates a fresh frame that can materialize at an explicitly expected
  permission-qualified async type for manual `resume` stepping.
  The hex literal underscore row records the SPEC tension between uppercase
  `E` as a hex digit and exponent-adjacent underscore rejection.
  The region lifecycle row records the intended reading that consumed unique
  region lifecycle transitions preserve cleanup authority on the returned
  modal state. The contract predicate compile-time procedure row records the
  intended reading that contract predicate checking is a static context for
  `Pure-Comptime` while compile-time procedures remain absent from runtime
  items. The procedure-call postcondition row records the current reading that
  successful statically verified calls do not inject callee postconditions into
  the caller proof context unless the SPEC adds an explicit fact-generation
  rule. The union-too-few row records the current reading that ordinary source
  union grammar constructs at least two member positions before semantic union
  well-formedness runs, making the fewer-than-two-member `TypeUnion` rule an
  internal AST/recovery diagnostic unless the SPEC identifies a source recovery
  path. The tuple-index non-const row records the current reading that ordinary
  source tuple projection grammar uses an `int_literal`, making non-constant
  tuple projection an internal AST/recovery diagnostic unless the SPEC
  identifies a source recovery path. The enum discriminant not-int and
  negative rows record the current reading that ordinary source discriminants
  parse only unsigned `integer_literal` tokens, making those two semantic
  discriminant diagnostics internal AST/recovery diagnostics unless the SPEC
  identifies a source recovery path. The overloaded procedure symbol identity
  row records the current reading that Chapter 15 selected-procedure lowering
  requires backend declaration identity for same-name overloads, while Chapter
  24 should clarify the canonical internal symbol component used for those
  overload declarations. The recursive type-alias row records the public SPEC
  typo where `TypeAlias-Recursive-Err` appears as
  `TypeAlias-Reultraviolet-Err` in the generated public SPEC text, while the
  internal obligation ledger and CSV use the intended rule name.
  The empty tuple pattern row records the intended reading that `()` in pattern
  position is `TuplePattern([])` and matches the unit value typed as
  `TypePrim("()")`, while the SPEC should add an explicit static rule for that
  zero-element unit-pattern case.
  The static-rule diagnostic-code row records the current reading that
  missing return annotations and non-boolean contract predicates are sourceable
  uncoded static diagnostics until Chapter 15 assigns concrete codes, and that
  `Transmute-Unsafe-Err` uses the Chapter 6 unsafe-operation diagnostic
  `E-MEM-3030` unless the SPEC assigns a narrower expression diagnostic.
- `Fixtures/AcceptedProjects/HostedExportLibrary` is a spec-valid shared
  library fixture with public `[[host_export]]` procedures, projected context
  bundle records, visible primitive and C-layout aggregate parameters, a
  `C-unwind` catch export, a file-system capability projection, and Win64
  direct/indirect aggregate carrier coverage. The generated catalog maps 39
  hosted-export obligations to accepted-project metadata backed by this
  fixture.
- Accepted-project catalog rows now record the physical fixture source files
  that exercise each obligation, such as
  `Fixtures/AcceptedProjects/HostedExportLibrary/Source/Library.uv`, instead
  of using only the accepted-project metadata source as their catalog target.
- Artifact-project catalog rows now record the physical fixture source files
  that drive artifact behavior, such as
  `Fixtures/ArtifactProjects/EmitBcLibrary/Source/Library.uv`, instead of
  using only the artifact-project metadata source as their catalog target.
- Project output artifact source now exercises the Chapter 3.6 output model
  directly: default and configured output roots, object/IR/bin/lib directories,
  required-output counts, primary artifact selection, import-library emission,
  library artifact inputs, module emission ordering, root and non-root symbol
  names, target-profile suffix/triple/data-layout constants, and the
  `BMap`/module-path mangling shape used by object paths.
- Project assembly-model source now exercises assembly import graph behavior
  directly: import edge formation from module imports, dependency paths with no
  library interior, library-boundary cycles, executable-import rejection,
  hosted-library linked-library import rejection, linked-library
  classification, emitted assembly counts, and imported-library counts.
- Project manifest-model source now exercises manifest parsing and validation
  behavior directly: parsed, missing, and invalid manifest states; top-level
  key validation; assembly table count and duplicate-name rejection; required
  and optional assembly field checks; `emit_ir` and `link_kind` domains;
  executable `link_kind` misuse; source/output path relativity; toolchain
  target-profile selection; and build default handling for incremental and
  progress settings.
- Project module-discovery source now exercises the source-root, directory,
  compilation-unit, module-path, discovery-state, and assembly-ownership
  relations directly: root and relative module-path formation, skipped
  non-module directories, ordered source files, deterministic directory order,
  folded module-path collision detection, invalid keyword components,
  relativization failure, deepest unique owner selection, module filtering for
  nested source roots, and ambiguous ownership failure.
- Module-level `using` source now exercises the accepted §11.2 declaration
  forms directly: single-item aliasing, list specifiers, aliased and unaliased
  specifier names, trailing list commas, imported value/type/class binding, and
  compile-time-only `using` behavior with no runtime action.
- Module-level static source now exercises static declarations directly:
  identifier bindings, tuple-valued statics, tuple-pattern multi-binding
  statics, cross-static initialization from a procedure call, private mutable
  static storage, and runtime reads of destructured static binding names.
  `UVBOOT-0082` records the bootstrap repair that makes static value lookup use
  `PatNames` plus pattern typing for destructuring static declarations.
- Module-level extern-block source now exercises the §11.4 extern block shell
  directly: default ABI blocks, identifier ABI blocks, string ABI blocks,
  block-level `[[library]]` metadata, extern procedure item binding, C-unwind
  catch metadata, compile-time-only block behavior, unsafe imported calls, and
  linked execution through matching exported providers or a Windows system
  library import.
- Module-level import source now exercises the §11.1 import declaration shell
  directly: default module alias binding from the final path segment, explicit
  `as` aliases, explicit `internal` and `private` visibility on import
  declarations, qualified type and procedure lookup through imported module
  aliases, and compile-time-only import behavior with no runtime action.
- Attribute reference source now exercises concrete attribute syntax and
  behavior directly: declaration attributes on records, fields, enums, type
  aliases, procedures, methods, and modal declarations; binding attributes;
  statement and expression attributes on `comptime`; `[[files]]`, `[[emit]]`,
  `[[deprecated]]`, `[[dynamic]]`, `[[stale_ok]]`, `[[inline]]`,
  `[[inline(default)]]`, `[[inline(always)]]`, `[[inline(never)]]`,
  `[[cold]]`, `[[layout(C)]]`, `[[layout(packed)]]`, `[[layout(C,
  align(16))]]`, and `[[layout(u8)]]`. The runtime runners verify the
  attributed procedures and methods execute, deprecated references emit their
  warning, dynamic key reads emit their info diagnostic, stale observations are
  explicitly marked, packed/aligned record layout is observable through
  `alignof`, and the integer-backed enum is observable through `sizeof`,
  `alignof`, construction, and pattern matching.
- Rejected-source and diagnostic-source catalog rows now record the physical
  fixture source files that exercise each expected diagnostic or diagnostic
  absence, such as
  `Fixtures/RejectedSource/Expressions/OperatorOperandMismatch/Source/Main.uv`
  and
  `Fixtures/DiagnosticSource/Keys/StaleOkSuppressesReleaseWarning/Source/Main.uv`.
- The catalog generator now recognizes fixture index wrappers such as
  `parallelismSpecimenMatches(...)` in addition to direct specimen constructors.
  Structured-parallelism expected-diagnostic rows now point at the concrete
  rejected-source fixture files under `Fixtures/RejectedSource/Parallelism`
  rather than the accepted structured-parallelism reference runners.
- The catalog generator now also reads every physical fixture `Expected.uv`
  artifact when assigning fixture-backed obligation targets. Secondary
  expected-diagnostic obligations recorded beside a fixture's primary
  diagnostic now map to the same concrete source specimen instead of remaining
  on accepted-source reference runners.
- This pass surfaced and corrected a bootstrap semantic-analysis defect:
  long spec-valid binary expression chains in generated catalog code could
  overflow the compiler stack during resolution after name-map collection. The
  resolver now resolves same-operator binary chains iteratively in
  `resolve_expr.cpp` while preserving the existing region-allocation `^`
  special case and the original AST shape.
- This pass surfaced and corrected an async direct-call lowering defect:
  spec-valid async `chain` closure calls that return aggregate async values
  needed the expected callback return type to preserve the ABI sret return
  path. The fix in `direct.cpp` threads the expected return type through async
  map, filter, fold, and chain callback invocation.
- This pass surfaced and corrected a method-call typechecking defect:
  spec-valid `until` calls with closure parameter annotations using an
  unqualified type declared in the same module were compared before scoped type
  path resolution. The fix in `method_call.cpp` resolves comparable type paths
  in scope before checking predicate/action parameter compatibility.
- This pass surfaced and corrected generic monomorphization diagnostic defects:
  recursive generic procedure instantiation now reports `E-TYP-2307`, and a
  128-deep finite generic instantiation chain reports `E-TYP-2308`. The fix in
  `lower/expr/call.cpp` tracks active generic declaration frames during
  lowering and emits the SPEC diagnostics before lowering continues, while the
  Windows bootstrap executable link settings now reserve enough native stack
  for the SPEC-defined 128-instantiation boundary.
- This pass promoted the existing GPU execution-domain and GPU-capture
  references into the accepted runtime flow. `runParallelismExecutionDomainsReference`
  now executes `parallel context~>gpu()` with GPU dispatch, barrier, intrinsic,
  and reduction behavior; `runParallelismCaptureSemanticsReference` now executes
  a `GpuSafe` captured payload through a GPU dispatch body.
- This pass promoted drop-bearing by-value FFI into the accepted runtime flow.
  `runFFIFfiSafeReference` now constructs a `[[ffi_pass_by_value]]`
  `FFIDroppingRecord` and moves its unique field payload, `runFFIExternProceduresReference`
  now calls an `extern "C"` imported procedure with a moved drop-bearing record
  and a same-image exported provider, and `runFFIExportedProceduresReference`
  now executes the raw exported drop-bearing procedure and observes the moved
  payload value.
- This pass expanded structured-parallelism accepted source specimens.
  `runParallelismParallelBlocksReference` now executes a full parallel option
  list with `name`, `cancel`, `workgroup`, and `workgroups` using the SPEC
  multiline trailing-comma form. `runParallelismSpawnReference` now executes a
  multiline trailing-comma spawn option list with `name`, `affinity`, and
  `priority`. `runParallelismDispatchReference` now executes a multiline
  trailing-comma dispatch option list with `reduce`, `chunk`, and `ordered`.
  `runParallelismExecutionDomainsReference` now executes both
  `context~>cpu(mask)` and `context~>cpu(mask, priority)` in accepted runtime
  source.
- This pass surfaced and corrected the bootstrap compiler/runtime gap for
  SPEC-valid configured CPU domains. Method-call lowering now uses the
  `Context` method signature for `cpu(mask)` and `cpu(mask, priority)`, the
  runtime has a configured CPU domain constructor, and parallel spawn/dispatch
  work items inherit the domain affinity/default-priority values required by
  `SPECIFICATION.md` §20.2.4 and §20.4.5.
- This pass expanded async runtime source coverage so the accepted corpus now
  exercises manual `Async@Suspended.resume`, yield input propagation,
  `yield release from`, `wait` over `Spawned<T>` handles in inline and CPU
  execution domains, `wait` over `Tracked<T, E>` values returned by
  `Context.reactor.register`, streaming race resume continuation, `sync`,
  `all`, return and streaming `race`, and the
  map/filter/take/fold/chain/until/loop composition forms. The source remains
  the reference artifact; bootstrap repairs were made where the compiler
  accepted or rejected spec-valid async source incorrectly.
- This pass expanded async composition runtime source coverage with
  `Source/Reference/Async/CombinatorRuntimeForms.uv`. The file exercises
  runtime resume behavior for map completion/failure, filter skip/completion,
  take zero/resume-done/source-complete, fold failure, chain source failure,
  and chain suspended-continuation paths. The `take(1usize)` resume-done
  specimen surfaced a bootstrap runtime/lowering defect now recorded as
  `UVBOOT-0078`.
- This pass surfaced and corrected async bootstrap conformance defects in
  fresh async permission materialization, modal-state pattern narrowing,
  streaming-race frame propagation, resume argument materialization,
  alias-aware `yield`/`yield from` lowering, `Async@Suspended.resume`
  input type specialization, builtin `Reactor.register` method typing, and the
  runtime ABI declaration needed for `wait` over `Tracked<T, E>`. These repairs
  are recorded in `Audit/BootstrapNonCompliance.md` as `UVBOOT-0050` and
  `UVBOOT-0053`.
- This pass also corrected the remaining async manual-resume runtime path:
  `yield` resume continuation lowering now preserves the caller-provided input
  as addressable continuation storage, and builtin `Async@Suspended.resume`
  dispatch selects `runtime::async::resume` before generic state-method
  instantiation. `runAsyncStateMachineManualResumeReference` and
  `runAsyncCompositionStreamingRaceResumeReference` now execute through the
  catalog symbol runner and the full corpus executable exits 0.
- This pass expanded key-system accepted runtime source coverage for memory
  ordering and speculative execution. `runKeysMemoryOrderingReference` now
  exercises all five expression memory-order attributes and all three fence
  orders; `runKeysSpeculativeExecutionReference` now exercises a speculative
  write block whose body calls a const receiver method on keyed data before
  committing a covered write, a multi-path speculative write block, and a
  `C-unwind` catch boundary that converts a speculative bounds panic after key
  cleanup. The catch-boundary cleanup repair is recorded in
  `Audit/BootstrapNonCompliance.md` as `UVBOOT-0070`. The
  `SpeculativeReadMode`, `SpeculativeOutsideWrite`, `SpeculativeNestedKeyRule`,
  and `SpeculativeFence` rejected-source fixtures now exercise
  `rule.19.K-Spec-Write-Required`, `rule.19.K-Spec-Pure-Body`,
  `rule.19.K-Spec-No-Nested-Key`, and the fence-expression branch of
  `rule.19.K-Spec-No-Memory-Ordering`.
- This pass also expanded dynamic key-verification accepted source.
  `runKeysDynamicVerificationReference` now exercises a dynamic indexed read,
  a dynamic indexed write block over two runtime-indexed paths, prefix
  coarsening for a dynamic slice read, runtime synchronization for conflicting
  dynamic indexed writes in spawned parallel tasks under `[[dynamic]]`,
  speculative dynamic-index write behavior with a follow-up explicit read, and
  temporary shared viewing from a unique-origin value under `[[dynamic]]`. It
  also added rejected and diagnostic-source specimens for the dynamic-key
  static-required rule:
  `DynamicKeyStaticRequired` rejects a non-`[[dynamic]]` parallel dynamic-key
  write with `E-CON-0020`, while `DynamicKeyRuntimeSyncInfo` compiles the
  matching `[[dynamic]]` form and emits `I-CON-0011`.
- This pass also expanded key-acquisition accepted source.
  `runKeysAcquisitionBlocksReference` now exercises passing a `shared` value
  as a procedure argument without acquiring a key at the call site; the callee
  performs the explicit read acquisition and returns through the key block.
  It also exercises a local closure that captures a `shared` binding, acquires
  the key in the closure body, returns through that key block, and is invoked
  before the captured binding's scope exits. It also exercises an escaping
  closure with an explicit shared dependency-set alias, invokes that closure
  before the captured binding's scope exits, and relies on the closure body to
  acquire the shared key. It now also exercises inline
  key coarsening markers on field and index segments through
  `#container.#leaf.value` and `#leaves[#1usize].value` key paths, plus
  default read-mode key blocks, canonical sorting of an unsorted multi-path
  key list, and explicit key-block exits through `break`, `continue`, and
  direct `return`. `keyModeContextValue` exercises let-initializer reads,
  assignment right-hand-side reads, arithmetic operand reads, if-condition
  reads, a `~` const receiver call, a `const` parameter argument, assignment
  left-hand-side writes, and a `~%` shared receiver call while preserving the
  SPEC permission-qualified `shared i32` type of the field read. The duplicate
  `W-CON-0009`
  bootstrap diagnostic surfaced by this specimen is recorded in
  `Audit/BootstrapNonCompliance.md` as `UVBOOT-0051`. The type-alias expected
  closure check repair required for the escaping closure specimen is recorded
  as `UVBOOT-0059`.
- This pass expanded key conflict-detection accepted source.
  `runKeysConflictDetectionReference` now exercises disjoint multi-path reads,
  dynamic and ordered same-base multi-index reads under `[[dynamic]]`, prefix
  coverage under a root read key, expanded and compound read-then-write forms
  permitted by a covering write key, and nested field/index writes covered by
  a root write key. The read-then-write specimens intentionally emit the SPEC
  `W-CON-0006` and `W-CON-0004` diagnostics while still compiling and
  executing. The dynamic ordered same-base specimen surfaced the bootstrap
  source-array length repair recorded as `UVBOOT-0071`.
- This pass expanded Chapter 10 permission accepted and rejected source.
  `runPermissionsPermissionFormsReference`,
  `runPermissionsAliasExclusivityReference`,
  `runPermissionsActivityStatesReference`, and
  `runPermissionsAdmissibilityReference` now exercise default and explicit
  `const`, `shared`, and `unique` bindings, receiver shorthand forms,
  shared field writes under an explicit key, unique field mutation and
  reactivation after non-consuming uses, layout neutrality for
  permission-qualified types, and accepted admissibility pairs. The
  `ConstMutation`, `UniqueInactiveUse`, `SharedMutationWithoutKey`, and
  `ReceiverPermissionMismatch` rejected-source fixtures exercise
  `E-TYP-1601`, `E-TYP-1602`, `E-TYP-1604`, and `E-TYP-1605`; `E-TYP-1603`
  remains covered by `Expressions/CallArgNotPlace`. The const root-assignment
  versus aggregate-path mutation diagnostic ownership question and the direct
  shared-mutation versus implicit-key-acquisition question are recorded in
  `SpecClarificationsNeeded.md`, and the bootstrap repair is recorded as
  `UVBOOT-0066`.
- This pass expanded qualified name-resolution accepted source.
  `runNamesQualifiedResolutionReference` now exercises an imported module alias,
  qualified procedure calls, qualified record brace construction, qualified
  unit/tuple/record enum constructors and patterns, a first-class qualified
  procedure value, a qualified type alias target, and a qualified class path in
  a record implementation clause.
- This pass expanded module aggregation accepted source.
  `runModulesAggregationReference` now exercises multiple `.uv` files in the
  same module directory, a discovered child module directory, multiple files
  inside that child module, import binding of the child module alias, and
  qualified calls/types crossing the parent-child module boundary.
- This pass expanded authority region/frame accepted source.
  `runAuthorityRegionsAndFramesReference` now exercises region statements with
  explicit options and alias binding, implicit and explicit frame statements,
  nested region/frame target selection, implicit allocation in the active
  region, explicit `region ^ expr` allocation, frame-scoped allocation,
  explicit region allocation from inside a frame, and `Region::new_scoped`
  with allocation plus explicit cleanup. The reset/freeze/thaw lifecycle chain
  is recorded in the clarification ledger because the SPEC return signatures
  for those unique receiver transitions need a permission-preservation rule.
- This pass surfaced and corrected region/frame bootstrap lowering defects:
  implicit allocation, explicit allocation, explicit frame targets, and
  region-provenance binding storage now translate source region aliases to the
  stable region locals used by IR emission. The repair is recorded in
  `Audit/BootstrapNonCompliance.md` as `UVBOOT-0055`.
- This pass expanded backend lowering accepted source.
  `runLoweringBackendRequirementsReference` now exercises function values,
  record/tuple/array aggregate construction and mutation, enum unit/tuple/record
  payload construction and pattern lowering, loop break-value phi lowering,
  closure environment calls, slice views, string literal data, bytes views, and
  runtime byte-slice reads.
- This pass expanded cleanup and unwinding accepted source.
  `runLoweringCleanupDropUnwindingReference` now exercises successful defer
  cleanup, successful binding-drop cleanup, static drop deinitialization for a
  non-`Bitcopy` module-scope value, caught defer cleanup panic, and caught
  direct and child binding-drop panics. The caught panic specimens cross
  `C-unwind` catch boundaries, trigger runtime index panics under
  `[[dynamic]]`, and verify that later cleanup still runs after the first
  cleanup panic.
- This pass expanded compile-time quote/splice accepted source.
  `runComptimeQuoteSpliceEmissionReference` now exercises `quote pattern`,
  string-valued identifier splices in typed-pattern, identifier-expression, and
  procedure-parameter positions, an `Ast::Stmt` splice in statement position,
  expression splicing inside a quoted return statement, and emitted item
  lowering that calls the generated procedures at runtime. The statement-splice
  parser and quote-builder repairs are recorded in
  `Audit/BootstrapNonCompliance.md` as `UVBOOT-0052`.
- This pass expanded compile-time derive-target accepted source.
  `runComptimeDeriveTargetsReference` now exercises derive-target fixed input
  binding by branching on `introspect.type_name(target)` and
  `introspect.module_path(target)` before emitting runtime declarations. The
  accepted source now makes `RunDeriveTarget` target binding, contract-ordered
  execution, pending emitted items, and emitted declaration lowering observable
  through the generated functions' return values. The reflection type-name
  rendering repair is recorded in `Audit/BootstrapNonCompliance.md` as
  `UVBOOT-0077`.
- This pass expanded compile-time form accepted source.
  `runComptimeCompileTimeFormsReference` now exercises generic compile-time
  procedure declarations and calls, ordinary `if` return propagation inside a
  compile-time procedure body, multiline erased `comptime` statement blocks,
  `comptime if` selected-branch behavior with no-else, else-block, and
  else-if forms, annotated and inferred-element `comptime loop` unrolling,
  empty loop unrolling, and literalization of integer, boolean, tuple, array,
  record, unit enum, tuple-payload enum, record-payload enum, and modal-state
  compile-time values. The ordinary-control propagation repair is recorded in
  `Audit/BootstrapNonCompliance.md` as `UVBOOT-0054`, and the aggregate/enum
  literalization repairs are recorded as `UVBOOT-0075`.
- This pass expanded compile-time reflection accepted source.
  `runComptimeReflectionReference` now exercises all `SourceSpan` fields on the
  current compile-time diagnostic span and on reflected field, variant, and
  state metadata. It also exercises `FieldInfo.name`, `FieldInfo.type`,
  `FieldInfo.visibility`, `FieldInfo.index`, `VariantInfo.payload_kind`,
  `VariantInfo.payload_types`, `VariantInfo.field_names`,
  `StateInfo.field_names`, `StateInfo.method_names`, and
  `StateInfo.transition_names` through executable `comptime loop` source. The
  parser repair required for the SPEC-defined `FieldInfo.type` selector is
  recorded in `Audit/BootstrapNonCompliance.md` as `UVBOOT-0060`; the parser
  repair required for multiline `comptime { expression }` brace boundaries is
  recorded as `UVBOOT-0061`. The span validation helper accepts both same-line
  and multiline spans by ordering columns only when the start and end line are
  the same.
- This pass expanded operator-expression accepted source.
  `runExpressionsOperatorsReference` now exercises additive, multiplicative,
  remainder, integer and floating exponentiation, right-associative `**`
  parsing, bitwise operators, shifts with `u32` counts, integer and boolean
  unary `!`, signed and floating unary negation, primitive/string comparisons,
  logical short-circuit evaluation with non-evaluated panic operands, and all
  six range-expression forms through slice bounds. The final-artifact repair
  required for floating exponentiation's lowered `pow` dependency is recorded
  in `Audit/BootstrapNonCompliance.md` as `UVBOOT-0066`.
- This pass expanded cast/transmute accepted source.
  `runExpressionsCastsAndTransmutesReference` now exercises identity casts,
  signed-to-signed and signed-to-unsigned integer casts, unsigned-to-signed and
  unsigned-to-wide integer casts, signedness-preserving int-to-float casts,
  float-to-float casts, truncating positive and negative float-to-int casts,
  bool-to-int and int-to-bool casts, `char`/`u32` casts, unsafe transmute value
  reinterpretation, and transmute operand control propagation. The initial
  source shape was corrected to satisfy SPEC `ExplicitReturn`, which requires a
  final `ReturnStmt` in a non-unit procedure body.
- Latest verification:
  `Fixtures/DiagnosticSource/SourceText/LeadingBOMWarning` exited 0 and
  emitted `W-SRC-0101`;
  `Fixtures/DiagnosticSource/SourceText/DecimalLeadingZeroWarning` exited 0
  and emitted `W-SRC-0301`;
  `Fixtures/RejectedSource/SourceText/LeadingBOMEmbeddedBOM` exited 1 and
  emitted both `W-SRC-0101` and `E-SRC-0103`;
  `Fixtures/RejectedSource/SourceText/ProhibitedControlCharacter` exited 1 and
  emitted `E-SRC-0104`;
  `Fixtures/RejectedSource/SourceText/InvalidUTF8` exited 1 and emitted
  `E-SRC-0101`;
  `Fixtures/RejectedSource/Parsing/EnumCommaSeparator` exited 1 and emitted
  `E-SRC-0520`;
  `Fixtures/RejectedSource/Expressions/TupleIndexOutOfBounds` exited 1 and
  emitted `E-TYP-1801`;
  `Fixtures/RejectedSource/Names/PrivateAccess` exited 1 and emitted
  `E-MOD-1207`;
  `Fixtures/RejectedSource/Modules/StaticMissingTypeAnnotation` exited 1 and
  emitted `E-TYP-1505`;
  `Fixtures/RejectedSource/Attributes/UnknownAttribute` exited 1 and emitted
  `E-MOD-2451`;
  `Fixtures/RejectedSource/Modules/MissingImport` exited 1 and emitted
  `E-MOD-1202`;
  `Fixtures/RejectedSource/Modules/UsingListDuplicate` exited 1 and emitted
  `E-MOD-1206`;
  `Fixtures/RejectedSource/Attributes/PackedLayoutOnEnum` exited 1 and emitted
  `E-MOD-2454`;
  `Fixtures/RejectedSource/Attributes/ReservedVendorNamespace` exited 1 and
  emitted `E-CNF-0402`;
  `Fixtures/RejectedSource/Modules/ReservedModuleKeyword` exited 1 and emitted
  `E-MOD-1105`;
  `Fixtures/RejectedSource/DataTypes/TypeAliasCycle` exited 1 and emitted
  `E-TYP-1506`;
  `Fixtures/RejectedSource/Expressions/UnionDirectAccess` exited 1 and emitted
  `E-TYP-2202`;
  `Fixtures/RejectedSource/Procedures/ContractDynamicAttribute` exited 1 and
  emitted `E-CON-0410`;
  `Fixtures/RejectedSource/Attributes/TestMissingPostcondition` exited 1 and
  emitted `E-TST-0106`;
  `Fixtures/RejectedSource/Patterns/IfCaseModalNonExhaustive` exited 1 and
  emitted `E-TYP-2060`;
  `Fixtures/RejectedSource/Expressions/AllocImplicitNoRegion` exited 1 and
  emitted `E-MEM-3021`;
  `Fixtures/OutputDiagnostics/Projects/ManifestParseError` exited 1 and
  emitted `E-PRJ-0102`;
  `Build/bin/uv.exe definitely_unknown_command` exited 1 and emitted
  `E-CLI-0001`;
  `Fixtures/DiagnosticSource/Keys/SpeculativeLargeStructWarning` exited 0 and
  emitted `W-CON-0020`;
  `Fixtures/DiagnosticSource/Keys/SpeculativeExpensiveBodyWarning` exited 0 and
  emitted `W-CON-0021`;
  `Fixtures/DiagnosticSource/Attributes/InlineAlwaysRecursive` exited 0 and
  emitted `W-MOD-2452`;
  `python3 .agents/scripts/generate_hello_catalog.py` completed after the
  generator skipped unchanged generated files and retried transient filesystem
  write failures;
  the generated catalog now contains 6,045 rows with 5,517 accepted-source,
  45 accepted-project, 438 rejected-source, 26 diagnostic-source,
  11 artifact-behavior, and 8 reference-model primary rows;
  rejected-source fixture metadata now indexes 426 expected diagnostic entries
  across 405 physical `Expected.uv` artifacts;
  diagnostic-source fixture metadata now indexes 28 compiling warning, info, or
  expected-absence specimens;
  output-diagnostic fixture metadata now indexes 8 diagnostic artifact entries;
  `python3 Tools/ExtractObligationLedger.py --check` passed with 6,045
  obligations;
  a source audit counted 154 `.uv` files and 187 public `run...Reference`
  procedures under `Source/Reference`, 0 public `run...Reference` procedures
  implemented only as `return true`, and 0 missing direct calls from
  `Source/Api.uv`;
  `Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress on --max-errors 20`
  exited 0 in 171.28s with fourteen warnings plus ten info diagnostics;
  `Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20`
  exited 0 in 162.49s with the same diagnostic set, rebuilt
  `HelloUltraviolet::Reference::Attributes`, reused 58 object files, and
  emitted `HelloUltraviolet/build/bin/HelloUltraviolet.exe`;
  `HelloUltraviolet.exe` exited 0 with 0-byte stdout/stderr;
  `HelloUltraviolet.exe --audit` exited 0 with 0-byte stdout/stderr.
- Previous verification:
  `Fixtures/RejectedSource/Names/PrivateAccess` and
  `Fixtures/RejectedSource/Names/InternalAccess` each exited 1 with
  `E-MOD-1207`, exercising the private and cross-assembly internal access
  rejection paths;
  `python3 Tools/ExtractObligationLedger.py --check` passed with 6,045
  obligations;
  a source audit counted 176 public `run...Reference` procedures under
  `Source/Reference` and 0 missing direct calls from `Source/Api.uv`;
  a focused data-type grammar catalog audit confirmed
  `grammar.RecordSyntax`, `grammar.EnumSyntax`, `grammar.UnionTypeSyntax`, and
  `grammar.TypeAliasSyntax` map to the records, enums, unions, and type-alias
  reference runners respectively;
  `Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20`
  exited 0 with ten warnings plus three info diagnostics;
  `Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20`
  exited 0 in 146.91s with the same diagnostic set, reusing 58 object files
  and rebuilding 1 object file for
  `HelloUltraviolet::Audit::Catalog::ConcreteDataTypes`;
  `HelloUltraviolet.exe` exited 0 with 0-byte stdout/stderr;
  `HelloUltraviolet.exe --audit` exited 0 with 0-byte stdout/stderr;
  targeted `git -c filter.lfs.process= -c filter.lfs.clean=cat -c filter.lfs.smudge=cat -c filter.lfs.required=false diff --check`
  over the regenerated concrete data-type catalog files and
  `Audit/CompletionAudit.md` exited 0;
  a targeted trailing-whitespace scan over the ignored catalog generator, the
  regenerated concrete data-type catalog files, and `Audit/CompletionAudit.md`
  exited 0;
  `GenericInfiniteMonomorphization`
  built with exit code 1
  and emitted `E-TYP-2307`; `GenericInstantiationDepthLimit` built with exit
  code 1 and emitted `E-TYP-2308`; the accepted-project fixtures `StaticLibrary`,
  `ExecutableMain`, `PtrNullReturn`, `ExpressionSemantics`,
  `VerificationFactPrecondition`, `HostedExportLibrary`, and
  `CrossAssemblyImplementation` built with exit code 0 in 312ms, 322ms, 312ms,
  314ms, 321ms, 316ms, and 311ms respectively, with
  `CrossAssemblyImplementation` built through
  `--assembly CrossAssemblyImplementation`; the `ExecutableMain` fixture binary
  exited 0 with 0-byte stdout/stderr; `objdump -p` on
  `HostedExportLibrary.dll` showed the hosted lifecycle exports
  `__ultraviolet_host_abi_version`, `__ultraviolet_host_session_create`, and
  `__ultraviolet_host_session_destroy`, plus the five hosted thunk entrypoints;
  targeted `git -c filter.lfs.process= -c filter.lfs.clean=cat -c filter.lfs.smudge=cat -c filter.lfs.required=false diff --check`
  over the generator, `HelloUltraviolet`, and the binary-chain bootstrap
  compiler files exited 0. The ignored catalog generator
  `.agents/scripts/generate_hello_catalog.py` and the touched project/compiler
  files passed a trailing-whitespace scan after regenerating the catalog.

## Verified Deliverables

- Project folder exists at `HelloUltraviolet/`.
- Project-local obligation ledger exists at
  `HelloUltraviolet/Audit/UltravioletObligations.csv`.
- The generated ledger check passes:
  `python3 Tools/ExtractObligationLedger.py --check`.
- `HelloUltraviolet/Source/Main.uv` calls the executable reference corpus through
  `runReferenceCorpus(context)`.
- All 154 files under `HelloUltraviolet/Source/Reference` contain executable
  reference bodies; the count of public `run...Reference` procedures whose
  body is only `return true` is `0`.
- The generated catalog contains 6,045 primary obligation rows in
  `HelloUltraviolet/Source/Audit/Catalog/**/*.uv`. Each row records the
  obligation id, internal spec line, module path, symbol, source path, and
  exercise kind in Ultraviolet source.
- `CoverageCheck.uv` verifies both the generated obligation total and the
  generated primary-reference validation total against `EXPECTED_OBLIGATION_COUNT`.
- `CatalogCsvMembership.uv` compares the 6,045 project-local CSV obligation
  keys with the 6,045 generated catalog primary keys in compiled Ultraviolet
  source.
- `CatalogPrimaryReferences.uv` checks that the 6,045 generated primary
  obligation references are unique by sorting `(id, internal_spec_line)` keys
  and verifying strict adjacent ordering in compiled Ultraviolet source.
- `CatalogSourcePaths.uv` checks the 595 unique source targets referenced by
  generated catalog rows, and `HelloUltraviolet.exe` validates their runtime
  existence through `catalogSourcePathsExist(context)`.
- `CatalogSymbols.uv` imports and executes the 602 compiled reference
  and fixture-validator symbol target tuples named by catalog rows, and
  `HelloUltraviolet.exe` validates them through
  `catalogCompiledSymbolsExecute()`. Failed compiled-symbol execution prints
  the failing symbol name before the aggregate reference failure.
- `Source/Reference/Modules/Aggregation.uv`,
  `Source/Reference/Modules/AggregationHelpers.uv`, and
  `Source/Reference/Modules/AggregationSubmodule/*.uv` exercise compilation
  unit aggregation across same-directory files, module discovery for a child
  directory, and import-qualified access across that module boundary.
- `Source/Reference/Async/SuspensionForms.uv` exercises `yield`, `yield from`,
  `wait` over spawned work in both ready and pending paths, and `wait` over a
  reactor-registered tracked future.
- `Source/Reference/Async/CompositionForms.uv` exercises async composition
  sync, all, return race, streaming race, map, filter, take, fold, chain,
  until, and loop iteration through dedicated reference runners. Streaming
  race now includes a suspended-yield resume specimen that verifies the
  previously yielded arm continues when the caller resumes it.
- `Source/Reference/Async/CombinatorRuntimeForms.uv` exercises concrete
  map/filter/take/fold/chain resume semantics, including completion,
  failure, skipped yields, source completion, and suspended continuation
  propagation.
- `Source/Reference/Async/StateMachine.uv` exercises direct async completion,
  suspension, manual `Async@Suspended.resume(input)`, and state-pattern
  observation of `@Completed`, `@Suspended`, and `@Failed`.
- `Source/Reference/Async/AsyncKeyIntegration.uv` exercises
  `yield release from` in an async key-acquisition integration path.
- `Source/Reference/Names/QualifiedResolution.uv` exercises imported module
  aliases, qualified value resolution, qualified type paths, qualified record
  and enum construction, qualified enum patterns, first-class qualified
  procedure values, and qualified class paths in implementation clauses.
- `Source/Reference/Keys/KeyPaths.uv` exercises root key paths, nested field
  and index key paths, and a record field declared with a `#` key boundary.
  The generated catalog maps the key-boundary parse helpers and field-boundary
  semantic requirement to this runner.
- `Source/Reference/Keys/AcquisitionBlocks.uv` exercises explicit read and
  write blocks, omitted-mode default-read blocks, ordered multi-path
  acquisition, canonical sorting from an unsorted multi-path list, a `shared`
  argument call whose callee performs the explicit key acquisition, a local
  closure that captures `shared` data and acquires the key in its body, and
  return, break, and continue control flow through key blocks. It also
  exercises inline key coarsening markers on field and index key-path
  segments, plus concrete read/write context classification through
  `keyModeContextValue`.
- `Source/Reference/Keys/ConflictDetection.uv` exercises disjoint key paths,
  dynamic and ordered same-base multi-index reads under `[[dynamic]]`, prefix
  coverage, covering write permission for expanded and compound
  read-then-write assignment forms, and nested field/index writes covered by a
  root write key.
- `Source/Reference/Keys/NestedRelease.uv` exercises release-read nested inside
  an outer write key, release-write nested inside an outer read key, and
  direct plus forwarded shared-parameter callee access summaries covered by an
  outer key.
- `Source/Reference/Keys/MemoryOrdering.uv` exercises expression-level
  `[[relaxed]]`, `[[acquire]]`, `[[release]]`, `[[acqrel]]`, and `[[seqcst]]`
  memory-order attributes on shared reads, plus `fence(acquire)`,
  `fence(release)`, and `fence(seqcst)` in runtime expression contexts.
- `Source/Reference/Keys/SpeculativeExecution.uv` exercises speculative write
  fallback/commit behavior for a direct shared write and for a const receiver
  method call on keyed data followed by a covered write, multi-path
  speculative writes, and an imported `C-unwind` catch boundary for a
  speculative bounds panic.
- `Fixtures/RejectedSource/Keys/SpeculativeReadMode`,
  `SpeculativeOutsideWrite`, `SpeculativeNestedKeyRule`, and
  `SpeculativeFence` reject with `E-CON-0095`, `E-CON-0091`, `E-CON-0090`,
  and `E-CON-0096`, exercising the SPEC's separate speculative key-block
  rejection rules as rejected source.
- `Fixtures/RejectedSource/Names/PrivateAccess` and
  `Fixtures/RejectedSource/Names/InternalAccess` reject with `E-MOD-1207`,
  exercising `Access-Err`, `Access-Internal-Err`, and
  `diagnostics.NameResolutionAndReservedNames` through concrete visibility
  diagnostic specimens.
- `Fixtures/DiagnosticSource/SourceText/LeadingBOMWarning` builds with exit
  code 0 and emits `W-SRC-0101`, exercising `Span-BOM-Warn` through a source
  file whose first bytes are `EF BB BF`.
- `Fixtures/DiagnosticSource/SourceText/DecimalLeadingZeroWarning` builds with
  exit code 0 and emits `W-SRC-0301`, exercising
  `Warn-DecimalLeadingZero` through the decimal literal `001`.
- `Fixtures/RejectedSource/SourceText/LeadingBOMEmbeddedBOM` rejects with
  `W-SRC-0101` and `E-SRC-0103`, exercising
  `req.LeadingBOMWarningPersistence` and `Span-BOM-Embedded` through a source
  file whose first bytes are `EF BB BF EF BB BF`.
- `Fixtures/RejectedSource/SourceText/ProhibitedControlCharacter` rejects with
  `E-SRC-0104`, exercising `Span-Prohibited` through a raw U+0001 byte outside
  a literal or comment span.
- `Fixtures/RejectedSource/SourceText/InvalidUTF8` rejects with
  `E-SRC-0101`, exercising `NoSpan-Decode` and
  `diagnostics.SourceLexicalDiagnostics` through an invalid `FF` byte in source
  text.
- `Fixtures/RejectedSource/Parsing/EnumCommaSeparator` rejects with
  `E-SRC-0520`, exercising `req.EnumTopLevelCommaSeparatorRejected`,
  `Parse-Syntax-Err`, and `diagnostics.ParsingDiagnostics` through a generic
  parser syntax error.
- `Fixtures/RejectedSource/Expressions/TupleIndexOutOfBounds` rejects with
  `E-TYP-1801`, exercising `TupleIndex-OOB` and `diagnostics.Tuples` through
  an out-of-bounds tuple projection.
- `Fixtures/RejectedSource/Modules/StaticMissingTypeAnnotation` rejects with
  `E-TYP-1505`, exercising `WF-StaticDecl-MissingType` and
  `diagnostics.StaticDeclarations` through a module-scope `let` declaration
  without a type annotation.
- `Fixtures/RejectedSource/Attributes/UnknownAttribute` rejects with
  `E-MOD-2451`, exercising `AttrList-Unknown` and
  `diagnostics.AttributeDiagnostics` through an unknown attribute name on a
  procedure declaration.
- `Fixtures/RejectedSource/Modules/MissingImport` rejects with `E-MOD-1202`,
  exercising `Import-Path-Err`, `Bind-Import-Err`,
  `Resolve-Import-Err`, and `diagnostics.ImportDeclarations` through a
  top-level import declaration whose module path cannot resolve.
- `Fixtures/RejectedSource/Modules/UsingListDuplicate` rejects with
  `E-MOD-1206`, exercising `Using-List-Dup`, `Bind-Using-Err`, and
  `diagnostics.UsingDeclarations` through a top-level using declaration whose
  list names the same item twice.
- `Fixtures/RejectedSource/Attributes/PackedLayoutOnEnum` rejects with
  `E-MOD-2454`, exercising `diagnostics.LayoutAttributeDiagnostics` through
  `[[layout(packed)]]` applied to an enum declaration.
- `Fixtures/RejectedSource/Attributes/ReservedVendorNamespace` rejects with
  `E-CNF-0402`, exercising `diagnostics.VendorAttributeDiagnostics` through
  the reserved `ultraviolet::...` vendor-attribute namespace.
- `Fixtures/RejectedSource/Modules/ReservedModuleKeyword` rejects with
  `E-MOD-1105`, exercising `WF-Module-Path-Reserved` and
  `diagnostics.ModuleAggregation` through a source-root child directory named
  with a reserved language keyword. `UVBOOT-0073` records the bootstrap repair
  that removed the extra parser-style `E-CNF-0401` diagnostic from the
  directory-derived module path validation path.
- `Fixtures/RejectedSource/DataTypes/TypeAliasCycle` rejects with
  `E-TYP-1506`, exercising `def.AliasCycle`,
  `TypeAlias-Recursive-Err`, `req.TypeAliasDiagnosticOwnership`, and
  `diagnostics.CoreTypeDiagnostics` through mutually recursive source-level
  type aliases. `UVBOOT-0074` records the bootstrap repair that maps the
  type-alias recursive rule id to the SPEC diagnostic code, and
  `SpecClarificationsNeeded.md` records the public SPEC spelling typo for the
  same rule.
- `Fixtures/RejectedSource/Expressions/UnionDirectAccess` rejects with
  `E-TYP-2202`, exercising `diagnostics.DataTypesSupplement` through direct
  field access on a union value without prior pattern matching.
- `Fixtures/RejectedSource/Procedures/ContractDynamicAttribute` rejects with
  `E-CON-0410`, exercising `diagnostics.DiagnosticsMetadataAttributes` through
  `[[dynamic]]` applied directly to a contract predicate expression.
- `Fixtures/RejectedSource/Attributes/TestMissingPostcondition` rejects with
  `E-TST-0106`, exercising `diagnostics.TestAttributes` through a
  source-native `[[test]]` procedure that has explicit visibility, explicit
  return type, and a body but lacks the required postcondition.
- `Fixtures/RejectedSource/Patterns/IfCaseModalNonExhaustive` rejects with
  `E-TYP-2060`, exercising `diagnostics.ModalPointerSupplement` through
  non-exhaustive `if ... is` analysis on a general modal value.
- `Fixtures/RejectedSource/Expressions/AllocImplicitNoRegion` rejects with
  `E-MEM-3021`, exercising `diagnostics.RuntimeStateAndMemoryDiagnostics`
  through region allocation `^` outside an active region scope.
- `Fixtures/DiagnosticSource/Attributes/InlineAlwaysRecursive` builds with
  exit code 0 and emits `W-MOD-2452`, exercising
  `diagnostics.OptimizationAttributeDiagnostics` through a recursive
  `[[inline(always)]]` procedure whose inlining cannot be honored.
- `Fixtures/OutputDiagnostics/Projects/ManifestParseError` rejects with
  `E-PRJ-0102`, exercising `Parse-Manifest-Err`, `Step-Parse-Err`,
  `LoadProject-Err`, and `diagnostics.ProjectDiagnostics` through a malformed
  `Ultraviolet.toml` artifact. Its conformance log records diagnostic emission
  and ill-formed-project rejection for the project-load path.
- `Fixtures/OutputDiagnostics/CommandLine/UnknownCommand` records
  `Build/bin/uv.exe definitely_unknown_command`; that invocation exits with
  code 1 and renders `E-CLI-0001 (error): unknown command`, exercising
  `diagnostics.CommandLineDiagnostics` through the command-line diagnostic
  surface.
- `Source/Fixtures/RejectedSource` compiles metadata for 426 rejected-source
  expected diagnostic entries across 405 physical `Expected.uv` artifacts, and
  `HelloUltraviolet.exe` validates that fixture index
  through `rejectedSourceFixturesAreIndexed`.
- The rejected-source fixture projects under
  `HelloUltraviolet/Fixtures/RejectedSource` fail with their expected SPEC
  diagnostic code or static-rule diagnostic when built with the compiler mode
  that owns the diagnostic. Check-time specimens use
  `Cursive.exe build ... --check`; the monomorphization lowering specimens use
  normal `Cursive.exe build ...` and emit `E-TYP-2307` or `E-TYP-2308`.
- Each rejected-source fixture includes an `Expected.uv` metadata artifact, and
  the compiled metadata records both the invalid source path and expected
  diagnostic metadata path.
- `HelloUltraviolet.exe` verifies runtime existence of each rejected fixture
  manifest, invalid source file, and `Expected.uv` artifact through
  `rejectedSourceFixtureArtifactsExist(context)`.
- `ExpectedFiles.uv` reads the 403 current rejected-source `Expected.uv`
  artifacts and `HelloUltraviolet.exe` validates exact metadata content through
  one named check per physical fixture artifact.
- `Source/Fixtures/DiagnosticSource` compiles metadata for 27 diagnostic-source
  fixture specimens whose source is expected to compile while emitting SPEC
  warnings or informational diagnostics, or while proving a SPEC diagnostic is
  absent, and `HelloUltraviolet.exe` validates the index, artifact paths, and
  exact `Expected.uv` metadata through the diagnostic-source fixture checks.
- `Source/Fixtures/OutputDiagnostics` compiles metadata for the
  `LlvmToolResolveOwnership`, `EmitLLVMRenderFailure`, `ManifestParseError`,
  and `UnknownCommand` output-diagnostic fixtures, and `HelloUltraviolet.exe`
  validates the index, source artifacts, exact `Expected.uv` metadata, captured
  command output, and conformance-log ownership markers through the
  output-diagnostic fixture checks.
- `Fixtures/OutputDiagnostics/Lowering/LlvmToolResolveOwnership` builds with
  exit code 1 and emits `E-OUT-0403` when bitcode emission requests
  `llvm-as` from an unavailable toolchain. The conformance log records
  `ResolveTool-Err-IR`, `Out-IR-Err`, and `Output-Pipeline-Err`, exercising
  `requirement.24.LLVMToolAcceptanceAndResolveOwnership` without assigning
  the failure to object emission.
- `Fixtures/OutputDiagnostics/Lowering/EmitLLVMRenderFailure` builds with
  exit code 1 and emits `E-OUT-0403` after resolving a fixture-local
  `llvm-as` tool that reports LLVM 21.1.8 and then rejects rendered LLVM text.
  The conformance log records `ResolveTool-Ok`, `AssembleIR-Err`,
  `EmitLLVM-Err`, `Out-IR-Err`, and `Output-Pipeline-Err`, exercising
  `rule.24.EmitLLVM-Err` without assigning the failure to object emission or
  LLVM-tool resolution.
- The `Source/Reference/Lowering` files now contain executable source
  specimens for expression, statement, block, and place lowering; concrete
  `sizeof`/`alignof` layout queries; module-scope initialization; defer cleanup
  and user `drop` lowering for non-`Bitcopy` records; static drop
  deinitialization; caught defer and binding drop panic cleanup continuation,
  including direct and child drop failure; runtime string/bytes interface
  calls; exported symbol mangling; and backend arithmetic/branching.
  `BackendRequirements.uv` now also exercises function values, aggregate
  memory operations, enum payloads, loop phi values, closure calls, slice
  views, string literal data, and bytes view reads.
  `HelloUltraviolet.exe` exercises those flows through the seven
  `runLowering...Reference` functions.
- The `Source/Reference/ModalTypes` files now contain executable source
  specimens for modal declarations, state fields, state-specific methods with
  receiver permissions, consuming transitions, explicit `widen` from concrete
  modal state to general modal type, string and bytes state/layout/builtin
  behavior, safe and raw pointer state/layout/deref behavior, first-class
  function types including `move` parameters, and closure type syntax including
  grouped-union parameters and shared-dependency annotations.
  `HelloUltraviolet.exe` exercises those flows through the ten
  `runModalTypes...Reference` functions.
- The `Source/Reference/Polymorphism` files now contain executable source
  specimens for generic default type parameters, generic aliases, multi-predicate
  generic clauses, body-local reuse of a generic value justified by
  `Bitcopy(T)`, generic record literal construction through expected generic
  record types, generic tuple-return procedures, class-bound generic method
  dispatch, default associated types, default class methods, dynamic casts to
  superclass objects, opaque class values, and receiver permissions for
  const/shared/unique class methods. This surfaced and repaired bootstrap
  lowering defects in generic class-bound method-symbol selection and generated
  procedure propagation from branch-local contexts.
  `HelloUltraviolet.exe` exercises those flows through the Polymorphism
  reference runner.
- `Fixtures/DiagnosticSource/Expressions/ValidTransmuteTarget` builds with exit
  code 0 and emits `W-SAFE-0100` for a valid unsafe transmute whose target type
  is known to admit invalid bit patterns.
- The 26 key-system rejected-source fixtures under
  `Fixtures/RejectedSource/Keys` reject with their expected `E-CON-*`
  diagnostics, and the 13 key-system diagnostic-source fixtures under
  `Fixtures/DiagnosticSource/Keys` compile with their expected `W-CON-*` or
  `I-CON-*` diagnostics or prove expected diagnostic absence.
  `FineGrainedLoopKeyWarning` now performs a repeated keyed field read inside
  the loop, `NestedReleaseInterleavingWarning` reads through the nested release
  block, and `DynamicKeyRuntimeInfo` performs a dynamic indexed read through
  the keyed path while still emitting the expected warning/info diagnostics.
  `DynamicKeyStaticRequired` covers the required `E-CON-0020` rejection for
  non-statically-safe key access outside `[[dynamic]]`, and
  `DynamicKeyRuntimeSyncInfo` covers the required `I-CON-0011` runtime-sync
  diagnostic under `[[dynamic]]`.
- `Fixtures/DiagnosticSource/Parallelism/DispatchDynamicKeyWarning` compiles
  with exit code 0 and emits `W-CON-0140` for dispatch partition specs with
  non-static index expressions.
- `Fixtures/DiagnosticSource/Parallelism/CancellationNoAdditionalDiagnostics`
  and
  `Fixtures/DiagnosticSource/Parallelism/PanicHandlingNoAdditionalDiagnostics`
  compile with exit code 0 and record that cancellation and panic propagation
  themselves introduce no additional named structured-parallelism diagnostics.
- The GPU-safe structured-parallelism fixtures reject with `E-TYP-2640` and
  `E-TYP-2642`, exercising direct prohibited GPU-safe types, record-field
  GPU-safe component rejection, and unbounded generic GPU-safe compound types.
  This required a bootstrap fix in `type_predicates.cpp` so `GpuSafeType`
  analysis follows generic applications lowered as `TypeApply`.
- The GPU pointer structured-parallelism fixtures reject with `E-TYP-2641` and
  `E-CON-0150`, exercising `GpuPtr` address-space mismatch and host pointer
  dereference inside GPU code. This required bootstrap fixes in
  `resolve_types.cpp`, `type_wf.cpp`, `type_infer.cpp`, and `expr/deref.cpp`
  so `GpuPtr<T, S>` address-space markers survive resolution and lowering and
  the dereference owner emits the SPEC diagnostic code.
- The async key-integration rejected-source fixtures under
  `Fixtures/RejectedSource/Keys` reject with `E-CON-0133`, `E-CON-0213`, and
  `E-CON-0224`, exercising wait, yield, yield-from, and shared-capturing
  closure obligations while keys are held. This required a bootstrap closure
  typing fix in `closure_expr.cpp` so an explicitly async-returning closure
  body is checked against its own declared return context.
- `Fixtures/DiagnosticSource/Keys/StaleAfterYieldReleaseWarning` emits
  `W-CON-0011` for stale binding use after `yield release`, while
  `Fixtures/DiagnosticSource/Keys/StaleOkSuppressesReleaseWarning` uses the
  same source shape with `[[stale_ok]]` and verifies `W-CON-0011` is absent.
- `Fixtures/DiagnosticSource/Keys/SpeculativeLargeStructWarning` compiles with
  exit code 0 and emits `W-CON-0020` for a speculative block over a large
  aggregate. `Fixtures/DiagnosticSource/Keys/SpeculativeExpensiveBodyWarning`
  compiles with exit code 0 and emits `W-CON-0021` for a speculative block body
  that calls a const receiver method before writing under the speculative key.
- `Fixtures/RejectedSource/Expressions/YieldOutsideAsync` rejects with
  `E-CON-0210`, exercising `rule.21.Yield-NotAsync-Err` by using `yield` in a
  non-async-returning procedure.
- The async suspension and sync fixture group under
  `Fixtures/RejectedSource/Expressions` now rejects with `E-CON-0132`,
  `E-CON-0211`, `E-CON-0220`, `E-CON-0221`, `E-CON-0222`, `E-CON-0225`,
  `E-CON-0212`, `E-CON-0223`, `E-CON-0250`, `E-CON-0251`, and `E-CON-0252`,
  exercising wait-handle, yield output, yield-from compatibility, and sync
  operand/context obligations from Chapter 21.
- The async race/all fixture group under `Fixtures/RejectedSource/Expressions`
  rejects with `E-CON-0260`, `E-CON-0261`, `E-CON-0262`, `E-CON-0263`,
  `E-CON-0270`, and `E-CON-0271`, exercising race arity, race handler
  consistency, race operand compatibility, race handler result compatibility,
  and all-expression operand requirements from Chapter 21.
- The async type well-formedness fixture group under
  `Fixtures/RejectedSource/Expressions` rejects with `E-CON-0201`, exercising
  async argument count, ill-formed async argument, and unapplied async type path
  obligations from Chapter 21. This required bootstrap diagnostic routing fixes
  for `WF-Async-ArgCount-Err`, `WF-Async-Arg-WF-Err`, and
  `WF-Async-Path-Err`.
- The async state-machine diagnostic fixtures now exercise `W-CON-0201`,
  `E-CON-0280`, `E-CON-0281`, `E-CON-0230`, and `W-CON-0011` through
  `Fixtures/DiagnosticSource/Async` and
  `Fixtures/RejectedSource/Expressions/AsyncCaptureOutlivesFrame`,
  `AsyncOperationEscapesRegion`, and `AsyncDiagnosticsSupplement`. This
  required bootstrap provenance fixes in `regions.cpp` for large-capture
  warning sizing, async capture/escape diagnostic ids, diagnostic emission
  routing through `ProvBindCheck`, and region/frame provenance separation. The
  compiler rebuild also required a surgical `parse_modules.cpp` update so
  dispatch-option reserved-binder traversal follows `chunk_expr` and
  `workgroup_expr`.
- `Fixtures/RejectedSource/Polymorphism/ImplementationMissingRequiredField`
  rejects with `E-TYP-2402`, and
  `Fixtures/RejectedSource/Polymorphism/ImplementationFieldTypeMismatch`
  rejects with `E-TYP-2404`; these exercise `rule.14.Impl-Field-Missing` and
  `rule.14.Impl-Field-Type-Err`.
- `Fixtures/RejectedSource/Polymorphism/DuplicateClassImplementationRule` and
  `Fixtures/RejectedSource/Polymorphism/DuplicateClassImplementationRequirement`
  reject with `E-TYP-2506`; these exercise `rule.14.Impl-Coherence-Err` and
  `req.14.DuplicateClassImplementationForbidden`.
- `Fixtures/RejectedSource/Polymorphism/NonModalImplementsModalClass` rejects
  with `E-TYP-2401`, and
  `Fixtures/RejectedSource/Polymorphism/ImplementationMissingAbstractMethod`
  rejects with `E-TYP-2503`; these exercise
  `req.14.ModalClassImplementationRequiresModalType` and
  `diag.14.Implementations`.
- `Fixtures/RejectedSource/Polymorphism/DuplicateAssociatedTypeName` rejects
  with `E-TYP-2504`, and
  `Fixtures/RejectedSource/Polymorphism/AssociatedTypeMissingBinding` rejects
  with `E-TYP-2503`; these exercise `diag.14.AssociatedTypes` and
  `req.14.AssociatedTypeAbstractAndDefaultBinding`.
- `Fixtures/RejectedSource/Polymorphism/AssociatedTypeUnboundMember` rejects
  with `E-TYP-2503`, exercising
  `req.14.ImplementationAssociatedTypeBoundForm`. This required a bootstrap
  fix in `record_decl.cpp` because the previous checker accepted `type Item`
  in an implementing record and silently used the class default.
- `Fixtures/RejectedSource/Polymorphism/GenericTypeApplyArgCount` rejects with
  `E-TYP-2303`, exercising `rule.14.WF-Apply-ArgCount-Err`. This required a
  bootstrap fix in `type_wf.cpp` because `TypeApply` well-formedness checked
  argument well-formedness but did not validate user-defined generic arity.
- `Fixtures/RejectedSource/Polymorphism/GenericPathMissingArguments` rejects
  with `E-TYP-2303`, exercising `rule.14.WF-Path-Generic-Err`. This uses the
  same bootstrap owner because a generic `TypePath` without required arguments
  must be rejected during type well-formedness.
- `Fixtures/RejectedSource/Polymorphism/GenericCallArgCount` rejects with
  `E-TYP-2303`, exercising `rule.14.Generic-Call-ArgCount-Err`. This required
  a bootstrap fix in `type_infer.cpp` because check-mode generic-call
  inference was previously ignoring explicit type arguments.
- `Fixtures/RejectedSource/Polymorphism/GenericClassBoundNonClass` rejects with
  `E-TYP-2305`, exercising `rule.14.WF-ClassPath-Err`.
- `Fixtures/RejectedSource/Polymorphism/DuplicateTypeParameterName` rejects
  with `E-TYP-2304`, exercising `rule.14.WF-Generic-Param`.
- `Fixtures/RejectedSource/Polymorphism/DuplicateClassProcedureName` rejects
  with `E-TYP-2500`, exercising class well-formedness for distinct class
  method names.
- `Fixtures/RejectedSource/Polymorphism/ClassMemberNameConflict` rejects with
  `E-TYP-2505`, exercising `diag.Classes` for cross-category class member name
  conflicts.
- `Fixtures/RejectedSource/Polymorphism/SuperclassCycle` rejects with
  `E-TYP-2508`, exercising `rule.14.Superclass-Cycle`.
- `Fixtures/RejectedSource/Polymorphism/OverrideAbstractMethod` rejects with
  `E-TYP-2501`, and
  `Fixtures/RejectedSource/Polymorphism/OverrideMissingOnDefault` rejects with
  `E-TYP-2502`; these exercise `rule.14.Override-Abstract-Err` and
  `rule.14.Override-Missing-Err`. This required a bootstrap diagnostic-code fix
  in `record_decl.cpp`.
- `Fixtures/RejectedSource/Polymorphism/ImplementationAbstractMethodSignatureMismatch`
  and
  `Fixtures/RejectedSource/Polymorphism/ImplementationDefaultMethodSignatureMismatch`
  reject with `E-TYP-2503`; these exercise `rule.14.Impl-Sig-Err` and
  `rule.14.Impl-Sig-Err-Concrete`.
- `Fixtures/RejectedSource/Polymorphism/OverrideNoConcrete` rejects with
  `E-UNS-0105`, exercising `rule.14.Override-NoConcrete`. This required a
  bootstrap diagnostic-code fix in `record_decl.cpp`.
- `Fixtures/RejectedSource/Polymorphism/HeapAllocatorRawCallRequiresUnsafe`,
  `Fixtures/RejectedSource/Polymorphism/HeapAllocRawUnsafe`,
  `Fixtures/RejectedSource/Polymorphism/HeapDeallocRawUnsafe`, and
  `Fixtures/RejectedSource/Polymorphism/CapabilityClassesDiagnostics` reject
  with `E-MEM-3030`; these exercise the HeapAllocator raw-call unsafe
  requirement, both formal raw-call unsafe rules, and the capability-class
  diagnostics aggregate.
- The refinement rejected-source specimens under
  `Fixtures/RejectedSource/Polymorphism` reject with `E-TYP-1953`,
  `E-TYP-1954`, `E-TYP-1955`, and `E-TYP-1956`; these exercise refinement
  well-formedness, static introduction, static-default checking, refinement
  diagnostic ownership, and the refinement diagnostic table surface. The
  inline-parameter `self` diagnostic required a bootstrap fix in
  `signature.cpp`.
- `Fixtures/RejectedSource/Procedures/NoMatchingOverload` rejects with
  `E-SEM-3031`, exercising the no-match branch of
  `req.15.FreeCallOverloadResolutionAlgorithm`. This required bootstrap fixes
  in `collect_toplevel.cpp` and `expr/call.cpp` so same-name free procedures
  form overload sets before ordinary call typing.
- `Fixtures/RejectedSource/Procedures/DuplicateErasedOverloadSignature`
  rejects with `E-SEM-3032`, exercising
  `req.15.DuplicateErasedOverloadSignaturesForbidden`. This required a
  bootstrap fix in `typecheck.cpp` so procedure declaration typing compares
  same-name overload parameter signatures after generic-parameter erasure.
- `Fixtures/RejectedSource/Procedures/OverloadingDiagnosticsTable` rejects
  with `E-SEM-3032`, exercising `diag-table.15.Overloading` through the
  duplicate-signature condition defined in the Chapter 15 overloading
  diagnostic table.
- `Fixtures/RejectedSource/Procedures/OverloadResolutionBeforeCallTyping`
  rejects with `E-SEM-3031`, exercising
  `req.15.FreeProcedureOverloadResolutionBeforeCallTyping` by requiring the
  overloaded callee set to reject at overload resolution rather than falling
  through to ordinary call typing.
- `Fixtures/RejectedSource/Procedures/VerificationFactNoRuntimeRepresentation`
  rejects with `E-MOD-1301`, exercising
  `req.15.VerificationFactsNoRuntimeRepresentation`. The specimen attempts to
  expose the spec's internal `VerificationFact` form as a returned surface
  type; the bootstrap correctly keeps verification facts out of the value/type
  namespace because §15.8 introduces no surface syntax for facts and gives them
  no runtime representation.
- The dynamic class object rejected-source specimens under
  `Fixtures/RejectedSource/Polymorphism` reject with `E-TYP-2509`,
  `E-TYP-2541`, `E-TYP-2542`, and `E-SEM-2536`; these exercise undefined
  dynamic class types, non-dispatchable casts, method-level generic
  vtable-ineligibility, missing dynamic method lookup, and the dynamic class
  object diagnostic surface.
- The opaque type rejected-source specimens under
  `Fixtures/RejectedSource/Polymorphism` reject with `E-TYP-2509`,
  `E-TYP-2510`, and `E-TYP-2511`; these exercise undefined opaque class types,
  interface-only opaque projection, and opaque return implementation checking.
- `Fixtures/RejectedSource/Polymorphism/BitcopyDropConflict` rejects with
  `E-TYP-2621`, exercising `rule.14.BitcopyDrop-Conflict`.
- `Fixtures/RejectedSource/Polymorphism/BitcopyFieldNonBitcopy` rejects with
  `E-TYP-2622`, exercising `diag.14.FoundationalClasses`. This required a
  bootstrap fix in `record_decl.cpp` and `enum_decl.cpp` because the checker
  treated `:< Bitcopy` as requiring an explicit `:< Clone` before evaluating
  the intrinsic `BitcopyType` predicate.
- `Fixtures/RejectedSource/Polymorphism/CapabilityClassNameReserved` and
  `Fixtures/RejectedSource/Polymorphism/FoundationalClassNameReserved` reject
  with `E-MOD-1304`; these exercise
  `req.14.CapabilityClassNamesReserved` and
  `req.14.FoundationalClassesSyntaxAndReservedNames`. This required bootstrap
  fixes in `scopes.cpp` and `resolve_module.cpp` for protected capability names
  and registered reserved-name diagnostic routing.
- The bootstrap checker now reports the SPEC diagnostic codes for these paths:
  undefined dynamic/opaque class bounds resolve to `E-TYP-2509`, opaque
  interface misses resolve to `E-TYP-2510`, and method-level generic class
  methods make dynamic class casts reject with `E-TYP-2542`.
- `Fixtures/RejectedSource/Comptime/UserDiagnosticError` rejects with
  `E-CTE-0070`, exercising `diagnostics.error` user diagnostic emission during
  Phase 2 and recording both the builtin rule and user-diagnostic emission
  obligation in its expected metadata.
- `Fixtures/RejectedSource/Comptime/QuoteOutsideComptime` rejects with
  `E-CTE-0221`, exercising the requirement that quote forms are valid only in
  compile-time contexts and the quote/splice diagnostic reference surface.
- `Fixtures/RejectedSource/Comptime/InvalidQuotedContent` rejects with
  `E-CTE-0220`, exercising quoted-content syntactic validity in the resolved
  quote category.
- `Fixtures/RejectedSource/Comptime/ComptimeIfConditionType` rejects with
  `E-CTE-0081`, exercising the compile-time-form diagnostics reference through
  a non-bool `comptime if` condition.
- `Fixtures/RejectedSource/Comptime/RuntimeCallsComptimeProcedure` rejects
  with `E-CTE-0034`, exercising the rule that compile-time procedures are only
  callable from compile-time contexts. This required a bootstrap fix in
  `rewrite.cpp` so Phase 2 reports the specific diagnostic before compile-time
  procedure declarations are removed from the expanded module.
- `Fixtures/RejectedSource/Comptime/IntrospectFieldsNonRecord` rejects with
  `E-CTE-0050`, exercising the rule that `introspect.fields` is valid only
  for reflectable record types and the reflection diagnostics reference. This
  required a bootstrap fix in `reflect.cpp` so reflected enums, modals, and
  records that fail the queried member-kind check emit the specific
  `E-CTE-0050`, `E-CTE-0051`, or `E-CTE-0052` diagnostic instead of the
  incomplete/non-reflectable declaration diagnostic.
- `Fixtures/RejectedSource/Comptime/ComptimePointerParameter` rejects with
  `E-CTE-0011`, exercising compile-time type availability rejection for a
  pointer-bearing compile-time procedure parameter. This required a bootstrap
  fix in `typecheck.cpp` and `compiler_main.cpp` so compile-time procedure
  signatures are checked before Phase 2 erases compile-time procedure
  declarations.
- `Fixtures/RejectedSource/Comptime/ComptimeProhibitedWait` rejects with
  `E-CTE-0020`, exercising the compile-time prohibited-construct rule with
  `wait` inside a `comptime` statement. This required a bootstrap fix in
  `rewrite.cpp` so Phase 2 expansion checks prohibited runtime constructs
  before evaluating and erasing compile-time statements.
- `Fixtures/RejectedSource/Comptime/EmitterEmitNonItem` rejects with
  `E-CTE-0251`, exercising `TypeEmitter.emit` argument-kind requirements,
  emission well-formedness, and compile-time capability diagnostics with a
  `quote type` AST passed to `emitter~>emit`.
- `Fixtures/RejectedSource/Comptime/DeriveOnProcedure` rejects with
  `E-CTE-0311`, exercising the requirement that `[[derive(... )]]` is valid
  only on record, enum, and modal declarations. This required a bootstrap fix
  in `rewrite.cpp` so Phase 2 reports the derive target-kind diagnostic before
  non-type derive attributes are stripped from runtime items.
- `Fixtures/RejectedSource/Comptime/UnknownDeriveTarget` rejects with
  `E-CTE-0310`, exercising derive target name resolution for a
  `[[derive(... )]]` attribute whose target name has no visible derive target
  declaration.
- `Fixtures/RejectedSource/Comptime/DeriveMissingRequiredClass` rejects with
  `E-CTE-0330`, exercising derive `requires` validation against the annotated
  declaration's explicit class implementation list.
- `Fixtures/RejectedSource/Comptime/DeriveMissingEmittedClass` rejects with
  `E-CTE-0331`, exercising derive `emits` validation against the annotated
  declaration's explicit class implementation list.
- `Fixtures/RejectedSource/Comptime/DeriveTargetUserError` rejects with
  `E-CTE-0070`, exercising derive target failure semantics when a derive target
  body signals a compile-time user error.
- `Fixtures/RejectedSource/Comptime/DeriveTargetProhibitedWait` rejects with
  `E-CTE-0320`, exercising derive target body restrictions for prohibited
  runtime constructs inside a derive target body. This required a bootstrap fix
  in `derive.cpp` so derive execution reports the specific derive-body
  diagnostic instead of a silent Phase 2 failure.
- `Fixtures/DiagnosticSource/Comptime/UserDiagnosticWarning` compiles with
  exit code 0 and emits `W-CTE-0071`, and
  `Fixtures/DiagnosticSource/Comptime/UserDiagnosticNote` compiles with exit
  code 0 while emitting the uncoded user note diagnostic. These exercise the
  `diagnostics.warning` and `diagnostics.note` Phase 2 builtin forms.
- `Fixtures/DiagnosticSource/Comptime/ProjectFilesInvalidPath` compiles with
  exit code 0 and exercises `[[files]]` ProjectFiles path restrictions by
  requiring an escaping path to return `IoError::InvalidPath`; the specimen
  emits `E-CTE-0070` only if the invalid-path outcome is not observed.
- `Fixtures/RejectedSource/Comptime/SpliceTypeMismatch` rejects with
  `E-CTE-0230`, exercising quote splice context and type compatibility by
  attempting to splice a compile-time integer expression into a type quote.
- `Fixtures/RejectedSource/Comptime/SpliceIdentifierStructuralName` rejects
  with `E-CTE-0220`, exercising the structural identifier restriction by
  attempting to use an identifier splice as an item declaration name inside a
  quote.
- `Source/Reference/Comptime/QuoteSpliceEmission.uv` exercises accepted
  quote/splice forms through emitted runtime procedures, including
  `quote pattern`, expression splices, statement splices, identifier-position
  string splices, and item emission order.
- `Source/Reference/Comptime/DeriveTargets.uv` exercises accepted derive
  targets whose emitted declarations depend on `target` metadata supplied by
  `BindDeriveTargetInputs`, plus `requires`/`emits` contract ordering and
  emitted declaration lowering.
- The bootstrap class-implementation checker now evaluates
  `def.ImplementationOrphanRule` for record, enum, and modal `implements`
  clauses in `CheckOrphanRule`, using the first module-path segment as the
  assembly identity and routing violations to `E-TYP-2507`.
- `Source/Fixtures/AcceptedProjects` compiles metadata for 45 accepted-project
  obligation expectations across 7 buildable project fixtures, and
  `HelloUltraviolet.exe` validates both the index and artifact paths through
  accepted-project fixture checks. The generated catalog maps these
  expectations to `acceptedProjectObligationEntryMatches` rows for
  `Archive-Ok`, `Link-Ok`, `rule.16.Chk-Null-Ptr`,
  `rule.21.T-Loop-Iter-Async`, `rule.15.Fact-Dominate`,
  `req.14.ImplementationOrphanRequirement`, and 39 hosted-export obligations
  from §23.3.
- `Fixtures/AcceptedProjects/StaticLibrary` builds as a valid static library
  project, `Fixtures/AcceptedProjects/ExecutableMain` builds and runs as a
  valid executable project, `Fixtures/AcceptedProjects/PtrNullReturn` builds as
  a valid library project for checked `Ptr::null()` return typing, and
  `Fixtures/AcceptedProjects/ExpressionSemantics` builds as a valid library
  project for rule-level expression typing, and
  `Fixtures/AcceptedProjects/VerificationFactPrecondition` builds as a valid
  library project for branch-generated verification facts discharging
  preconditions.
- `Fixtures/AcceptedProjects/HostedExportLibrary` builds as a valid shared
  hosted library project. Its source exercises a projected `Context` bundle
  instead of raw `Context`, hosted-export classification and parsing through
  ordinary public procedure declarations, C and C-unwind ABI strings, visible
  primitive and aggregate FFI-safe parameters, a projected `$FileSystem`
  capability root, catch-unwind zeroable return behavior, and Win64
  direct/indirect aggregate carrier lowering. The emitted DLL export table
  contains the three hosted lifecycle exports and five hosted thunk
  entrypoints selected by `[[mangle(... )]]`.
- `Fixtures/AcceptedProjects/CrossAssemblyImplementation` builds as a valid
  multi-assembly library project. The selected library assembly imports a
  dependency assembly, implements the dependency's public classes with a local
  record, enum, and modal declaration, and returns the implemented record field
  value after constructing the enum and modal values. This exercises the valid
  source surface for `def.ImplementationOrphanRule` and
  `req.14.ImplementationOrphanRequirement` across all three implementer forms:
  the implementing type is local to the current assembly while the class is
  imported from another assembly.
  Building this fixture required a bootstrap correction in
  `collect_toplevel.cpp` and `item/import_decl.cpp` so import validation uses
  the semantic AST module set being analyzed rather than the selected
  assembly's initial module list.
- `Source/Fixtures/ArtifactProjects` compiles metadata for 7 artifact project
  fixture specimens, and `HelloUltraviolet.exe` validates the index,
  source/manifest paths, and emitted-IR erasure checks through artifact-project
  fixture checks.
- `Fixtures/ArtifactProjects/StaticLibraryArchive` builds a `.lib` archive and
  `.obj`, `Fixtures/ArtifactProjects/EmitLlLibrary` builds a `.ll`, `.lib`, and
  `.obj`, `Fixtures/ArtifactProjects/EmitBcLibrary` builds a `.bc`, `.lib`, and
  `.obj` with the pinned LLVM 21.1.8 `llvm-as` tool directory, and
  `Fixtures/ArtifactProjects/FlowProofRuntimeErasure` builds a `.ll` and `.obj`
  whose emitted IR contains no verification-fact runtime materialization.
  `Fixtures/ArtifactProjects/ExecutableOutput` builds and runs an `.exe`,
  `.map`, and `.obj` artifact.
- `Fixtures/ArtifactProjects/ReducedEmptyDispatchPanic` builds an executable
  artifact and exits with runtime panic code `2862`, exercising
  `P-SEM-2862` for reduced dispatch over an empty iteration space and the
  structured-parallelism runtime-panic ownership obligation.
- `Fixtures/ArtifactProjects/AArch64DependencyObject` builds a dependency
  assembly with `toolchain.target_profile = "aarch64-aapcs64"` and emits
  `build/obj/AArch64DependencyObject.o`, exercising SPEC target-profile object
  emission without final archiving. This required linking and initializing the
  bootstrap compiler's AArch64 LLVM target components; the previous
  target-machine lookup failure for `aarch64-unknown-linux-gnu` is repaired.
- The object emission path no longer records `EmitObj-Err` when LLVM module
  materialization fails before object emission begins. That path is owned by
  `LowerIR-Err`; `EmitObj-Err` remains reserved for failures of
  `LLVMEmitObj_21` after a module exists. `rule.24.LowerIR-Err` and
  `rule.24.EmitObj-Err` are cataloged as reference-model rows while their
  deterministic fixture class is pending SPEC clarification. The bootstrap
  owner path is `LLVMBootstrap/cursive/src/06_driver/pipeline.cpp`: it records
  `LowerIR-Err` when module materialization fails before an LLVM module is
  available, and records `EmitObj-Err` only after a module exists for verifier,
  target-machine, or object-emission pass setup failures.
- The project check gate passes:
  `Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64
  --build-progress on --max-errors 20`, most recently run with exit code 0 in
  215.47s and the expected 12 warnings plus 9 info diagnostics after expanding
  the module-level import source specimen.
- The project build gate passes:
  `Cursive.exe build HelloUltraviolet --target-profile x86_64-win64
  --build-progress on --max-errors 20`, most recently run with exit code 0 in
  181.54s with 58 objects reused, 1 object rebuilt, and the expected 12
  warnings plus 9 info diagnostics after expanding the module-level import
  source specimen.
- The executable gate passes:
  `HelloUltraviolet/build/bin/HelloUltraviolet.exe`, most recently run with
  exit code 0 and no output.
- The audit-argument executable gate passes:
  `HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit`, most recently
  run with exit code 0 and no output.
- The focused bootstrap regression gate passes:
  `cursive_codegen_abi_sret_test.exe`.
- Whitespace validation passes:
  `git -c filter.lfs.process= -c filter.lfs.clean=cat -c filter.lfs.smudge=cat -c filter.lfs.required=false diff --check`.

## Completion Blockers

- Rejected-source, diagnostic-source, and output-diagnostic fixtures are
  partially populated. The current fixture set covers 426 rejected-source
  diagnostic entries, 28 compiling diagnostic-source warning/info/absence
  specimens, and 4 output-diagnostic artifact cases. Of the 382
  `oracle.expected-diagnostics`
  obligations, the generated catalog now maps 350 to rejected-source fixture
  files, 23 to diagnostic-source fixture files, 4 to artifact-behavior fixture
  files, 1 to an accepted-project fixture, and 4 to the compiled clarification
  ledger. The 5
  sourceability-limited rows in the accepted-project or clarification-ledger
  buckets still do not have concrete rejecting-source diagnostic fixtures.
  Their ownership counts are: lowering 2, abstraction/polymorphism 2,
  statements 1.
  `HelloUltraviolet/Audit/SpecClarificationsNeeded.md` records the current
  sourceability questions for those five rows, and
  `Source/Audit/SpecClarifications.uv` now makes those classifications
  executable in the reference corpus.
- `Fixtures/BootstrapNonCompliance/Procedures/FreeProcedureOverloadResolution`
  now passes semantic checking, standalone build, and runtime execution after
  the bootstrap repair in `collect_toplevel.cpp`, `expr/call.cpp`,
  `mangle.cpp`, `lower_module.cpp`, `lower_proc.cpp`, and
  `lower/expr/call.cpp`. The `NoMatchingOverload` rejected-source fixture
  covers the no-match branch of the free-call overload selection algorithm,
  while `Source/Reference/Procedures/Overloading.uv` now exercises arity-based
  selection, parameter-type selection, and same-name method lookup through
  executable return values.
- `rule.18.BlockInfo-Res-Err` is cataloged as a reference-model row pending
  source-construct clarification. The row rejects block result prefixes whose
  `Res` set has no common type, but the Chapter 18 rules inspected here produce
  `Res = []` for let, var, assignment, expression, defer, region, frame, return,
  continue, and unsafe statements; route `break` values through `Brk` instead
  of `Res`; and expand `CtStmt` away before it contributes a runtime statement.
  A corrected probe using
  two block-expression statement prefixes with incompatible tail types was
  rejected as `E-SEM-3161` at the enclosing `return`, not as
  `BlockInfo-Res-Err`. The bootstrap owner path is `TypeBlockInfo` in
  `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/stmt_common.cpp`; it
  checks `ResType(stmts_typed.flow.results)` and can emit `BlockInfo-Res-Err`
  when the vector is heterogeneous, but the inspected source-facing producers
  append only nested statement-block results of type `!` into `flow.results`.
  That can exercise `BlockInfo-Res` without creating a heterogeneous `Res` set
  for `BlockInfo-Res-Err`. This is tracked in
  `HelloUltraviolet/Audit/SpecClarificationsNeeded.md`.
- `rule.14.Impl-Orphan-Err` is cataloged as a reference-model row, while
  `req.14.ImplementationOrphanRequirement` is cataloged as an accepted-project
  row. Both remain sourceability-limited as concrete expected-diagnostic
  obligations. The accepted
  `CrossAssemblyImplementation` fixture now exercises the valid cross-assembly
  implementation surface for the same orphan requirement with a local record,
  enum, and modal declaration implementing imported classes.
  `SPECIFICATION.md:13116` states that class implementation occurs at the
  defining record, enum, or modal declaration and that standalone extension
  implementation blocks are not part of the language. Under that surface, an
  ordinary source implementation always has the implementing declaration in the
  current assembly. The bootstrap checker now owns the rule and emits
  `E-TYP-2507` if an implementation relation is ever presented outside that
  owner context; there is no current SPEC-valid source spelling for the
  rejecting case. This is tracked in
  `HelloUltraviolet/Audit/SpecClarificationsNeeded.md`.
- `Fixtures/RejectedSource/Patterns/TuplePatternArity` rejects with
  `E-TYP-1803`, exercising `rule.17.Pat-Tuple-R-Arity-Err` through a tuple
  pattern whose element count differs from the matched tuple type arity. This
  required a bootstrap fix in `pattern_common.cpp` and `stmt_common.cpp`
  because both tuple-pattern typing paths previously returned the uncoded
  static-rule label instead of the concrete tuple diagnostic code defined by
  `SPECIFICATION.md:9127`.
- `Fixtures/RejectedSource/Statements/FrameNoActiveRegion`,
  `Fixtures/RejectedSource/Statements/FrameTargetNotActive`, and
  `Fixtures/RejectedSource/Statements/FrameDiagnostic` reject with
  `E-MEM-1207` and `E-MEM-1208`, exercising
  `rule.18.Frame-NoActiveRegion-Err`, `rule.18.Frame-Target-NotActive-Err`,
  and `diag.18.Frame`. `UVBOOT-0084` records the bootstrap repair that maps
  these static rule labels to the Chapter 6 diagnostic table.
- Structured-parallelism rejected-source fixtures now cover spawn/dispatch
  context requirements, invalid spawn option types, execution-domain typing and
  constructor parameters, cancellation option typing, GPU workgroup shape
  checks, reducer associativity, nested GPU parallelism, GPU intrinsic scope,
  GPU barrier scope, GPU key-block rejection, barrier divergence, and GPU
  capture rejection for shared and non-`GpuSafe` captures. They also cover
  unique capture rejection, non-selected outer unique moves, escaping closures
  that contain `spawn`, dispatch key inference failure, dispatch dependency
  conflicts, heap-provenance capture rejection in GPU context, and the dynamic
  dispatch-key warning. They also cover direct GPU-safe type rejection,
  GPU-safe record-field rejection through a bitcopy `Ptr<i32>@Valid` field,
  generic GPU-safe compound rejection for an unbounded type parameter, `GpuPtr`
  address-space mismatch, and host pointer dereference inside GPU code. Additional bootstrap
  diagnostic conformance repairs were required: nested GPU parallel now emits
  `E-CON-0152` for `T-GPU-Nested-Err`, `gpu_barrier()` outside a GPU context
  now emits `E-CON-0156` for `Barrier-Outside-Err`, key blocks in GPU contexts
  now emit `E-CON-0155` for `KeyBlock-GPU-Err`, and divergent GPU barriers now
  emit `E-CON-0158` for `Barrier-Divergence-Err`. Heap allocation provenance
  from `HeapAllocator.alloc_raw` is now preserved by `prov_expr.cpp`, allowing
  GPU capture checks to emit `E-CON-0150` for `GpuCapture-HeapProv-Err`. GPU-safe
  nominal type analysis now handles `TypeApply` in `type_predicates.cpp`, so
  generic GPU-safe records and enums follow the same diagnostics as non-generic
  nominal types. `GpuPtr<T, S>` resolution now preserves `Global`, `Shared`,
  and `Private` address-space markers, type well-formedness accepts those
  markers only in the `GpuPtr` address-space position, address-space mismatch
  checking follows lowered `TypeApply`, and GPU host-pointer dereference emits
  `E-CON-0150`.
- Structured-parallelism source fixtures now also exercise the two
  no-additional-diagnostics clauses for cancellation and panic propagation, and
  the artifact-project runtime specimen exercises reduced empty dispatch
  through `P-SEM-2862`. This required bootstrap repairs in structured-parallel
  cancel-token typing, parallel cleanup lowering, runtime panic propagation,
  reduced empty dispatch panic reporting, lazy worker startup, and Windows
  executable manifest emission.
- `Fixtures/RejectedSource/Statements/UnsafeRequiredOperationOwnershipDiagnostic`
  rejects with `E-MEM-3030`, exercising
  `diag.18.UnsafeRequiredOperationOwnership` through the construct owner named
  by `SPECIFICATION.md:20217`. `UVBOOT-0084` records the bootstrap repair that
  maps the sourceable `Transmute-Unsafe-Err` static rule to the Chapter 6
  unsafe-operation diagnostic and records the SPEC clarification request for a
  direct `Code(Transmute-Unsafe-Err)` assignment.
- Accepted-project fixtures are partially populated with 7 buildable projects.
  Artifact-project fixtures are partially populated with 7 buildable projects.
- Some high-level areas currently use executable reference models rather than
  source specimens for every concrete syntax form, notably async and compile-time
  forms. Structured-parallelism source coverage now includes configured CPU
  domains and multiline trailing-comma option lists for parallel blocks, spawn,
  and dispatch. The FFI runtime path now exercises
  primitive, record, generic record, enum, generic enum, fixed-array,
  raw-pointer, function-pointer, foreign-contract, boundary-unwind, capability
  isolation, and drop-bearing by-value call forms.
  Backend source specimens now exercise function values, aggregate memory
  operations, enum payload lowering, loop phi values, closure calls, slice
  views, string literal data, and bytes view reads.
  Compile-time derive-target source specimens now exercise target metadata
  binding, target-name rendering, module-path rendering, derives ordered by
  `emits`/`requires`, pending emission transfer, and runtime execution of the
  emitted declarations.
  Compile-time reflection source specimens now exercise category, implements,
  type-name, module-path, full `SourceSpan` field access, and the complete
  `FieldInfo`, `VariantInfo`, and `StateInfo` metadata field surfaces, including
  a multiline `comptime { expression }` diagnostic-span specimen.
  Async source specimens now include `yield`, `yield from`, `wait` over
  `Spawned`, and `wait` over `Tracked` with a `Reactor.register` source path.
  Module aggregation source specimens now include same-module multi-file
  compilation units, child-module discovery, child-module multi-file
  aggregation, import-qualified parent-to-child access, and reserved keyword
  module path rejection through a concrete child directory specimen.
  Source-text literal specimens now include grouped decimal and based integer
  lexemes, all integer suffixes, contextual and explicit float suffixes,
  exponent forms, simple, byte, and Unicode escape sequences, char escapes,
  boolean, null, and unit literals, and tuple-projection lexical
  disambiguation. Source-text operator and punctuator specimens now include
  arithmetic compound assignments, all six range/slice token shapes, the
  `:=` binding operator, and the `~>` method-call suffix.
  Name-resolution source specimens now include imported module aliases,
  qualified procedure calls, qualified record and enum constructors, qualified
  enum patterns, first-class qualified procedure values, and qualified class
  paths in implementation clauses.
  Procedure contract source specimens now include no-clause, pre-only,
  post-only, pre/post, `@result`, stable `@entry`, pure literal, identifier,
  field, tuple access, index, unary, binary, cast, if, if-is, if-case, block,
  tuple, array, record, builtin call, ordinary procedure call, const receiver
  method call, compile-time procedure call, and flow-fact precondition
  discharge forms. Behavioral-subtyping source now uses actual class procedure
  contracts, an implementation that weakens the class precondition and
  strengthens the class postcondition, direct concrete calls, and an opaque
  class method call whose wrapper precondition discharges the class
  precondition. `UVBOOT-0056` records the bootstrap repairs needed for
  full contract predicate parsing, pure block predicates, stable `@entry`
  proof, and non-runtime compile-time procedure metadata for
  `Pure-Comptime`. `UVBOOT-0058` records the later bootstrap repairs needed for
  braced no-else `if is` predicate parsing, pure-predicate proof equality,
  aggregate and control-flow precondition substitution, and literal
  compile-time boolean predicate proof. `UVBOOT-0076` records the bootstrap
  repair needed for a no-argument compile-time boolean predicate whose body
  computes `true` through immutable local bindings and constant expressions.
  Statement source specimens now exercise block statement sequences, empty and
  unit blocks, tail expressions, return-tail control, annotated and inferred
  `let`/`var` bindings, tuple and record binding patterns, `:=` immovable
  bindings, explicit unique moves, unique-to-const binding suspension, fresh
  and rebound region handles, direct and compound assignment, field/tuple/index
  assignment places, assignment control propagation, local `using` alias chains
  and alias identity, deferred cleanup order and defer-safe loop control,
  anonymous and optioned regions with and without aliases, region option/body
  control propagation, implicit and explicit frames, frame target selection,
  frame allocation, non-unit unsafe statements, unsafe statement control
  propagation, an owned unsafe-required `transmute` operation, return
  value/unit forms, return-expression control propagation, break value/unit
  forms, break-expression control propagation, continue, and drop-bearing
  temporaries on return and break.
  Primitive source specimens now exercise every primitive type lexeme through
  typed local bindings and value operations: signed and unsigned integer
  widths, pointer-sized integer widths, `f16`/`f32`/`f64` literals and ordered
  comparisons, default integer and float literal typing, `bool`, `char`, `()`,
  `sizeof`, and `alignof` for primitive, unit, and never types. `UVBOOT-0065`
  records the bootstrap repairs needed for Windows half-float runtime helpers
  and binary16 ordered comparison lowering.
  Union source specimens now exercise member introduction from primitive,
  unit, and pointer values, reordered-union and width-subtyping coercions,
  exhaustive type-case analysis, success and error propagation through `?`,
  tagged union layout, niche union layout for `() | Ptr<T>@Valid`, and runtime
  value extraction from both empty and payload niche cases. The concrete
  `Union-DirectAccess-Err` row and `diagnostics.DataTypesSupplement` table row
  now point at the existing rejected source specimen, and `WF-Union-TooFew` is
  indexed through the clarification ledger.
  Tuple and array source specimens now exercise nested tuple scanning,
  multiline tuple construction, tuple place projection assignment, tuple and
  array construction control propagation, tuple access control propagation,
  tuple layout, empty and repeated array literal segments, array place
  indexing, dynamic array indexing and dynamic array place assignment, and
  array layout. `TupleIndex-OOB`, `TupleAccess-NotTuple`, and
  `Index-Array-NonUsize` now point at rejected-source specimens, while
  `TupleIndex-NonConst` is indexed through the clarification ledger.
  Type-alias source specimens now include a mutually recursive alias cycle
  rejected-source fixture that exercises alias-cycle detection and the Chapter
  8 core type diagnostics table.
  Record source specimens now exercise field declarations with and without
  default initializers, associated type members, record methods, shorthand and
  out-of-order field initializers, default record construction, record literal
  control propagation, empty-record layout, and non-empty record layout.
  `WF-Record-DupField`, `FieldVisOk-Err`, and `diagnostics.Records` now point
  at rejected-source specimens. The bootstrap now emits the SPEC-assigned
  `E-TYP-1901` diagnostic code for `WF-Record-DupField`.
  Enum source specimens now exercise unit, tuple, and record variants;
  terminator-separated top-level variants; explicit discriminants; implicit
  discriminant sequencing; generic enum payloads; qualified enum literal
  resolution; enum pattern matching; enum literal control propagation; and
  tagged enum layout. `req.EnumTopLevelCommaSeparatorRejected`,
  `Enum-Empty-Err`, `Enum-Variant-Dup`, `Enum-Disc-Invalid`,
  `Enum-Disc-Dup`, `Enum-Lit-Unknown`, `Enum-Lit-Tuple-Arity-Err`,
  `Enum-Lit-Record-MissingField`, and `diagnostics.Enums` now point at
  rejected-source specimens. `Enum-Disc-NotInt` and `Enum-Disc-Negative` are
  indexed through the clarification ledger. The bootstrap now emits the
  SPEC-assigned diagnostics for duplicate enum variants, unknown enum
  variants, tuple payload arity mismatches, and record payload missing fields.
  Key-system source specimens now exercise accepted key paths, key acquisition,
  field key boundaries, inline coarsening markers, expression-level and
  key-block-default memory-order attributes, ordering override behavior, fence
  expressions, dynamic indexed reads and writes, duplicate equivalent slice
  keys through const, variable, and folded indices, constant-offset dynamic
  slice keys, shared safe-pointer
  dereference key boundaries, accepted shared dynamic class object method
  calls, shared-argument call-site behavior, key-block control-flow return,
  local and escaping closure shared captures, key-block `break` and `continue`
  control-flow exits, explicit read/write context classification for
  let-initializer, assignment, operand, condition, argument, and receiver
  positions, covering-write read-then-write behavior, root-key nested write
  coverage, release-block stale-observation marking, dynamic prefix coarsening,
  dynamic runtime synchronization for conflicting spawned writes, dynamic
  unique-origin shared viewing, speculative const receiver calls, field and
  indexed-field speculative writes, dynamic indexed speculative writes,
  multi-path speculative writes, speculative panic cleanup through a catch
  boundary, selected rejected-source obligations, and selected
  diagnostic-source obligations. `UVBOOT-0080` records the bootstrap repair
  needed for shared
  slice bounds checks to prefer the runtime fat-pointer length over stale
  static length metadata. `UVBOOT-0081` records the bootstrap repair that
  prevents local same-body disjointness from proving dynamic indexed key
  safety in parallel contexts outside `[[dynamic]]`.
  Expression closure and pipeline source specimens now exercise noncapturing
  function-typed closure literals, capturing closure-typed literals, typed and
  inferred closure parameters, typed and inferred returns, trailing-comma
  parameter lists, empty parameter lists, block and expression bodies, `move`
  parameters, explicit move capture, parenthesized union parameter annotations,
  closure-call callee and argument control propagation, function pipelines,
  captured-closure pipelines, chained pipelines, and expected-type closure
  literals on a pipeline right-hand side. `UVBOOT-0067` records the bootstrap
  repairs needed for callable-alias normalization in closure/pipeline typing
  and lowering, ordinary closure-call dispatch through callable aliases, and
  indirect closure code-pointer ABI typing. The source also corrects an earlier
  reference mistake by assigning noncapturing closure literals to function
  aliases, matching `SPECIFICATION.md` §16.9.4.
  Expression call source specimens now exercise no-argument calls, positional
  calls, multiline trailing-comma argument lists, argument evaluation and
  control-flow propagation, `move` arguments, generic calls, qualified generic
  calls with predicate clauses, function values, closure values, record
  construction calls, method calls, and region argument control propagation.
  `UVBOOT-0068` records the bootstrap repair needed for generic procedure
  predicate bounds to satisfy `Bitcopy(T)` inside the checked procedure body.
  `UVBOOT-0069` records the later backend repair needed for indirect closure
  calls whose actual callable type carries `move` parameter modes while stale
  concrete closure-code signature recovery supplies borrowed modes.
  Names qualified-resolution source specimens now exercise imported module
  aliases, full qualified procedure paths, qualified record literals,
  qualified enum constructors and patterns, qualified generic type
  applications, qualified modal-state literals, qualified procedure values,
  qualified function-typed parameters, qualified record destructuring
  patterns, local `using` aliases, full class paths, and dynamic class paths.
  `UVBOOT-0079` records the bootstrap repairs needed for qualified
  modal-state literal parsing, qualified record-pattern disambiguation through
  module aliases, and resolver diagnostic context propagation. The
  cross-module modal-state observation uses a public state method because
  §13.2.4 restricts direct modal payload field visibility to the declaring
  module. The record-pattern versus enum-record-payload token-shape ambiguity
  is indexed through the clarification ledger.
  Pattern source specimens now exercise literal, wildcard, identifier, typed,
  tuple, singleton tuple, record, empty-record, enum unit/tuple/record, modal
  state, range, union, else-complement, and exhaustive no-else forms through
  executable source. `UVBOOT-0083` records the bootstrap repair needed for
  empty tuple pattern matching against the unit value; the empty record
  specimen uses default construction because ordinary empty record literals are
  excluded by the SPEC's non-empty record-literal field-list rule.

## Current Status

The reference corpus is executable, the generated catalog now materializes every
ledger row in Ultraviolet source, and all verification gates listed above pass.
The current full-corpus run exercises the accepted-source, rejected-source,
diagnostic-source, artifact-behavior, accepted-project, and reference-model
catalog surfaces without injecting pass-through reference runners.

Latest full-corpus verification:

- Visual Studio bootstrap build wrapper, `Config=Release`: exit 0 after
  rebuilding the modified lowering and emission owners.
- `LLVMBootstrap/cursive/build/Release/Cursive.exe build .agents/tmp/AsyncSuspendedProbe --target-profile x86_64-win64 --build-progress off --incremental off --max-errors 20`:
  exit 0.
- `.agents/tmp/AsyncSuspendedProbe/build/bin/AsyncSuspendedProbe.exe`: exit 0.
- `cmp -s Docs/Audit/UltravioletObligations.csv HelloUltraviolet/Audit/UltravioletObligations.csv`:
  exit 0 after refreshing the project-local CSV copy from the authoritative
  audit ledger.
- `python3 .agents/scripts/generate_hello_catalog.py`: exit 0.
- `LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --incremental on --max-errors 30`:
  exit 0, 14 warnings, 10 infos, 318.94s, 24 objects rebuilt and 35 reused
  after regenerating the catalog from the refreshed CSV.
- `HelloUltraviolet/build/bin/HelloUltraviolet.exe`: exit 0.
- `HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit`: exit 0.
- `python3 Tools/ExtractObligationLedger.py --check`: exit 0,
  `PASS obligations=6045`.
- `GIT_LFS_SKIP_SMUDGE=1 git -c filter.lfs.process= -c filter.lfs.required=false diff --check -- ...`:
  exit 0 for the touched HelloUltraviolet and bootstrap compiler paths.
- `Fixtures/RejectedSource/Procedures/BehavioralSubtypingPrecondition`: exit 1
  with `E-SEM-2803`.
- `Fixtures/RejectedSource/Procedures/BehavioralSubtypingPostcondition`: exit 1
  with `E-SEM-2804`.
- `Fixtures/RejectedSource/Procedures/BehavioralSubtypingLiskov`: exit 1 with
  `E-SEM-2803`.
- `Fixtures/RejectedSource/Procedures/BehavioralSubtypingDiagnostics`: exit 1
  with `E-SEM-2803`.
- Focused diagnostic-fixture verification now emits `E-MEM-1207`,
  `E-MEM-1208`, `E-MEM-3030`, `E-SEM-2526`, `E-TYP-2103`, `E-TYP-2106`, and
  `E-MOD-1307` for the repaired static-rule routing cases.
- The remaining blank expected diagnostic codes are the spec-defined uncoded
  user note and the Chapter 15 missing-return-annotation / non-boolean-contract
  cases recorded in `SpecClarificationsNeeded.md`.
