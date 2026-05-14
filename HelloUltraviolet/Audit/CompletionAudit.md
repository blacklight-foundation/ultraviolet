# HelloUltraviolet Completion Audit

Objective: execute `.agents/plans/HelloUltravioletReferenceCorpus.md`.

## Verified Deliverables

- Project folder exists at `HelloUltraviolet/`.
- Project-local obligation ledger exists at
  `HelloUltraviolet/Audit/UltravioletObligations.csv`.
- The generated ledger check passes:
  `python3 Tools/ExtractObligationLedger.py --check`.
- `HelloUltraviolet/Source/Main.uv` calls the executable reference corpus through
  `runReferenceCorpus(context)`.
- All 149 files under `HelloUltraviolet/Source/Reference` contain executable
  reference bodies; the simple placeholder run-body count is `0`.
- The generated catalog contains 5,986 primary obligation rows in
  `HelloUltraviolet/Source/Audit/Catalog/**/*.uv`. Each row records the
  obligation id, internal spec line, module path, symbol, and source path in
  Ultraviolet source.
- `CoverageCheck.uv` verifies both the generated obligation total and the
  generated primary-reference validation total against `EXPECTED_OBLIGATION_COUNT`.
- `CatalogCsvMembership.uv` compares the 5,986 project-local CSV obligation
  keys with the 5,986 generated catalog primary keys in compiled Ultraviolet
  source.
- `CatalogPrimaryReferences.uv` checks that the 5,986 generated primary
  obligation references are unique by sorting `(id, internal_spec_line)` keys
  and verifying strict adjacent ordering in compiled Ultraviolet source.
- `CatalogSourcePaths.uv` checks the 65 unique source files referenced by
  generated catalog rows, and `HelloUltraviolet.exe` validates their runtime
  existence through `catalogSourcePathsExist(context)`.
- `CatalogSymbols.uv` imports and executes the 65 unique compiled reference
  symbols named by catalog rows, and `HelloUltraviolet.exe` validates them
  through `catalogCompiledSymbolsExecute()`.
- `Source/Fixtures/RejectedSource` compiles metadata for 368 rejected-source
  fixture specimens, and `HelloUltraviolet.exe` validates that fixture index
  through `rejectedSourceFixturesAreIndexed`.
- The 368 rejected-source fixture projects under
  `HelloUltraviolet/Fixtures/RejectedSource` fail with their expected SPEC
  diagnostic code or static-rule diagnostic when built individually with
  `Cursive.exe build ... --check`.
- Each rejected-source fixture includes an `Expected.uv` metadata artifact, and
  the compiled metadata records both the invalid source path and expected
  diagnostic metadata path.
- `HelloUltraviolet.exe` verifies runtime existence of each rejected fixture
  manifest, invalid source file, and `Expected.uv` artifact through
  `rejectedSourceFixtureArtifactsExist(context)`.
- `ExpectedFiles.uv` reads the 368 current rejected-source `Expected.uv`
  artifacts and `HelloUltraviolet.exe` validates exact metadata content through
  one named check per specimen.
- `Source/Fixtures/DiagnosticSource` compiles metadata for 22 diagnostic-source
  fixture specimens whose source is expected to compile while emitting SPEC
  warnings or informational diagnostics, or while proving a SPEC diagnostic is
  absent, and `HelloUltraviolet.exe` validates the index, artifact paths, and
  exact `Expected.uv` metadata through the diagnostic-source fixture checks.
- `Source/Fixtures/OutputDiagnostics` compiles metadata for the
  `LlvmToolResolveOwnership` and `EmitLLVMRenderFailure` output-diagnostic
  fixtures, and `HelloUltraviolet.exe` validates the index, source artifacts,
  exact `Expected.uv` metadata, and conformance-log ownership markers through
  the output-diagnostic fixture checks.
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
  and user `drop` lowering for a non-`Bitcopy` record; runtime string/bytes
  interface calls; exported symbol mangling; and backend arithmetic/branching.
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
  generic clauses, generic tuple-return procedures, class-bound generic method
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
- The 21 key-system rejected-source fixtures under
  `Fixtures/RejectedSource/Keys` reject with their expected `E-CON-*`
  diagnostics, and the 10 key-system diagnostic-source fixtures under
  `Fixtures/DiagnosticSource/Keys` compile with their expected `W-CON-*` or
  `I-CON-*` diagnostics or prove expected diagnostic absence.
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
- The bootstrap class-implementation checker now evaluates
  `def.ImplementationOrphanRule` for record, enum, and modal `implements`
  clauses in `CheckOrphanRule`, using the first module-path segment as the
  assembly identity and routing violations to `E-TYP-2507`.
- `Source/Fixtures/AcceptedProjects` compiles metadata for 6 accepted project
  fixture specimens, and `HelloUltraviolet.exe` validates both the index and
  artifact paths through accepted-project fixture checks.
- `Fixtures/AcceptedProjects/StaticLibrary` builds as a valid static library
  project, `Fixtures/AcceptedProjects/ExecutableMain` builds and runs as a
  valid executable project, `Fixtures/AcceptedProjects/PtrNullReturn` builds as
  a valid library project for checked `Ptr::null()` return typing, and
  `Fixtures/AcceptedProjects/ExpressionSemantics` builds as a valid library
  project for rule-level expression typing, and
  `Fixtures/AcceptedProjects/VerificationFactPrecondition` builds as a valid
  library project for branch-generated verification facts discharging
  preconditions.
