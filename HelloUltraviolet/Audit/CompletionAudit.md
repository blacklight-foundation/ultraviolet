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
- `Source/Fixtures/RejectedSource` compiles metadata for 279 rejected-source
  fixture specimens, and `HelloUltraviolet.exe` validates that fixture index
  through `rejectedSourceFixturesAreIndexed`.
- The 279 rejected-source fixture projects under
  `HelloUltraviolet/Fixtures/RejectedSource` fail with their expected SPEC
  diagnostic code or static-rule diagnostic when built individually with
  `Cursive.exe build ... --check`.
- Each rejected-source fixture includes an `Expected.uv` metadata artifact, and
  the compiled metadata records both the invalid source path and expected
  diagnostic metadata path.
- `HelloUltraviolet.exe` verifies runtime existence of each rejected fixture
  manifest, invalid source file, and `Expected.uv` artifact through
  `rejectedSourceFixtureArtifactsExist(context)`.
- `ExpectedFiles.uv` reads the 279 current rejected-source `Expected.uv`
  artifacts and `HelloUltraviolet.exe` validates exact metadata content through
  one named check per specimen.
- `Source/Fixtures/DiagnosticSource` compiles metadata for 11 diagnostic-source
  fixture specimens whose source is expected to compile while emitting SPEC
  warnings or informational diagnostics, or while proving a SPEC diagnostic is
  absent, and `HelloUltraviolet.exe` validates the index, artifact paths, and
  exact `Expected.uv` metadata through the diagnostic-source fixture checks.
- `Fixtures/DiagnosticSource/Expressions/ValidTransmuteTarget` builds with exit
  code 0 and emits `W-SAFE-0100` for a valid unsafe transmute whose target type
  is known to admit invalid bit patterns.
- The 16 key-system rejected-source fixtures under
  `Fixtures/RejectedSource/Keys` reject with their expected `E-CON-*`
  diagnostics, and the 10 key-system diagnostic-source fixtures under
  `Fixtures/DiagnosticSource/Keys` compile with their expected `W-CON-*` or
  `I-CON-*` diagnostics or prove expected diagnostic absence.
- `Fixtures/DiagnosticSource/Keys/StaleAfterYieldReleaseWarning` emits
  `W-CON-0011` for stale binding use after `yield release`, while
  `Fixtures/DiagnosticSource/Keys/StaleOkSuppressesReleaseWarning` uses the
  same source shape with `[[stale_ok]]` and verifies `W-CON-0011` is absent.
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
- `Source/Fixtures/AcceptedProjects` compiles metadata for 5 accepted project
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
- `Source/Fixtures/ArtifactProjects` compiles metadata for 3 artifact project
  fixture specimens, and `HelloUltraviolet.exe` validates both the index and
  source/manifest paths through artifact-project fixture checks.
- `Fixtures/ArtifactProjects/StaticLibraryArchive` builds a `.lib` archive and
  `.obj`, `Fixtures/ArtifactProjects/EmitLlLibrary` builds a `.ll`, `.lib`, and
  `.obj`, and `Fixtures/ArtifactProjects/ExecutableOutput` builds and runs an
  `.exe`, `.map`, and `.obj` artifact.
- The project check gate passes:
  `Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64
  --build-progress off`.
- The project build gate passes:
  `Cursive.exe build HelloUltraviolet --target-profile x86_64-win64
  --build-progress off`.
- The executable gate passes:
  `HelloUltraviolet/build/bin/HelloUltraviolet.exe`.
- The focused bootstrap regression gate passes:
  `cursive_codegen_abi_sret_test.exe`.
- Whitespace validation passes:
  `git diff --check -- HelloUltraviolet ...bootstrap files...`.

## Completion Blockers

- Rejected-source and diagnostic-source fixtures are partially populated. The
  current fixture set covers 279 rejected-source diagnostics and 11 compiling
  diagnostic-source warning/info/absence cases; the full expected-diagnostics
  obligation surface is not yet represented. Of the 382 expected-diagnostic
  obligations, 116 remain
  uncovered. Remaining uncovered expected-diagnostic ownership counts are:
  abstraction/polymorphism 2, async 41, compile-time 27,
  key-system 1, lowering 3, procedures/contracts 3, statements 1,
  and structured parallelism 38.
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
  heterogeneous `Res` set for `BlockInfo-Res-Err`.
- `rule.17.Pat-Tuple-R-Arity-Err` remains unindexed as a concrete
  expected-diagnostic obligation. `SPECIFICATION.md:18119` names
  `Code(Pat-Tuple-Arity-Err)`, but the Chapter 17 diagnostic table at
  `SPECIFICATION.md:18729-18737` does not assign a concrete diagnostic code
  for tuple-pattern arity mismatch. The current bootstrap therefore emits an
  uncoded static-rule diagnostic for the `TuplePatternArity` specimen.
- `rule.18.Frame-NoActiveRegion-Err`, `rule.18.Frame-Target-NotActive-Err`,
  and `diag.18.Frame` remain unindexed as concrete expected-diagnostic
  obligations. `SPECIFICATION.md:19704-19710` names frame diagnostic codes, but
  the Chapter 18 diagnostic table at `SPECIFICATION.md:19996-20008` does not
  assign concrete codes for the frame rules. The current bootstrap emits
  uncoded static-rule diagnostics for the frame specimens.
- `diag.18.UnsafeRequiredOperationOwnership` remains unindexed. The current
  fixture exercises `Transmute-Unsafe-Err`, but the expression diagnostic table
  does not assign a concrete diagnostic code for unsafe transmute outside an
  unsafe span, so the bootstrap emits an uncoded static-rule diagnostic.
- Accepted-project fixtures are partially populated with 5 buildable projects.
  Artifact-project fixtures are partially populated with 3 buildable projects.
- Some high-level areas currently use executable reference models rather than
  source specimens for every concrete syntax form, notably polymorphism,
  structured parallelism, async, compile-time forms, FFI, and backend artifacts.
  Key-system source specimens now exercise selected rejected-source and
  diagnostic-source obligations, with accepted/source-runtime coverage still
  incomplete.

## Current Status

The reference corpus is executable, the generated catalog now materializes every
ledger row in Ultraviolet source, and all verification gates listed above pass.
The plan is not complete until the remaining fixture and concrete-specimen
requirements above are implemented and verified.