- `Fixtures/AcceptedProjects/CrossAssemblyImplementation` builds as a valid
  multi-assembly library project. The selected library assembly imports a
  dependency assembly, implements the dependency's public required-field class
  with a local record, and returns the implemented field value. This exercises
  the valid source surface for `def.ImplementationOrphanRule` and
  `req.14.ImplementationOrphanRequirement`: the implementing type is local to
  the current assembly while the class is imported from another assembly.
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
  `LLVMEmitObj_21` after a module exists.
- The project check gate passes:
  `Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64
  --build-progress off`.
- The project build gate passes:
  `Cursive.exe build HelloUltraviolet --target-profile x86_64-win64
  --build-progress on`, captured in
  `Build/HelloUltraviolet.after-generated-procs.build.stderr`; it rebuilt all
  58 modules and produced
  `HelloUltraviolet/build/bin/HelloUltraviolet.exe`.
- The executable gate passes:
  `HelloUltraviolet/build/bin/HelloUltraviolet.exe`.
- The focused bootstrap regression gate passes:
  `cursive_codegen_abi_sret_test.exe`.
- Whitespace validation passes:
  `git diff --check -- HelloUltraviolet ...bootstrap files...`.

## Completion Blockers

- Rejected-source, diagnostic-source, and output-diagnostic fixtures are
  partially populated. The current fixture set covers 368 rejected-source
  diagnostics, 22 compiling diagnostic-source warning/info/absence cases, and
  2 output-diagnostic artifact cases; the full expected-diagnostics obligation
  surface is not yet represented. Of the 382 expected-diagnostic obligations,
  5 remain uncovered by current expected-diagnostic metadata. Remaining
  uncovered expected-diagnostic ownership counts are: lowering 2,
  abstraction/polymorphism 2, statements 1.
  `HelloUltraviolet/Audit/SpecClarificationsNeeded.md` records the current
  sourceability questions for those five rows.
- `Fixtures/BootstrapNonCompliance/Procedures/FreeProcedureOverloadResolution`
  now passes both semantic checking and the standalone library build after the
  bootstrap repair in `collect_toplevel.cpp` and `expr/call.cpp`. The
  `NoMatchingOverload` rejected-source fixture covers the no-match branch of
  the free-call overload selection algorithm.
- `rule.18.BlockInfo-Res-Err` remains unindexed pending source-construct
  clarification. The row rejects block result prefixes whose `Res` set has no
  common type, but the Chapter 18 rules inspected here produce `Res = []` for
  expression, unsafe, region, and frame statements, and route `break` values
  through `Brk` instead of `Res`. A corrected probe using two block-expression
  statement prefixes with incompatible tail types was rejected as
  `E-SEM-3161` at the enclosing `return`, not as `BlockInfo-Res-Err`. The
  bootstrap owner path is `TypeBlockInfo` in
  `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/stmt_common.cpp`; it
  forwards only nested statement-block results with type `!` into
  `flow.results`, which can exercise `BlockInfo-Res` but does not create a
  heterogeneous `Res` set for `BlockInfo-Res-Err`. This is tracked in
  `HelloUltraviolet/Audit/SpecClarificationsNeeded.md`.
- `rule.14.Impl-Orphan-Err` and
  `req.14.ImplementationOrphanRequirement` remain unindexed as concrete
  expected-diagnostic obligations. The accepted
  `CrossAssemblyImplementation` fixture now exercises the valid cross-assembly
  implementation surface for the same orphan requirement. `SPECIFICATION.md:13033` states that class
  implementation occurs at the defining record, enum, or modal declaration and
  that standalone extension implementation blocks are not part of the language.
  Under that surface, an ordinary source implementation always has the
  implementing declaration in the current assembly. The bootstrap checker now
  owns the rule and emits `E-TYP-2507` if an implementation relation is ever
  presented outside that owner context; there is no current SPEC-valid source
  spelling for the rejecting case. This is tracked in
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
  `Fixtures/RejectedSource/Statements/FrameDiagnostic` reject with the
  uncoded static-rule diagnostics `Frame-NoActiveRegion-Err` and
  `Frame-Target-NotActive-Err`, exercising
  `rule.18.Frame-NoActiveRegion-Err`, `rule.18.Frame-Target-NotActive-Err`,
  and `diag.18.Frame`. This matches the SPEC diagnostic-code selection rule:
  `SPECIFICATION.md:477-478` states that `SpecCode(id) = ⊥` when the owning
  construct section assigns no diagnostic code, and the frame rules at
  `SPECIFICATION.md:19950-19958` do not assign concrete codes.
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
  rejects with the uncoded static-rule diagnostic `Transmute-Unsafe-Err`,
  exercising `diag.18.UnsafeRequiredOperationOwnership` through the construct
  owner named by `SPECIFICATION.md:20217`. The transmute rule is defined at
  `SPECIFICATION.md:16569-16572`, and the expression diagnostic table at
  `SPECIFICATION.md:18027-18049` assigns concrete codes for invalid casts,
  transmute size mismatch, transmute alignment mismatch, and invalid bit-pattern
  warnings, but assigns no concrete code for transmute outside `unsafe`.
- Accepted-project fixtures are partially populated with 6 buildable projects.
  Artifact-project fixtures are partially populated with 7 buildable projects.
- Some high-level areas currently use executable reference models rather than
  source specimens for every concrete syntax form, notably structured
  parallelism, async, compile-time forms, and FFI.
  Key-system source specimens now exercise selected rejected-source and
  diagnostic-source obligations, with accepted/source-runtime coverage still
  incomplete.

## Current Status

The reference corpus is executable, the generated catalog now materializes every
ledger row in Ultraviolet source, and all verification gates listed above pass.
The plan is not complete until the remaining fixture and concrete-specimen
requirements above are implemented and verified.
