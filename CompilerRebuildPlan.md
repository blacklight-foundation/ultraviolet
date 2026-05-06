# Ultraviolet Compiler Rebuild Plan

Date: 2026-05-05

This plan lives in the Ultraviolet repository and is the execution contract for rebuilding the full Ultraviolet compiler in Ultraviolet, bootstrapped by the existing Cursive compiler. It is intentionally concrete: implementation must migrate one audited object at a time, but the target architecture is the complete compiler, not a minimal vertical slice.

The authoritative language contract is `SPECIFICATION.md`. The current obligation ledger used by this plan is `Docs/Audit/UltravioletObligations.csv`; the scaffold pass must preserve ledger contents while normalizing final audit/tool names to the PascalCase paths defined below.
The authoritative language contract is `SPECIFICATION.md`. The current obligation ledger used by this plan is `Docs/Audit/UltravioletObligations.csv`; the scaffold pass must preserve ledger contents while normalizing final audit/tool names to the PascalCase paths defined below.

## Non-Negotiable Constraints

- `SPECIFICATION.md` is the source of truth. Do not edit it as part of the rebuild unless the user explicitly approves the spec change.
- The rebuild is a full self-hosting compiler implementation. Do not create a partial/minimal compiler architecture and do not mechanically rename C++ files into Ultraviolet.
- Every target directory is PascalCase. Every Ultraviolet source file is `PascalCase.uv` except externally mandated names inside source text, ABI symbols, serialized keys, and the public `uv` command name.
- The Cursive compiler is the bootstrap compiler. It may be patched only for bootstrap blockers or confirmed spec-conformance defects needed to compile Ultraviolet.
- The Cursive compiler is the bootstrap compiler. It may be patched only for bootstrap blockers or confirmed spec-conformance defects needed to compile Ultraviolet.
- No shim, adapter, duplicate implementation, fallback path, test-only branch, or compatibility layer is allowed unless the specification explicitly defines that boundary.
- Keep the obligation extraction/ledger tooling as the only support tooling; conformance evidence must come from compiler tests, source-native test procedures, traces, and bootstrap outputs.
- Ultraviolet is self-hosted. The Ultraviolet compiler implementation must own its lowering, target-instruction lowering, object emission, archive emission, and final artifact production. Do not depend on LLVM IR, LLVM libraries, `llvm-as`, `lld-link`, `llvm-lib`, or an LLVM-shaped backend architecture in Ultraviolet source. The Cursive bootstrap compiler may have its own bootstrap implementation details, but no LLVM dependency is allowed to carry into self-hosted Ultraviolet compiler generations.
- Keep the obligation extraction/ledger tooling as the only support tooling; conformance evidence must come from compiler tests, source-native test procedures, traces, and bootstrap outputs.
- Ultraviolet is self-hosted. The Ultraviolet compiler implementation must own its lowering, target-instruction lowering, object emission, archive emission, and final artifact production. Do not depend on LLVM IR, LLVM libraries, `llvm-as`, `lld-link`, `llvm-lib`, or an LLVM-shaped backend architecture in Ultraviolet source. The Cursive bootstrap compiler may have its own bootstrap implementation details, but no LLVM dependency is allowed to carry into self-hosted Ultraviolet compiler generations.
- Windows `x86_64-win64` is the first bootstrap target. The implementation must not infer a target profile from the host platform.

## Target Repository Shape

The scaffold pass must produce the exact file tree below. Do not add compiler or runtime source files outside this tree unless this plan is updated first. The public command remains `uv`; that is a command-name exception, not a directory-name exception. Directories use PascalCase and acronym-preserving names such as `IR`, `ABI`, `COFF`, `ELF`, and `IO`.
The scaffold pass must produce the exact file tree below. Do not add compiler or runtime source files outside this tree unless this plan is updated first. The public command remains `uv`; that is a command-name exception, not a directory-name exception. Directories use PascalCase and acronym-preserving names such as `IR`, `ABI`, `COFF`, `ELF`, and `IO`.

### Exact File Tree

```text
.gitattributes
.gitignore
README.md
LICENSE.md
SPECIFICATION.md
.gitattributes
.gitignore
README.md
LICENSE.md
SPECIFICATION.md
Ultraviolet.toml
CompilerRebuildPlan.md
AGENTS.md
Compiler/
  Api.uv
  Core/
    BehaviorModel.uv  [owns behavior, undefinedness, and ill-formed rejection obligations]
    ConformanceModel.uv  [owns conformance vocabulary, constructs, checks, and recovery obligations]
      Path/
        Path.uv  [owns the public path value surface and path operation entrypoints]
        Text.uv  [owns path text ownership states and byte-view access]
        Roots.uv  [owns root predicates, root tags, tails, and absolute-path classification]
        Components.uv  [owns path segment and component extraction]
        Sequences.uv  [owns state-specific component sequence representation]
      Algebra.uv  [owns component-based path algebra]
    Spans.uv  [owns source-span obligations]
    SpecificationTrace.uv  [owns specification document, keyword, diagnostic-code, and reference obligations]
  Diagnostics/
    Codes.uv
    Diagnostic.uv
    Emission.uv
    Ordering.uv
    Recovery.uv
    Rendering.uv
    Sources.uv
  Project/
    AssemblyGraph.uv
    AssemblyLoader.uv
    AssemblySelection.uv
    BuildConfig.uv
    DeterministicOrder.uv
    Manifest.uv
    ManifestValidation.uv
    ModuleDiscovery.uv
    OutputArtifacts.uv
    PathResolution.uv
    ProjectModel.uv
    RootDiscovery.uv
    TargetProfile.uv
    ToolchainConfig.uv
  Source/
    Text/
      LogicalLines.uv
      SourceLoading.uv
      UnicodeSecurity.uv
      UTF8.uv
    Lexer/
      CharacterClasses.uv
      Comments.uv
      Identifiers.uv
      Keywords.uv
      Literals.uv
      Operators.uv
      Scanner.uv
      Tokenize.uv
      Tokens.uv
    Ast/
      AstDump.uv
      AstNodes.uv
      Attributes.uv
      Expressions.uv
      Items.uv
      Modules.uv
      Patterns.uv
      Statements.uv
      Types.uv
    Parser/
      AttributeParser.uv
      DocComments.uv
      ExpressionParser.uv
      ItemParser.uv
      ListParser.uv
      ModuleParser.uv
      ParserState.uv
      PathParser.uv
      PatternParser.uv
      PermissionParser.uv
      Recovery.uv
      Terminators.uv
      TokenCursor.uv
      TypeParser.uv
  Comptime/
    DependencyOrder.uv
    Derive.uv
    Eval.uv
    Files.uv
    Hygiene.uv
    Pass.uv
    Quote.uv
    Reflect.uv
    Rewrite.uv
  Driver/
    CLI/
      Api.uv
      BuildCommand.uv
      CheckCommand.uv
      CleanCommand.uv
      Commands.uv
      InitCommand.uv
      Options.uv
      RunCommand.uv
      TestCommand.uv
      Version.uv
    CLI/
      Api.uv
      BuildCommand.uv
      CheckCommand.uv
      CleanCommand.uv
      Commands.uv
      InitCommand.uv
      Options.uv
      RunCommand.uv
      TestCommand.uv
      Version.uv
    ConformanceTrace.uv
    CrashReporting.uv
    Fingerprints.uv
    Incremental.uv
    Pipeline.uv
    TestCoverage.uv
    TestDiscovery.uv
    TestExecution.uv
    TestHarness.uv
    TestResults.uv
    TestCoverage.uv
    TestDiscovery.uv
    TestExecution.uv
    TestHarness.uv
    TestResults.uv
  Resolve/
    Disambiguation.uv
    Imports.uv
    Lookup.uv
    Modules.uv
    NameIntroduction.uv
    Qualified.uv
    ResolveContracts.uv
    ResolveExpressions.uv
    ResolveItems.uv
    ResolvePatterns.uv
    ResolveTypes.uv
    Scopes.uv
    Using.uv
    Visibility.uv
  Semantics/
    RuntimeModel/
      AbstractMachine.uv
      ObservableBehavior.uv
      SequencePoints.uv
    Capabilities/
      AuthorityModel.uv
      CallGraph.uv
      ContextCapabilities.uv
      FileSystemCapabilities.uv
      HeapCapabilities.uv
      NetworkCapabilities.uv
      SystemCapabilities.uv
    Attributes/
      AttributeTargets.uv
      AttributeValidation.uv
      DiagnosticsMetadata.uv
      LayoutAttributes.uv
      OptimizationAttributes.uv
      TestAttributes.uv
      TestAttributes.uv
    Types/
      Aliases.uv
      Arrays.uv
      CapabilityClasses.uv
      Classes.uv
      Closures.uv
      DynamicObjects.uv
      Enums.uv
      FunctionTypes.uv
      Generics.uv
      ModalTypes.uv
      OpaqueTypes.uv
      Pointers.uv
      PrimitiveTypes.uv
      Ranges.uv
      Records.uv
      Refinements.uv
      Slices.uv
      StringsBytes.uv
      Tuples.uv
      TypeEquivalence.uv
      TypeInference.uv
      TypeModel.uv
      TypeWellFormedness.uv
      Unions.uv
    Permissions/
      Admissibility.uv
      AliasExclusivity.uv
      BindingState.uv
      DynamicContext.uv
      PermissionModel.uv
    Memory/
      DropSemantics.uv
      InitializationState.uv
      Provenance.uv
      Regions.uv
      SafePointers.uv
    Declarations/
      Contracts.uv
      Invariants.uv
      Methods.uv
      Procedures.uv
      TypeDeclarations.uv
    Expressions/
      Calls.uv
      Closures.uv
      ExpressionCheck.uv
      Literals.uv
      Operators.uv
      Places.uv
    Statements/
      Blocks.uv
      ControlFlow.uv
      Defer.uv
      LetVar.uv
      StatementCheck.uv
    Patterns/
      Exhaustiveness.uv
      PatternCheck.uv
      PatternModel.uv
    Keys/
      KeyAcquisition.uv
      KeyConflict.uv
      KeyLifetimes.uv
      KeyModel.uv
    Parallelism/
      Dispatch.uv
      Parallel.uv
      Spawn.uv
      StructuredConcurrency.uv
    Async/
      AsyncModel.uv
      Await.uv
      Future.uv
      Race.uv
      Sync.uv
    FFI/
      ExportValidation.uv
      ForeignBoundary.uv
      HostedExports.uv
      UnsafeSurface.uv
    Initialization/
      Cleanup.uv
      ModuleLifecycle.uv
      StaticInitialization.uv
  Lowering/
    AttributeLowering.uv
    BindingLowering.uv
    CheckLowering.uv
    CleanupLowering.uv
    ExpressionLowering.uv
    InitializationLowering.uv
    LoweringContext.uv
    ModuleLowering.uv
    PatternLowering.uv
    PermissionLowering.uv
    StatementLowering.uv
  Backend/
    BackendDiagnostics.uv
    BackendPipeline.uv
    IR/
      Globals.uv
      IRBuilder.uv
      IRDump.uv
      IRModel.uv
      Literals.uv
      Poison.uv
      RuntimeSymbols.uv
      VTables.uv
    ABI/
      ABICalls.uv
      ABIModel.uv
      ABIParameters.uv
      ABIReturns.uv
      ABITypes.uv
      CallingConventions.uv
      ForeignABI.uv
      HostedABI.uv
    InstructionLowering/
      CallingSequence.uv
      FrameLayout.uv
      InstructionSelection.uv
      MachineIR.uv
      RegisterAllocation.uv
      TargetLowering.uv
      TargetMachine.uv
    ObjectFormat/
      COFFObject.uv
      ELFObject.uv
      ObjectWriter.uv
      Relocations.uv
    InstructionLowering/
      CallingSequence.uv
      FrameLayout.uv
      InstructionSelection.uv
      MachineIR.uv
      RegisterAllocation.uv
      TargetLowering.uv
      TargetMachine.uv
    ObjectFormat/
      COFFObject.uv
      ELFObject.uv
      ObjectWriter.uv
      Relocations.uv
    Link/
      ArchivePlan.uv
      ArtifactFinalize.uv
      ExternLibraries.uv
      LinkPlan.uv
      RuntimeResolution.uv
      ToolInvocation.uv
  LanguageService/
    DiagnosticsAdapter.uv
    DocumentStore.uv
    JSONRPC.uv
    LineIndex.uv
    LSPProtocol.uv
    Navigation.uv
    Rename.uv
    SemanticTokens.uv
    Server.uv
    Snapshot.uv
    SymbolIndex.uv
  Tests/
    TestSupport/
      Assertions.uv
      CompileCase.uv
      CompileResult.uv
      Diagnostics.uv
      Sources.uv
      TestContext.uv
    Parser/
      AttributeTests.uv
      ContractTests.uv
    Checker/
      TypeTests.uv
    Lowering/
      LoweringTests.uv
    Backend/
      InstructionLoweringTests.uv
      ObjectFormatTests.uv
    Core/
      Path/
        AbsPathTests.uv
        BasenameTests.uv
        CanonTests.uv
        DropTests.uv
        FileExtTests.uv
        JoinCompTests.uv
        JoinTests.uv
        NormalizeTests.uv
        PathComponentsTests.uv
        PathFunctionTypesTests.uv
        PathPrefixTests.uv
        PathRootPredicatesTests.uv
        PathRootTagAndTailTests.uv
        PathSegmentsTests.uv
        RelativePathComputationTests.uv
        UnderTests.uv
  Tests/
    TestSupport/
      Assertions.uv
      CompileCase.uv
      CompileResult.uv
      Diagnostics.uv
      Sources.uv
      TestContext.uv
    Parser/
      AttributeTests.uv
      ContractTests.uv
    Checker/
      TypeTests.uv
    Lowering/
      LoweringTests.uv
    Backend/
      InstructionLoweringTests.uv
      ObjectFormatTests.uv
    Core/
      Path/
        AbsPathTests.uv
        BasenameTests.uv
        CanonTests.uv
        DropTests.uv
        FileExtTests.uv
        JoinCompTests.uv
        JoinTests.uv
        NormalizeTests.uv
        PathComponentsTests.uv
        PathFunctionTypesTests.uv
        PathPrefixTests.uv
        PathRootPredicatesTests.uv
        PathRootTagAndTailTests.uv
        PathSegmentsTests.uv
        RelativePathComputationTests.uv
        UnderTests.uv
Runtime/
  Api.uv
  Host/
    ABI.uv
    PanicBoundary.uv
    Platform.uv
    Session.uv
    Startup.uv
  Context/
    Capabilities.uv
    Context.uv
    FileSystemContext.uv
    HeapContext.uv
    NetworkContext.uv
    SystemContext.uv
  Memory/
    Allocation.uv
    AsyncStorage.uv
    BindingStore.uv
    Bytes.uv
    Frames.uv
    Regions.uv
    Strings.uv
    ValueModel.uv
  Concurrency/
    Cancellation.uv
    Dispatch.uv
    Keys.uv
    Parallel.uv
    Reactor.uv
    Spawn.uv
  IO/
    Directory.uv
    File.uv
    FileSystem.uv
    Paths.uv
  Network/
    Network.uv
    Restrictions.uv
  System/
    DynamicLibrary.uv
    Environment.uv
    Process.uv
  Tests/
    Context/
      CapabilityTests.uv
    Memory/
      RegionTests.uv
    Concurrency/
      KeyTests.uv
  Tests/
    Context/
      CapabilityTests.uv
    Memory/
      RegionTests.uv
    Concurrency/
      KeyTests.uv
Tools/
  Uv/
    Main.uv
    Tests/
      Bootstrap/
        CursiveBootstrapTests.uv
        SelfHostRebuildTests.uv
        FixedPointTests.uv
  ExtractObligationLedger.py
    Tests/
      Bootstrap/
        CursiveBootstrapTests.uv
        SelfHostRebuildTests.uv
        FixedPointTests.uv
  ExtractObligationLedger.py
Docs/
  Audit/
    FileObligationMap.csv
    FileObligationMap.csv
    MigrationLedger.md
    UltravioletObligations.csv
  Internal/
    UltravioletSpecification.obligations.md
```

`Ultraviolet.toml` must define exactly these initial assemblies:

- `UltravioletRT`: `kind = "library"`, `link_kind = "static"`, `root = "Runtime"`; produces `UltravioletRT.lib` on `x86_64-win64`.
- `UltravioletCompiler`: `kind = "library"`, `link_kind = "static"`, `root = "Compiler"`; contains all compiler logic except the CLI entrypoint.
- `uv`: `kind = "executable"`, `root = "Tools/Uv"`; this is the only lowercase assembly-name exception because it owns the public command artifact.

Only `uv` is executable so default assembly selection is unambiguous. Source-native tests live in `Tests` submodules inside their parent assembly roots: `Compiler/Tests` for `UltravioletCompiler`, `Runtime/Tests` for `UltravioletRT`, and `Tools/Uv/Tests` for `uv`.

### Runtime Naming Contract

The existing bootstrap runtime in `/mnt/c/Dev/Cursive/cursive/runtime` uses C
runtime naming at its C ABI boundary:

- CMake targets and artifacts use the existing spellings such as `cursive0_rt`
  and `CursiveRT`.
- C runtime symbols use snake_case prefixes such as `cursive_rt_*` and
  `cursive_platform_*`.
- Generated Cursive runtime symbols keep their escaped C ABI spelling, including
  `cursive_x3a_x3aruntime_*`.

The Ultraviolet runtime source tree uses the Ultraviolet style guide:

- Directories, modules, files, types, and assembly names use PascalCase with
  preserved established acronyms.
- Procedures and methods use camelCase.
- Local variables and parameters use snake_case.
- The public command assembly remains `uv`.
- The runtime library artifact spelling remains `UltravioletRT.lib` or
  `UltravioletRT.a`, as required by `def.RuntimeLibName@L5641` and
  `def.24.DefaultCallingConventionAndTargetArtifacts@L90752`.

Within Ultraviolet source, use full source names such as `Runtime`,
`RuntimeSymbol`, `RuntimeLibrary`, `StringView`, `StringManaged`, `BytesView`,
and `BytesManaged`. Use `RT` only for the spec-owned runtime library artifact
name or an external C/runtime ABI spelling.

### First Migration Object

The first migration object is W0 repository scaffold and `Ultraviolet.toml`. W0 creates the PascalCase source roots, test roots, `Docs/Audit`, `Docs/Internal`, `Docs/Audit/MigrationLedger.md`, and the initial manifest. W0 contains no compiler behavior migration.

Compiler source-object migration begins only after the readiness gates in `Execution Progression` are complete.

### Self-Hosted Backend Specification Reconciliation

The current `SPECIFICATION.md` and `Docs/Audit/UltravioletObligations.csv` still contain LLVM-specific backend obligations, including owners such as `backend.llvm-target`, `backend.llvm-codegen`, and LLVM-specific output/tool-resolution rules. Those obligations are retained in this plan for traceability only. They must be reconciled before backend implementation by a user-approved specification update that replaces LLVM-specific requirements with Ultraviolet-owned target-machine lowering, object emission, archive emission, and final-artifact production.

Implementation must not satisfy self-hosting by wrapping, embedding, shelling out to, or mechanically reproducing LLVM. The correct target is an Ultraviolet backend that lowers accepted semantic IR into target-specific object artifacts directly.

### Backend Model Decision

Ultraviolet must not model Rust's default backend contract, where the language compiler lowers to LLVM IR and an external backend owns target instruction selection, target-specific optimization, and object generation. That model is rejected for Ultraviolet because it would make LLVM IR the effective backend boundary and would prevent the self-hosted Ultraviolet compiler generations from being fully self-hosted.

Ultraviolet can still use explicit IR boundaries, monomorphization boundaries when required by generics, deterministic lowering traces, and pass-local conformance tests. Those are implementation practices, not permission to delegate the backend contract.

The selected model is self-owned instruction lowering:

1. `Compiler/Backend/IR` owns target-independent backend IR.
2. `Compiler/Backend/InstructionLowering` lowers backend IR to target-processor instructions represented in `MachineIR`.
3. `Compiler/Backend/ObjectFormat` encodes `MachineIR`, relocation records, symbols, and data into COFF/ELF object bytes.
4. `Compiler/Backend/Link` owns archives, final artifact production, runtime resolution, and linker behavior.

Textual assembly is not the canonical backend boundary. It may be added later only as a diagnostic listing or debugging output, and only if it is generated from the canonical `MachineIR` path.
Only `uv` is executable so default assembly selection is unambiguous. Source-native tests live in `Tests` submodules inside their parent assembly roots: `Compiler/Tests` for `UltravioletCompiler`, `Runtime/Tests` for `UltravioletRT`, and `Tools/Uv/Tests` for `uv`.

### Runtime Naming Contract

The existing bootstrap runtime in `/mnt/c/Dev/Cursive/cursive/runtime` uses C
runtime naming at its C ABI boundary:

- CMake targets and artifacts use the existing spellings such as `cursive0_rt`
  and `CursiveRT`.
- C runtime symbols use snake_case prefixes such as `cursive_rt_*` and
  `cursive_platform_*`.
- Generated Cursive runtime symbols keep their escaped C ABI spelling, including
  `cursive_x3a_x3aruntime_*`.

The Ultraviolet runtime source tree uses the Ultraviolet style guide:

- Directories, modules, files, types, and assembly names use PascalCase with
  preserved established acronyms.
- Procedures and methods use camelCase.
- Local variables and parameters use snake_case.
- The public command assembly remains `uv`.
- The runtime library artifact spelling remains `UltravioletRT.lib` or
  `UltravioletRT.a`, as required by `def.RuntimeLibName@L5641` and
  `def.24.DefaultCallingConventionAndTargetArtifacts@L90752`.

Within Ultraviolet source, use full source names such as `Runtime`,
`RuntimeSymbol`, `RuntimeLibrary`, `StringView`, `StringManaged`, `BytesView`,
and `BytesManaged`. Use `RT` only for the spec-owned runtime library artifact
name or an external C/runtime ABI spelling.

### First Migration Object

The first migration object is W0 repository scaffold and `Ultraviolet.toml`. W0 creates the PascalCase source roots, test roots, `Docs/Audit`, `Docs/Internal`, `Docs/Audit/MigrationLedger.md`, and the initial manifest. W0 contains no compiler behavior migration.

Compiler source-object migration begins only after the readiness gates in `Execution Progression` are complete.

### Self-Hosted Backend Specification Reconciliation

The current `SPECIFICATION.md` and `Docs/Audit/UltravioletObligations.csv` still contain LLVM-specific backend obligations, including owners such as `backend.llvm-target`, `backend.llvm-codegen`, and LLVM-specific output/tool-resolution rules. Those obligations are retained in this plan for traceability only. They must be reconciled before backend implementation by a user-approved specification update that replaces LLVM-specific requirements with Ultraviolet-owned target-machine lowering, object emission, archive emission, and final-artifact production.

Implementation must not satisfy self-hosting by wrapping, embedding, shelling out to, or mechanically reproducing LLVM. The correct target is an Ultraviolet backend that lowers accepted semantic IR into target-specific object artifacts directly.

### Backend Model Decision

Ultraviolet must not model Rust's default backend contract, where the language compiler lowers to LLVM IR and an external backend owns target instruction selection, target-specific optimization, and object generation. That model is rejected for Ultraviolet because it would make LLVM IR the effective backend boundary and would prevent the self-hosted Ultraviolet compiler generations from being fully self-hosted.

Ultraviolet can still use explicit IR boundaries, monomorphization boundaries when required by generics, deterministic lowering traces, and pass-local conformance tests. Those are implementation practices, not permission to delegate the backend contract.

The selected model is self-owned instruction lowering:

1. `Compiler/Backend/IR` owns target-independent backend IR.
2. `Compiler/Backend/InstructionLowering` lowers backend IR to target-processor instructions represented in `MachineIR`.
3. `Compiler/Backend/ObjectFormat` encodes `MachineIR`, relocation records, symbols, and data into COFF/ELF object bytes.
4. `Compiler/Backend/Link` owns archives, final artifact production, runtime resolution, and linker behavior.

Textual assembly is not the canonical backend boundary. It may be added later only as a diagnostic listing or debugging output, and only if it is generated from the canonical `MachineIR` path.

### Module Obligation Responsibility Matrix

This matrix is the exact primary ownership map for the rebuild. For every module below, each listed obligation owner means the module is responsible for satisfying every exact obligation ID listed for that owner in Appendix A. No obligation owner from the current ledger is unassigned or assigned to more than one primary module.

### File-Level Obligation Map

`Docs/Audit/FileObligationMap.csv` is the authoritative file-level responsibility
map. It contains one row for each obligation row in
`Docs/Audit/UltravioletObligations.csv` and assigns that obligation to exactly
one source file. The map columns are `index`, `id`, `kind`, `phase`,
`strength`, `owner`, `internal_spec_line`, and `file`.

The current map covers 5,966 ledger rows, 5,942 unique obligation IDs, 164
obligation owners, and 107 owning files. Verification must reject any missing
ledger row, unknown owner, nonexistent mapped file, or obligation row with more
than one owning file.

### `Compiler/Core`
### File-Level Obligation Map

`Docs/Audit/FileObligationMap.csv` is the authoritative file-level responsibility
map. It contains one row for each obligation row in
`Docs/Audit/UltravioletObligations.csv` and assigns that obligation to exactly
one source file. The map columns are `index`, `id`, `kind`, `phase`,
`strength`, `owner`, `internal_spec_line`, and `file`.

The current map covers 5,966 ledger rows, 5,942 unique obligation IDs, 164
obligation owners, and 107 owning files. Verification must reject any missing
ledger row, unknown owner, nonexistent mapped file, or obligation row with more
than one owning file.

### `Compiler/Core`

Shared compiler primitives: conformance vocabulary, behavior classes, paths,
spans, and spec tracing.

File responsibility assignments:

- `Compiler/Core/ConformanceModel.uv` owns the global conformance model,
  construct vocabulary, check-kind vocabulary, outside-conformance boundary, and
  error-recovery limits: `def.Conforming@L151`, `def.WF@L166`,
  `def.ReqJudgments@L181`, `def.TypeAndStatementNodes@L274`,
  `def.ItemKind@L289`, `def.TopDeclConstructs@L311`,
  `def.TypeCtor@L325`, `def.TypeConstructs@L366`,
  `def.PermissionConstructs@L380`, `def.ExprKind@L399`,
  `def.StmtKind@L440`, `def.ExprStmtConstructs@L467`,
  `def.CapConstructs@L481`, `def.Constructs@L495`,
  `def.OutsideConformance@L740`, `def.CheckKind@L755`,
  `def.StaticCheck@L770`, `def.RuntimeCheck@L784`,
  `def.RuntimeCheckBehavior@L798`,
  `req.ResourceExhaustionOutsideConformance@L814`,
  `def.LexRecovery@L828`, `def.ParseRecovery@L843`,
  `def.TypeRecovery@L857`, `def.MaxErrorCount@L871`,
  `def.SuggestedMaxErrorCount@L885`, and `def.AbortOnErrorCount@L899`.

- `Compiler/Core/BehaviorModel.uv` owns static judgment behavior, rule
  applicability, undefined static judgments, and ill-formed rejection:
  `Reject-IllFormed@L509`, `def.StaticJudgmentSet@L544`,
  `def.StaticRuleSet@L558`, `def.RuleShape@L572`,
  `def.JudgmentSubjectAndEnvironment@L587`,
  `def.RuleSubstitutions@L602`, `def.RuleApplies@L616`,
  `def.PremisesHold@L630`, `def.IllFormed@L644`,
  `def.StaticUndefined@L658`, `def.RuleDiagnosticIdentity@L673`,
  `def.RuleSectionIndex@L689`, `Static-Undefined@L704`, and
  `Static-Undefined-NoCode@L722`.

- `Compiler/Core/SpecificationTrace.uv` owns specification organization,
  section templates, design-contract traceability, normative keyword handling,
  diagnostic-code structure, and normative-reference traceability:
  `front-matter.document-organization@L19`,
  `front-matter.feature-section-template@L77`,
  `front-matter.language-design-contract@L102`,
  `def.NormativeKeywords@L915`, `conformance.RFC2119@L930`,
  `def.DiagnosticCodeComponents@L945`,
  `def.DiagnosticCodeFormat@L962`,
  `def.DiagnosticCodeDigitParts@L976`, `refs.NormativeRefs@L993`,
  `refs.ReferenceDetails@L1015`, and `refs.Conformance@L1036`.

- `Compiler/Core/Path/Path.uv` owns the public `Path` value surface and the
  public pure path operation entry points required by `project.path-resolution`:
  `def.PathFunctionTypes@L3268`.

- `Compiler/Core/Path/Text.uv` owns path-specific text ownership states and
  byte-view access used by root classification and component discovery.

- `Compiler/Core/Path/Roots.uv` owns reusable host-path root classification,
  root tags, tails, and absoluteness required by `project.path-resolution`:
  `def.PathRootPredicates@L3146`, `def.PathRootTagAndTail@L3165`, and
  `def.AbsPath@L3253`.

- `Compiler/Core/Path/Components.uv` owns segment and component extraction
  required by `project.path-resolution`: `def.PathSegments@L3188` and
  `def.PathComponents@L3202`.

- `Compiler/Core/Path/Sequences.uv` owns the state-specific component sequence
  representation shared by `Path.uv`, `Components.uv`, and `Algebra.uv`.
  It is a supporting implementation file for the assigned path obligations; it
  has no independent specification obligation row.

- `Compiler/Core/Path/Algebra.uv` owns component-based path algebra required by
  `project.path-resolution`: `def.JoinComp@L3218`, `def.Join@L3237`,
  `def.PathPrefix@L3284`, `def.Normalize@L3298`, `def.Under@L3312`,
  `def.Canon@L3326`, `def.Drop@L3341`,
  `def.RelativePathComputation@L3355`, `def.Basename@L3369`, and
  `def.FileExt@L3386`. Any operation that synthesizes path backing data uses
  the built-in region surface directly: callers create `region as paths { ... }`
  and pass the active region alias to path construction and algebra procedures;
  sequence nodes are allocated with `paths ^ value`.

- `Compiler/Core/Spans.uv` owns source-span representation and
  source-location well-formedness required by `diagnostics.source-spans`:
  `def.SourceLocation@L1283`, `def.Span@L1298`, `def.SpanRange@L1313`,
  `WF-Location@L1327`, `WF-Span@L1345`, `def.ClampSpan@L1363`, and
  `Span-Of@L1380`.

Obligation owners:
- `conformance.behavior-types` (8 total, 8 required)
- `conformance.check-kinds` (4 total, 4 required)
- `conformance.constructs` (11 total, 11 required)
- `conformance.definitions` (3 total, 3 required)
- `conformance.document-conventions` (5 total, 5 required)
- `conformance.error-recovery` (6 total, 5 required, 1 recommended)
- `conformance.normative-references` (3 total, 2 required, 1 informative)
- `conformance.outside-conformance` (2 total, 2 required)
- `conformance.rejection` (1 total, 1 required)
- `conformance.undefinedness` (5 total, 5 required)
- `front-matter.document-organization` (1 total, 1 required)
- `front-matter.feature-section-template` (1 total, 1 required)
- `front-matter.language-design-contract` (1 total, 0 required, 1 recommended)

### `Compiler/Diagnostics`

Diagnostic codes, source association, ordering, recovery policy, emission, and rendering.

Obligation owners:
- `diagnostics.attributes` (2 total, 2 required)
- `diagnostics.attributes.layout` (1 total, 1 required)
- `diagnostics.attributes.metadata` (1 total, 1 required)
- `diagnostics.attributes.optimization` (1 total, 1 required)
- `diagnostics.code-selection` (5 total, 5 required)
- `diagnostics.emission` (6 total, 6 required)
- `diagnostics.memory` (1 total, 1 required)
- `diagnostics.name-resolution` (1 total, 1 required)
- `diagnostics.no-source-span` (1 total, 1 required)
- `diagnostics.ordering` (1 total, 1 required)
- `diagnostics.parsing` (4 total, 4 required)
- `diagnostics.permissions` (2 total, 2 required)
- `diagnostics.project` (1 total, 1 required)
- `diagnostics.records` (5 total, 5 required)
- `diagnostics.rendering` (10 total, 10 required)
- `diagnostics.source-lexical` (1 total, 1 required)
- `diagnostics.source-loading` (9 total, 9 required)
- `diagnostics.source-spans` (7 total, 7 required)
- `diagnostics.token-spans` (6 total, 6 required)
- `diagnostics.types` (2 total, 1 required)

### `Compiler/Project`

Project records, manifests, assembly loading, module discovery, target profiles, and output artifact naming.

Obligation owners:
- `conformance.target-abi` (7 total, 7 required)
- `project.assembly-graph` (16 total, 16 required)
- `project.assembly-loader` (5 total, 5 required)
- `project.assembly-ownership` (5 total, 5 required)
- `project.assembly-selection` (4 total, 4 required)
- `project.build-config` (4 total, 4 required)
- `project.context` (1 total, 1 required)
- `project.core-records` (3 total, 3 required)
- `project.deterministic-ordering` (10 total, 10 required)
- `project.loader-state-machine` (14 total, 14 required)
- `project.manifest-loader` (3 total, 3 required)
- `project.manifest-parser` (4 total, 4 required)
- `project.manifest-schema` (12 total, 12 required)
- `project.manifest-validation` (31 total, 31 required)
- `project.module-discovery` (7 total, 7 required)
- `project.output-artifacts` (43 total, 43 required)
- `project.path-resolution` (20 total, 20 required)
- `project.root-discovery` (5 total, 5 required)
- `project.source-roots` (3 total, 3 required)
- `project.toolchain-config` (6 total, 6 required)
- `project.validation-scope` (1 total, 1 required)

### `Compiler/Source/Text`

UTF-8, BOM, source loading, Unicode normalization, logical lines, and statement terminator inputs.

Obligation owners:
- `lexer.source-loading` (35 total, 35 required)
- `lexer.statement-termination` (11 total, 11 required)
- `lexer.unicode` (13 total, 13 required)
- `lexer.unicode-security` (2 total, 2 required)

### `Compiler/Source/Lexer`

Token records and lexical scanning for comments, literals, identifiers, keywords, operators, security, and tokenization.

Obligation owners:
- `lexer.character-classes` (8 total, 8 required)
- `lexer.comments` (11 total, 11 required)
- `lexer.identifiers` (6 total, 6 required)
- `lexer.literals` (53 total, 53 required)
- `lexer.maximal-munch` (9 total, 9 required)
- `lexer.operators` (3 total, 3 required)
- `lexer.records` (9 total, 9 required)
- `lexer.reserved-lexemes` (6 total, 6 required)
- `lexer.security` (11 total, 11 required)
- `lexer.tokenization` (18 total, 18 required)
- `lexer.tokens` (5 total, 5 required)

### `Compiler/Source/Ast`

AST node definitions, module/file AST forms, attributes, and AST inspection/dump helpers.

Obligation owners:
- `parser.ast` (10 total, 10 required)

### `Compiler/Source/Parser`

Parser state, token consumption, grammar helpers, lists, terminators, items, attributes, types, permissions, FFI shells, and module aggregation.

Obligation owners:
- `parser` (7 total, 7 required)
- `parser.attributes` (42 total, 42 required)
- `parser.attributes.layout` (3 total, 3 required)
- `parser.attributes.metadata` (4 total, 4 required)
- `parser.attributes.optimization` (3 total, 3 required)
- `parser.doc-comments` (5 total, 5 required)
- `parser.ffi` (1 total, 1 required)
- `parser.file` (2 total, 2 required)
- `parser.items` (2 total, 2 required)
- `parser.list-parsing` (11 total, 11 required)
- `parser.modules` (58 total, 58 required)
- `parser.permissions` (10 total, 10 required)
- `parser.phase` (5 total, 5 required)
- `parser.recovery` (12 total, 12 required)
- `parser.shared-grammar` (6 total, 6 required)
- `parser.shared-helpers` (17 total, 17 required)
- `parser.state` (11 total, 11 required)
- `parser.terminators` (8 total, 8 required)
- `parser.token-consumption` (6 total, 6 required)
- `parser.types` (16 total, 16 required)
- `spec.grammar` (18 total, 18 required)

### `Compiler/Comptime`

Compile-time dependency order, evaluation, files, derive, reflection, quotation, hygiene, and rewrites.

Obligation owners:
- `spec.comptime` (181 total, 181 required)

### `Compiler/Driver`

Compilation pipeline ordering, conformance traces, incremental fingerprints, crash reporting, source-native test discovery and execution, and test coverage reporting.

`Compiler/Driver/CLI` owns command parsing, command selection, command-specific entrypoints, command options, and version display. `Compiler/Driver/CLI/TestCommand.uv` owns CLI integration for `uv test`. `TestDiscovery.uv` owns deterministic discovery of `[[test]]` procedures. `TestHarness.uv` owns generated harness construction. `TestExecution.uv` owns invocation and result classification. `TestResults.uv` owns result records and rendering. `TestCoverage.uv` owns `covers(...)` extraction and obligation-ledger coverage checks.
Compilation pipeline ordering, conformance traces, incremental fingerprints, crash reporting, source-native test discovery and execution, and test coverage reporting.

`Compiler/Driver/CLI` owns command parsing, command selection, command-specific entrypoints, command options, and version display. `Compiler/Driver/CLI/TestCommand.uv` owns CLI integration for `uv test`. `TestDiscovery.uv` owns deterministic discovery of `[[test]]` procedures. `TestHarness.uv` owns generated harness construction. `TestExecution.uv` owns invocation and result classification. `TestResults.uv` owns result records and rendering. `TestCoverage.uv` owns `covers(...)` extraction and obligation-ledger coverage checks.

Obligation owners:
- `conformance.phase-ordering` (8 total, 8 required)
- `conformance.translation-phases` (6 total, 6 required)
- `project.command-output` (6 total, 6 required)

### `Compiler/Resolve`

Scopes, name introduction, imports, using declarations, visibility, lookup, qualified resolution, and disambiguation.

Obligation owners:
- `checker.modules` (209 total, 209 required)
- `checker.name-resolution` (307 total, 307 required)
- `checker.visibility` (15 total, 15 required)

### `Compiler/Semantics/RuntimeModel`

Static model of observable behavior, abstract-machine sequence points, and runtime state consumed by analysis and lowering.

Obligation owners:
- `abstract-machine.observable-behavior` (5 total, 5 required)
- `abstract-machine.sequence-points` (3 total, 3 required)

### `Compiler/Semantics/Capabilities`

Authority model, capability roots, no-ambient-authority checks, attenuation, and capability call-graph facts.

Obligation owners:
- `authority.attenuation` (6 total, 6 required)
- `authority.capabilities` (8 total, 7 required)
- `authority.model` (1 total, 1 required)
- `authority.no-ambient-authority` (5 total, 5 required)

### `Compiler/Semantics/Attributes`

Attribute target validation and static semantics for layout, metadata, diagnostics, optimization, and `[[test]]` attributes.

`Compiler/Semantics/Attributes/TestAttributes.uv` owns `[[test]]` target checking, argument validation, test-procedure shape checks, and test-attribute diagnostics after the user-approved specification update.
Attribute target validation and static semantics for layout, metadata, diagnostics, optimization, and `[[test]]` attributes.

`Compiler/Semantics/Attributes/TestAttributes.uv` owns `[[test]]` target checking, argument validation, test-procedure shape checks, and test-attribute diagnostics after the user-approved specification update.

Obligation owners:
- `checker.attributes` (16 total, 16 required)
- `checker.attributes.layout` (10 total, 10 required)
- `checker.attributes.metadata` (23 total, 23 required)
- `checker.attributes.optimization` (2 total, 2 required)

### `Compiler/Semantics/Types`

Type model and static semantics for primitive, aggregate, modal, class, generic, pointer, function, closure, alias, opaque, and refinement forms.

Obligation owners:
- `checker.types` (283 total, 181 required)
- `checker.types.arrays` (34 total, 34 required)
- `checker.types.primitive` (13 total, 13 required)
- `checker.types.ranges` (55 total, 55 required)
- `checker.types.slices` (28 total, 28 required)
- `checker.types.tuples` (41 total, 41 required)

### `Compiler/Semantics/Permissions`

Permission forms, binding activity state, admissibility, alias/exclusivity rules, and dynamic-context facts.

Obligation owners:
- `checker.binding-state` (72 total, 72 required)
- `checker.permission-state` (18 total, 18 required)
- `checker.permissions` (32 total, 32 required)

### `Compiler/Semantics/Memory`

Provenance, region checks, safe pointer checks, initialization state, and drop-state facts.
Provenance, region checks, safe pointer checks, initialization state, and drop-state facts.

Obligation owners:
- `checker.provenance` (51 total, 51 required)
- `checker.regions` (12 total, 12 required)

### `Compiler/Semantics/Declarations`

Type declarations, procedures, methods, contracts, invariants, modal declarations, abstraction, and polymorphism surfaces.

Obligation owners:
- `spec.abstraction-polymorphism` (328 total, 327 required)
- `spec.modal-special` (386 total, 386 required)
- `spec.procedures-contracts` (283 total, 283 required)

### `Compiler/Semantics/Expressions`

Expression typing and semantics for literals, calls, operators, closures, places, and expression-level effects.

Obligation owners:
- `spec.expressions` (478 total, 475 required)

### `Compiler/Semantics/Statements`

Statement typing and semantics for blocks, bindings, control flow, loops, defer, and sequencing.

Obligation owners:
- `spec.statements` (260 total, 260 required)

### `Compiler/Semantics/Patterns`

Pattern typing, exhaustiveness, binding introduction, and case coverage.

Obligation owners:
- `spec.patterns` (161 total, 161 required)

### `Compiler/Semantics/Keys`

Shared-key acquisition, release, conflict analysis, lifetimes, and dynamic fallback eligibility.

Obligation owners:
- `spec.key-system` (185 total, 175 required)

### `Compiler/Semantics/Parallelism`

Structured parallelism, spawn, dispatch, execution domains, and concurrency static checks.

Obligation owners:
- `spec.structured-parallelism` (181 total, 180 required)

### `Compiler/Semantics/Async`

Async modal values, await, race, sync, futures, and suspension/resumption checks.

Obligation owners:
- `spec.async` (254 total, 253 required)

### `Compiler/Semantics/FFI`

Foreign ABI validation, unsafe and hosted export surfaces, FFI type safety, and boundary restrictions.

Obligation owners:
- `authority.unsafe-ffi` (1 total, 1 required)
- `checker.ffi` (1 total, 1 required)
- `spec.ffi` (203 total, 203 required)

### `Compiler/Semantics/Initialization`

Module lifecycle, static initialization, cleanup, deinitialization, and unwind responsibilities before lowering.

Obligation owners:
- `spec.cleanup` (56 total, 56 required)
- `spec.initialization` (102 total, 102 required)

### `Compiler/Lowering`

Canonical lowering from accepted semantics into compiler IR, including attributes, permissions, checks, cleanup, bindings, expressions, statements, and patterns.

Obligation owners:
- `lowering.attributes` (2 total, 2 required)
- `lowering.attributes.layout` (1 total, 1 required)
- `lowering.attributes.metadata` (1 total, 1 required)
- `lowering.attributes.optimization` (1 total, 1 required)
- `lowering.permissions` (3 total, 3 required)
- `spec.lowering` (158 total, 158 required)

### `Compiler/Backend`

Backend-level ownership for target-independent backend rules, backend diagnostics, and final backend pipeline coordination. Current `spec.backend` obligations are LLVM-shaped in the ledger and must be reconciled in the specification before implementation; the Ultraviolet implementation target is a self-hosted backend, not LLVM.
Backend-level ownership for target-independent backend rules, backend diagnostics, and final backend pipeline coordination. Current `spec.backend` obligations are LLVM-shaped in the ledger and must be reconciled in the specification before implementation; the Ultraviolet implementation target is a self-hosted backend, not LLVM.

Obligation owners:
- `spec.backend` (190 total, 190 required)

### `Compiler/Backend/IR`

IR model, global/static symbols, runtime symbols, literal data, vtables, poison state, and IR dump/build helpers.

Obligation owners:
- `codegen` (51 total, 51 required)
- `spec.runtime-interface` (64 total, 64 required)
- `spec.symbols` (51 total, 51 required)

### `Compiler/Backend/InstructionLowering`
### `Compiler/Backend/InstructionLowering`

Lowering backend IR into target-processor instructions in `MachineIR`, including target-machine description, instruction selection, register allocation, frame layout, calling sequence construction, and target legalization. This module does not emit textual assembly or object bytes. The current ledger owner name `backend.llvm-target` is retained here only as a traceability label until the specification and obligation ledger are updated to self-hosted backend terminology.
Lowering backend IR into target-processor instructions in `MachineIR`, including target-machine description, instruction selection, register allocation, frame layout, calling sequence construction, and target legalization. This module does not emit textual assembly or object bytes. The current ledger owner name `backend.llvm-target` is retained here only as a traceability label until the specification and obligation ledger are updated to self-hosted backend terminology.

Obligation owners:
- `backend.llvm-target` (3 total, 3 required)

### `Compiler/Backend/ObjectFormat`

Relocation generation and COFF/ELF object writing. The current ledger owner name `backend.llvm-codegen` is retained here only as a traceability label until the specification and obligation ledger are updated to self-hosted backend terminology.

Obligation owners:
- `backend.llvm-codegen` (4 total, 4 required)

### `Compiler/Backend/ObjectFormat`

Relocation generation and COFF/ELF object writing. The current ledger owner name `backend.llvm-codegen` is retained here only as a traceability label until the specification and obligation ledger are updated to self-hosted backend terminology.

Obligation owners:
- `backend.llvm-codegen` (4 total, 4 required)

### `Compiler/Backend/Link`

Output pipeline, linker/archive plans, runtime library resolution, external library materialization, and artifact finalization. This module must emit final artifacts without relying on LLVM linker or archiver tools.
Output pipeline, linker/archive plans, runtime library resolution, external library materialization, and artifact finalization. This module must emit final artifacts without relying on LLVM linker or archiver tools.

Obligation owners:
- `project.linker` (31 total, 31 required)
- `project.output-pipeline` (39 total, 39 required)
- `project.tool-resolution` (7 total, 7 required)

### `Runtime/Host`

Runtime host ABI, hosted sessions, primitive dispatch, panic boundaries, startup, and runtime attribute behavior.

Obligation owners:
- `runtime` (23 total, 23 required)
- `runtime.attributes` (2 total, 2 required)
- `runtime.attributes.layout` (1 total, 1 required)
- `runtime.attributes.metadata` (1 total, 1 required)
- `runtime.attributes.optimization` (1 total, 1 required)
- `runtime.host-primitives` (7 total, 7 required)
- `runtime.primitive-method-application` (35 total, 35 required)

### `Runtime/Memory`

Runtime binding store, permission state, regions, frames, strings, bytes, async storage, and value model.

Obligation owners:
- `runtime.binding-store` (35 total, 35 required)
- `runtime.permissions` (2 total, 2 required)
- `runtime.regions` (45 total, 45 required)
- `runtime.value-model` (17 total, 17 required)

### `Runtime/IO`

Filesystem, file, directory, and path primitives.

Obligation owners:
- `runtime.filesystem-primitives` (56 total, 56 required)

### `Runtime/Network`

Network primitive relations and restrictions.

Obligation owners:
- `runtime.network-primitives` (6 total, 6 required)

### `Runtime/System`

System/process/environment/dynamic-library primitive relations.

Obligation owners:
- `runtime.system-primitives` (8 total, 8 required)

## Bootstrap Ladder

Cursive compiler path for the current workstation: `C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe`.
Cursive compiler path for the current workstation: `C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe`.

Required Cursive commands use Windows target selection explicitly:
Required Cursive commands use Windows target selection explicitly:

```sh
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 --check C:\Dev\Ultraviolet
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly UltravioletRT
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly UltravioletCompiler
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly uv
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 --check C:\Dev\Ultraviolet
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly UltravioletRT
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly UltravioletCompiler
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly uv
```

The self-host ladder is:

1. Cursive bootstrap compiler: Cursive compiles `UltravioletRT`, `UltravioletCompiler`, and `uv` from Ultraviolet source. Any Cursive-internal backend dependency is bootstrap machinery only.
2. First self-hosted compiler: the bootstrap-built `uv.exe` compiles the same assemblies using Ultraviolet's target-instruction lowering and artifact emission.
3. Fixed-point compiler: the first self-hosted `uv.exe` compiles the same assemblies again using Ultraviolet's target-instruction lowering and artifact emission.
4. Completion requires matching diagnostics, matching conformance traces, stable normalized IR, and stable artifact fingerprints between the two self-hosted compiler outputs.
1. Cursive bootstrap compiler: Cursive compiles `UltravioletRT`, `UltravioletCompiler`, and `uv` from Ultraviolet source. Any Cursive-internal backend dependency is bootstrap machinery only.
2. First self-hosted compiler: the bootstrap-built `uv.exe` compiles the same assemblies using Ultraviolet's target-instruction lowering and artifact emission.
3. Fixed-point compiler: the first self-hosted `uv.exe` compiles the same assemblies again using Ultraviolet's target-instruction lowering and artifact emission.
4. Completion requires matching diagnostics, matching conformance traces, stable normalized IR, and stable artifact fingerprints between the two self-hosted compiler outputs.

## Per-File Migration Protocol

Each migrated source object must go through this exact gate before it is accepted:

1. Identify the C++ source object(s), current tests, and current call sites.
2. Identify the canonical target Ultraviolet module and file; use the planned semantic module structure instead of the legacy numbered C++ translation-pass layout.
2. Identify the canonical target Ultraviolet module and file; use the planned semantic module structure instead of the legacy numbered C++ translation-pass layout.
3. Map the object to obligation owners and exact obligation IDs from Appendix A.
4. Read the corresponding `SPECIFICATION.md` section before writing code.
5. Implement the general rule in the canonical module; delete or avoid any duplicate behavior path.
6. Add source-native conformance tests for valid examples, invalid examples, diagnostics, phase ordering, and relevant target-profile behavior.
7. Run the Cursive `--check` command for the affected assembly and the targeted conformance test set.
6. Add source-native conformance tests for valid examples, invalid examples, diagnostics, phase ordering, and relevant target-profile behavior.
7. Run the Cursive `--check` command for the affected assembly and the targeted conformance test set.
8. Record in `Docs/Audit/MigrationLedger.md`: source object, target file, obligation IDs, tests, command output summary, and any C++ bootstrap bug found.

A file is not migrated if it merely compiles. It is migrated only when its mapped required obligations are covered by tests or an explicit, reviewed non-applicability note.

### Migration Ledger Entry Format

Every accepted migration entry in `Docs/Audit/MigrationLedger.md` must use this format:

- Source object: original C++ path, primary symbols, and current call sites.
- Target object: Ultraviolet module path, file path, and owned responsibility.
- Specification basis: exact `SPECIFICATION.md` sections read and obligation IDs from Appendix A.
- Implementation summary: canonical behavior implemented and old behavior reconciled.
- Tests: source-native test files, `[[test]]` names, covered obligation IDs, and diagnostic cases.
- Verification commands: exact command lines, target profile, assembly, and summarized output.
- Bootstrap notes: Cursive bootstrap issue found, patch reference, or `none`.
- Non-applicability notes: obligation ID, reason, spec citation, and reviewer approval.
- Acceptance: reviewer, date, and status.

## Execution Progression

The rebuild proceeds by compiler dependency order, not by the historical C++ file order. A later progression step may start only after the earlier step exposes a stable checked surface and source-native conformance tests for the obligations it owns.

1. **Scaffold and manifest**: create the clean PascalCase repository tree, `Ultraviolet.toml`, assembly roots, Docs/Audit locations, and bootstrap manifest shape. Do not add compiler behavior in this step.
2. **Cursive Ultraviolet project support**: lock normal Cursive project detection for `Ultraviolet.toml` and `.uv` files, Windows `x86_64-win64` target profile, runtime library naming, tool resolution behavior, and no-host-inference policy.
3. **Source-native conformance testing**: apply the user-approved specification update for `[[test]]`, contract-defined test success, `covers(...)` obligation metadata, and `uv test` discovery under each parent assembly's `Tests` submodule.
4. **Foundation**: implement source positions, spans, identifiers, paths, deterministic ordering primitives, diagnostic records, diagnostic rendering, and obligation trace plumbing.
5. **Project model**: implement manifest parsing, root discovery, assembly loading, assembly graph construction, module discovery, source root rules, output planning, and explicit target profile selection.
6. **Source text and lexer**: implement UTF-8/BOM/newline handling, Unicode identifier rules, Unicode security checks, comments, whitespace, literals, operators, statement termination, maximal munch, and token records.
7. **Parser and AST**: implement parser state, AST forms, item sequencing, terminators, doc association, recovery, attribute syntax, extern block parsing, file parsing, and file-to-module aggregation.
8. **Name resolution and visibility**: implement module path validation, name introduction, duplicate detection, scope lookup, qualified lookup, imports, `using` forms, accessibility, and stable symbol identity.
9. **Core type and permission semantics**: implement type well-formedness, equivalence, subtyping rules, inference, permission admissibility, binding activity, provenance, regions, and FFI-safety checks.
10. **Declarations and type constructs**: implement records, enums, unions, aliases, modal types, strings, bytes, pointers, functions, closures, generics, classes, implementations, associated types, dynamic class objects, procedures, methods, contracts, refinements, and capability classes.
11. **Executable semantics**: implement statements, expressions, patterns, key acquisition/release/conflict behavior, structured parallelism, async state machines, compile-time forms, propagation, control flow, and phase ordering.
12. **Runtime interface and lifecycle**: implement authority roots, capability attenuation, host/runtime primitives, foreign ABI validation, hosted exports, module/static initialization, cleanup, unwind/drop hooks, and runtime symbol naming.
13. **Canonical lowering**: lower accepted semantic forms into target-independent backend IR. This step must preserve checked language semantics and must not select target processor instructions.
14. **Self-owned backend**: implement backend IR, ABI lowering, `InstructionLowering`, `MachineIR`, `ObjectFormat`, and `Link` in that order. `InstructionLowering` lowers to target-processor instructions in internal `MachineIR`; `ObjectFormat` encodes COFF/ELF object bytes; `Link` owns archives and final artifacts.
15. **Driver**: implement the public `uv` command, check/build modes, assembly selection, diagnostics output, artifact layout, target selection, and build failure behavior.
16. **Self-host ladder**: the Cursive bootstrap compiler builds `UltravioletRT`, `UltravioletCompiler`, and `uv`; the first self-hosted `uv.exe` rebuilds them; the fixed-point compiler rebuilds them again. Completion requires stable diagnostics, conformance traces, normalized IR, and artifact fingerprints between the self-hosted compiler outputs.

Each progression step is accepted only when every migrated object in that step has a `Docs/Audit/MigrationLedger.md` entry, mapped obligation IDs, source-native conformance tests, and fresh command output. If a step exposes a Cursive bootstrap defect, patch Cursive at the root cause before continuing the Ultraviolet rebuild.

Compiler source-object migration begins only after the following readiness gates are complete:

- the repository scaffold and manifest exist in the PascalCase target shape;
- the `[[test]]` specification update is approved;
- the obligation ledger is regenerated from the approved specification;
- the Cursive bootstrap compiler accepts the `[[test]]` surface required for bootstrap testing;
- `uv test` ownership and harness behavior are implemented far enough to run at least one source-native compiler conformance test;
- `Docs/Audit/MigrationLedger.md` defines the required entry format for per-object migration evidence.

Before these gates are complete, allowed work is scaffold, manifest, specification reconciliation, obligation ledger regeneration, Cursive Ultraviolet project support, and source-native test-surface implementation.

### Migration Ledger Entry Format

Every accepted migration entry in `Docs/Audit/MigrationLedger.md` must use this format:

- Source object: original C++ path, primary symbols, and current call sites.
- Target object: Ultraviolet module path, file path, and owned responsibility.
- Specification basis: exact `SPECIFICATION.md` sections read and obligation IDs from Appendix A.
- Implementation summary: canonical behavior implemented and old behavior reconciled.
- Tests: source-native test files, `[[test]]` names, covered obligation IDs, and diagnostic cases.
- Verification commands: exact command lines, target profile, assembly, and summarized output.
- Bootstrap notes: Cursive bootstrap issue found, patch reference, or `none`.
- Non-applicability notes: obligation ID, reason, spec citation, and reviewer approval.
- Acceptance: reviewer, date, and status.

## Execution Progression

The rebuild proceeds by compiler dependency order, not by the historical C++ file order. A later progression step may start only after the earlier step exposes a stable checked surface and source-native conformance tests for the obligations it owns.

1. **Scaffold and manifest**: create the clean PascalCase repository tree, `Ultraviolet.toml`, assembly roots, Docs/Audit locations, and bootstrap manifest shape. Do not add compiler behavior in this step.
2. **Cursive Ultraviolet project support**: lock normal Cursive project detection for `Ultraviolet.toml` and `.uv` files, Windows `x86_64-win64` target profile, runtime library naming, tool resolution behavior, and no-host-inference policy.
3. **Source-native conformance testing**: apply the user-approved specification update for `[[test]]`, contract-defined test success, `covers(...)` obligation metadata, and `uv test` discovery under each parent assembly's `Tests` submodule.
4. **Foundation**: implement source positions, spans, identifiers, paths, deterministic ordering primitives, diagnostic records, diagnostic rendering, and obligation trace plumbing.
5. **Project model**: implement manifest parsing, root discovery, assembly loading, assembly graph construction, module discovery, source root rules, output planning, and explicit target profile selection.
6. **Source text and lexer**: implement UTF-8/BOM/newline handling, Unicode identifier rules, Unicode security checks, comments, whitespace, literals, operators, statement termination, maximal munch, and token records.
7. **Parser and AST**: implement parser state, AST forms, item sequencing, terminators, doc association, recovery, attribute syntax, extern block parsing, file parsing, and file-to-module aggregation.
8. **Name resolution and visibility**: implement module path validation, name introduction, duplicate detection, scope lookup, qualified lookup, imports, `using` forms, accessibility, and stable symbol identity.
9. **Core type and permission semantics**: implement type well-formedness, equivalence, subtyping rules, inference, permission admissibility, binding activity, provenance, regions, and FFI-safety checks.
10. **Declarations and type constructs**: implement records, enums, unions, aliases, modal types, strings, bytes, pointers, functions, closures, generics, classes, implementations, associated types, dynamic class objects, procedures, methods, contracts, refinements, and capability classes.
11. **Executable semantics**: implement statements, expressions, patterns, key acquisition/release/conflict behavior, structured parallelism, async state machines, compile-time forms, propagation, control flow, and phase ordering.
12. **Runtime interface and lifecycle**: implement authority roots, capability attenuation, host/runtime primitives, foreign ABI validation, hosted exports, module/static initialization, cleanup, unwind/drop hooks, and runtime symbol naming.
13. **Canonical lowering**: lower accepted semantic forms into target-independent backend IR. This step must preserve checked language semantics and must not select target processor instructions.
14. **Self-owned backend**: implement backend IR, ABI lowering, `InstructionLowering`, `MachineIR`, `ObjectFormat`, and `Link` in that order. `InstructionLowering` lowers to target-processor instructions in internal `MachineIR`; `ObjectFormat` encodes COFF/ELF object bytes; `Link` owns archives and final artifacts.
15. **Driver**: implement the public `uv` command, check/build modes, assembly selection, diagnostics output, artifact layout, target selection, and build failure behavior.
16. **Self-host ladder**: the Cursive bootstrap compiler builds `UltravioletRT`, `UltravioletCompiler`, and `uv`; the first self-hosted `uv.exe` rebuilds them; the fixed-point compiler rebuilds them again. Completion requires stable diagnostics, conformance traces, normalized IR, and artifact fingerprints between the self-hosted compiler outputs.

Each progression step is accepted only when every migrated object in that step has a `Docs/Audit/MigrationLedger.md` entry, mapped obligation IDs, source-native conformance tests, and fresh command output. If a step exposes a Cursive bootstrap defect, patch Cursive at the root cause before continuing the Ultraviolet rebuild.

Compiler source-object migration begins only after the following readiness gates are complete:

- the repository scaffold and manifest exist in the PascalCase target shape;
- the `[[test]]` specification update is approved;
- the obligation ledger is regenerated from the approved specification;
- the Cursive bootstrap compiler accepts the `[[test]]` surface required for bootstrap testing;
- `uv test` ownership and harness behavior are implemented far enough to run at least one source-native compiler conformance test;
- `Docs/Audit/MigrationLedger.md` defines the required entry format for per-object migration evidence.

Before these gates are complete, allowed work is scaffold, manifest, specification reconciliation, obligation ledger regeneration, Cursive Ultraviolet project support, and source-native test-surface implementation.

## Workstreams And Required Obligation Owners

Each workstream lists the exact obligation owners it must discharge. Appendix A lists the exact obligation IDs under each owner.

### W0. Repository Scaffold And Style Normalization

Create the final PascalCase repository structure, normalize existing lowercase Docs/Tools material, and introduce the bootstrap manifest without compiler logic.
Create the final PascalCase repository structure, normalize existing lowercase Docs/Tools material, and introduce the bootstrap manifest without compiler logic.

Obligation owners:
- `front-matter.language-design-contract` (1 total, 0 required, 1 recommended)
- `conformance.document-conventions` (5 total, 5 required)
- `project.core-records` (3 total, 3 required)
- `project.manifest-schema` (12 total, 12 required)
- `project.manifest-validation` (31 total, 31 required)
- `project.source-roots` (3 total, 3 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W1. Cursive Ultraviolet Project Support
### W1. Cursive Ultraviolet Project Support

Lock normal Cursive project detection for `Ultraviolet.toml` and `.uv` files, Windows target profile, runtime library name, tool resolution, and no-host-inference policy.
Lock normal Cursive project detection for `Ultraviolet.toml` and `.uv` files, Windows target profile, runtime library name, tool resolution, and no-host-inference policy.

Obligation owners:
- `conformance.translation-phases` (6 total, 6 required)
- `conformance.phase-ordering` (8 total, 8 required)
- `conformance.target-abi` (7 total, 7 required)
- `project.toolchain-config` (6 total, 6 required)
- `project.tool-resolution` (7 total, 7 required)
- `project.output-artifacts` (43 total, 43 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W2. Foundation And Diagnostics

Rebuild spans, source positions, diagnostic records, deterministic emission, rendering, and no-source diagnostics before frontend migration.

Obligation owners:
- `diagnostics.source-spans` (7 total, 7 required)
- `diagnostics.token-spans` (6 total, 6 required)
- `diagnostics.records` (5 total, 5 required)
- `diagnostics.emission` (6 total, 6 required)
- `diagnostics.code-selection` (5 total, 5 required)
- `diagnostics.ordering` (1 total, 1 required)
- `diagnostics.rendering` (10 total, 10 required)
- `diagnostics.no-source-span` (1 total, 1 required)
- `diagnostics.source-loading` (9 total, 9 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W3. Project Model And Output Planning

Rebuild manifest parsing, assembly selection, module discovery, output paths, link plans, and tool/runtime resolution exactly as the spec defines them.

Obligation owners:
- `project.manifest-parser` (4 total, 4 required)
- `project.root-discovery` (5 total, 5 required)
- `project.loader-state-machine` (14 total, 14 required)
- `project.assembly-loader` (5 total, 5 required)
- `project.assembly-selection` (4 total, 4 required)
- `project.assembly-ownership` (5 total, 5 required)
- `project.assembly-graph` (16 total, 16 required)
- `project.deterministic-ordering` (10 total, 10 required)
- `project.module-discovery` (7 total, 7 required)
- `project.command-output` (6 total, 6 required)
- `project.output-artifacts` (43 total, 43 required)
- `project.output-pipeline` (39 total, 39 required)
- `project.linker` (31 total, 31 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W4. Source Text And Lexer

Rebuild UTF-8/BOM/newline handling, NFC-sensitive identifier rules, token records, comments, whitespace, literals, identifiers, operators, security checks, and tokenization.

Obligation owners:
- `lexer.source-loading` (35 total, 35 required)
- `lexer.unicode` (13 total, 13 required)
- `lexer.unicode-security` (2 total, 2 required)
- `lexer.statement-termination` (11 total, 11 required)
- `lexer.character-classes` (8 total, 8 required)
- `lexer.reserved-lexemes` (6 total, 6 required)
- `lexer.tokens` (5 total, 5 required)
- `lexer.comments` (11 total, 11 required)
- `lexer.literals` (53 total, 53 required)
- `lexer.identifiers` (6 total, 6 required)
- `lexer.operators` (3 total, 3 required)
- `lexer.maximal-munch` (9 total, 9 required)
- `lexer.security` (11 total, 11 required)
- `lexer.tokenization` (18 total, 18 required)
- `spec.grammar` (18 total, 18 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W5. Parser, AST, Attributes, And Module Aggregation

Rebuild parser state, AST forms, item sequencing, terminators, doc association, recovery, attribute syntax, extern block parsing, and file-to-module aggregation.

Obligation owners:
- `parser.phase` (5 total, 5 required)
- `parser.ast` (10 total, 10 required)
- `parser.state` (11 total, 11 required)
- `parser.shared-grammar` (6 total, 6 required)
- `parser.shared-helpers` (17 total, 17 required)
- `parser.token-consumption` (6 total, 6 required)
- `parser.list-parsing` (11 total, 11 required)
- `parser.terminators` (8 total, 8 required)
- `parser.file` (2 total, 2 required)
- `parser.items` (2 total, 2 required)
- `parser.doc-comments` (5 total, 5 required)
- `parser.recovery` (12 total, 12 required)
- `parser.attributes` (42 total, 42 required)
- `parser.attributes.layout` (3 total, 3 required)
- `parser.attributes.metadata` (4 total, 4 required)
- `parser.attributes.optimization` (3 total, 3 required)
- `parser.modules` (58 total, 58 required)
- `parser.ffi` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W6. Authority And Runtime Model

Rebuild capability roots, no-ambient-authority guarantees, attenuation, observable behavior, sequence points, host primitives, binding store, permissions, regions, and runtime values.

Obligation owners:
- `authority.model` (1 total, 1 required)
- `authority.capabilities` (8 total, 7 required)
- `authority.no-ambient-authority` (5 total, 5 required)
- `authority.attenuation` (6 total, 6 required)
- `authority.unsafe-ffi` (1 total, 1 required)
- `abstract-machine.observable-behavior` (5 total, 5 required)
- `abstract-machine.sequence-points` (3 total, 3 required)
- `runtime.host-primitives` (7 total, 7 required)
- `runtime.filesystem-primitives` (56 total, 56 required)
- `runtime.system-primitives` (8 total, 8 required)
- `runtime.network-primitives` (6 total, 6 required)
- `runtime.primitive-method-application` (35 total, 35 required)
- `runtime.binding-store` (35 total, 35 required)
- `runtime.permissions` (2 total, 2 required)
- `runtime.regions` (45 total, 45 required)
- `runtime.value-model` (17 total, 17 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W7. Modules, Name Resolution, And Visibility

Rebuild module path validation, name introduction, duplicate detection, scope lookup, qualified resolution, imports, using forms, and accessibility.

Obligation owners:
- `checker.modules` (209 total, 209 required)
- `checker.name-resolution` (307 total, 307 required)
- `checker.visibility` (15 total, 15 required)
- `diagnostics.name-resolution` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W8. Type System, Permissions, And Core Static Semantics

Rebuild type equivalence, subtyping, inference, primitive/composite type well-formedness, permission admissibility, binding activity, provenance, regions, and FFI-safety checks.

Obligation owners:
- `checker.types` (283 total, 181 required)
- `checker.types.primitive` (13 total, 13 required)
- `checker.types.tuples` (41 total, 41 required)
- `checker.types.arrays` (34 total, 34 required)
- `checker.types.slices` (28 total, 28 required)
- `checker.types.ranges` (55 total, 55 required)
- `checker.permissions` (32 total, 32 required)
- `checker.permission-state` (18 total, 18 required)
- `checker.binding-state` (72 total, 72 required)
- `checker.provenance` (51 total, 51 required)
- `checker.regions` (12 total, 12 required)
- `checker.ffi` (1 total, 1 required)
- `diagnostics.types` (2 total, 1 required)
- `diagnostics.permissions` (2 total, 2 required)
- `diagnostics.memory` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W9. Declarations And Type Constructs

Rebuild records, enums, unions, aliases, modal types, strings, bytes, pointers, functions, closures, generics, classes, implementations, associated types, dynamic class objects, opaque/refinement/capability classes, procedures, methods, and contracts.

Obligation owners:
- `spec.modal-special` (386 total, 386 required)
- `spec.abstraction-polymorphism` (328 total, 327 required)
- `spec.procedures-contracts` (283 total, 283 required)
- `checker.attributes` (16 total, 16 required)
- `checker.attributes.layout` (10 total, 10 required)
- `checker.attributes.metadata` (23 total, 23 required)
- `checker.attributes.optimization` (2 total, 2 required)
- `diagnostics.attributes` (2 total, 2 required)
- `diagnostics.attributes.layout` (1 total, 1 required)
- `diagnostics.attributes.metadata` (1 total, 1 required)
- `diagnostics.attributes.optimization` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W10. Statements, Expressions, Patterns, Keys, Parallelism, Async, And Comptime

Rebuild all executable language constructs, pattern coverage, key acquisition/release/conflict semantics, structured parallelism, async state machines, and compile-time forms.

Obligation owners:
- `spec.statements` (260 total, 260 required)
- `spec.expressions` (478 total, 475 required)
- `spec.patterns` (161 total, 161 required)
- `spec.key-system` (185 total, 175 required)
- `spec.structured-parallelism` (181 total, 180 required)
- `spec.async` (254 total, 253 required)
- `spec.comptime` (181 total, 181 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W11. FFI, Initialization, Runtime Interface, Cleanup, And Symbols

Rebuild foreign ABI validation, hosted exports, module/global initialization, runtime interface symbols, cleanup/unwind/drop hooks, and symbol naming.

Obligation owners:
- `spec.ffi` (203 total, 203 required)
- `spec.initialization` (102 total, 102 required)
- `spec.runtime-interface` (64 total, 64 required)
- `spec.cleanup` (56 total, 56 required)
- `spec.symbols` (51 total, 51 required)
- `runtime.attributes` (2 total, 2 required)
- `runtime.attributes.layout` (1 total, 1 required)
- `runtime.attributes.metadata` (1 total, 1 required)
- `runtime.attributes.optimization` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### Backend Specification Reconciliation Gate

Backend migration begins only after the LLVM-shaped specification obligations are replaced by user-approved self-hosted backend obligations and the obligation ledger is regenerated. The reconciliation must replace LLVM-owner terminology with owner names for target-machine description, backend IR, ABI lowering, instruction lowering, object-format emission, archive emission, and final artifact production.

#### Backend Specification Approval Packet

Before W12, submit an approval request that includes:

- exact replacement text for LLVM-shaped backend sections;
- regenerated obligation-owner names and counts;
- mapping from old LLVM-named obligations to self-hosted backend obligations;
- definitions for backend IR, `MachineIR`, target-machine constants, ABI lowering, instruction lowering, object writing, archive writing, linking, and artifact fingerprinting;
- conformance tests for backend IR, instruction lowering, object bytes, archive output, linker output, and self-host fixed-point stability.

Minimum owner replacements:

- `backend.llvm-target` -> `backend.target-machine`
- `backend.llvm-codegen` -> `backend.instruction-lowering` and `backend.object-format`
- LLVM IR emission and LLVM tool-resolution obligations -> backend IR, `MachineIR`, object writer, archive writer, link, and finalization obligations

W12 acceptance uses the regenerated self-hosted backend obligations. Legacy LLVM-named rows may remain only in historical audit material.

### Backend Specification Reconciliation Gate

Backend migration begins only after the LLVM-shaped specification obligations are replaced by user-approved self-hosted backend obligations and the obligation ledger is regenerated. The reconciliation must replace LLVM-owner terminology with owner names for target-machine description, backend IR, ABI lowering, instruction lowering, object-format emission, archive emission, and final artifact production.

#### Backend Specification Approval Packet

Before W12, submit an approval request that includes:

- exact replacement text for LLVM-shaped backend sections;
- regenerated obligation-owner names and counts;
- mapping from old LLVM-named obligations to self-hosted backend obligations;
- definitions for backend IR, `MachineIR`, target-machine constants, ABI lowering, instruction lowering, object writing, archive writing, linking, and artifact fingerprinting;
- conformance tests for backend IR, instruction lowering, object bytes, archive output, linker output, and self-host fixed-point stability.

Minimum owner replacements:

- `backend.llvm-target` -> `backend.target-machine`
- `backend.llvm-codegen` -> `backend.instruction-lowering` and `backend.object-format`
- LLVM IR emission and LLVM tool-resolution obligations -> backend IR, `MachineIR`, object writer, archive writer, link, and finalization obligations

W12 acceptance uses the regenerated self-hosted backend obligations. Legacy LLVM-named rows may remain only in historical audit material.

### W12. Lowering, Backend, Driver, And Self-Host

Rebuild canonical lowering, IR, target-machine constants, ABI lowering, instruction lowering, object emission, linking, archiving, driver commands, and bootstrap-to-fixed-point self-host verification. Current LLVM-named obligation owners remain in the ledger for traceability, but implementation must satisfy the corrected self-hosted backend specification rather than LLVM behavior.
Rebuild canonical lowering, IR, target-machine constants, ABI lowering, instruction lowering, object emission, linking, archiving, driver commands, and bootstrap-to-fixed-point self-host verification. Current LLVM-named obligation owners remain in the ledger for traceability, but implementation must satisfy the corrected self-hosted backend specification rather than LLVM behavior.

Obligation owners:
- `spec.lowering` (158 total, 158 required)
- `spec.backend` (190 total, 190 required)
- `lowering.permissions` (3 total, 3 required)
- `lowering.attributes` (2 total, 2 required)
- `lowering.attributes.layout` (1 total, 1 required)
- `lowering.attributes.metadata` (1 total, 1 required)
- `lowering.attributes.optimization` (1 total, 1 required)
- `backend.llvm-target` (3 total, 3 required)
- `backend.llvm-codegen` (4 total, 4 required)
- `codegen` (51 total, 51 required)
- `project.command-output` (6 total, 6 required)
- `project.output-pipeline` (39 total, 39 required)
- `project.linker` (31 total, 31 required)
- `project.tool-resolution` (7 total, 7 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

## Source-Native Conformance Testing

The rebuild uses a specification-owned `[[test]]` procedure attribute, ordinary
procedure contracts as the test oracle, and optional `covers(...)` metadata for
audit coverage. The specification update is recorded in §9.6 and the obligation
ledger has been regenerated.

Specification obligations directly affected by the update:

- Attributes: `def.SpecAttributeRegistry@L26885`, `def.SpecAttributeTargets@L26908`, `def.AttributeStaticSemantics.Helpers2@L27012`.
- Test attributes: `grammar.TestAttribute@L28264`, `parse.TestAttributeByOrdinaryAttributeParser@L28284`, `ast.TestProcedureClassification@L28302`, `def.TestName@L28317`, `def.TestCoverage@L28332`, `req.TestAttributeProcedureTarget@L28349`, `def.TestAttributeArgsOk@L28363`, `req.TestProcedureShape@L28383`, `req.TestContextAuthority@L28405`, `conformance.TestAttributeDynamicSemantics@L28423`, `lowering.TestHarnessGeneration@L28444`, `def.TestDiscoveryOrder@L28503`, `diagnostics.TestAttributes@L28521`.
- Contracts: `grammar.15.Postconditions@L55125`, `def.15.PostconditionProofContext@L55207`, `rule.15.Post-Valid@L55222`, `req.15.ContractResultProperties@L55258`, `req.15.PostconditionResultRuntimeBinding@L55423`.
- Project model and parent assembly loading: `WF-Assembly-Kind@L2394`, `WF-Assembly@L3511`, `Select-By-Name@L3887`.

Compiler conformance tests must live in PascalCase `Tests` submodules inside the parent assembly root that owns the behavior. Test helper code lives in `Compiler/Tests/TestSupport`; parser tests live in `Compiler/Tests/Parser`; checker tests live in `Compiler/Tests/Checker`; lowering and backend tests live in `Compiler/Tests/Lowering` and `Compiler/Tests/Backend`; runtime tests live in `Runtime/Tests`; bootstrap fixed-point tests live in `Tools/Uv/Tests/Bootstrap`.

`Compiler/Tests/TestSupport/TestContext.uv` owns the compiler-rebuild test context type. The generated harness imports `UltravioletCompiler::Tests::TestSupport` and injects `TestContext` only for tests whose single parameter has that exact type. `TestContext` is a compiler test-support API for this rebuild. Parameterless `[[test]]` procedures remain valid. General user-facing test support beyond this rebuild requires a separate approved public tooling/library design.

`uv test` uses a positional target model:

```text
uv test
uv test UltravioletCompiler
uv test UltravioletCompiler::Tests::Parser
uv test Compiler/Tests/Parser/AttributeTests.uv
uv test C:\Dev\Ultraviolet
uv test C:\Dev\Ultraviolet\Compiler\Tests\Parser\AttributeTests.uv
```

Target resolution:

1. No target: discover and run every assembly that owns a `Tests` module subtree.
2. Assembly name: run that assembly's `Tests` module subtree.
3. Module path: run tests under that module subtree.
4. Source file path: run tests in that source file.
5. Directory path: find the nearest `Ultraviolet.toml`; if the directory is the project root, run all test-bearing assemblies, and if it is inside an assembly root, run tests under that directory.

Execution model:

1. Resolve the positional target into one or more test-bearing assembly scopes.
2. Load each selected assembly with ordinary project loading.
3. Discover `[[test]]` procedures under the resolved `Tests` scope.
4. Build an ephemeral test harness module under the selected assembly's build output directory.
5. Compile the selected assembly with the harness entrypoint for test execution.
6. Invoke discovered tests in deterministic order.
7. Report results and coverage through the driver.

The generated harness is build output, not source-controlled project content. Discovery order is module path, file order, declaration span, then fully-qualified procedure symbol. Each discovered test receives a stable identity from its fully-qualified procedure path; `name: "..."` is a display label.

Test result categories:

- `Passed`: the procedure returns normally and its postcondition is satisfied.
- `Failed`: the procedure returns normally and its postcondition is violated.
- `Error`: the test procedure is ill-formed, precondition evaluation fails, execution panics, the compiler crashes, harness generation fails, or required test authority cannot be provided.

Example test shape:

```uv
[[test(
    name: "unknown attribute is rejected",
    covers("AttrList-Unknown@L26976"),
    covers("diagnostics.AttributeDiagnostics@L27117")
)]]
internal procedure unknownAttributeIsRejected(context: TestContext) -> CompileResult
|: =>
    rejected(@result)
    && hasDiagnostic(@result, DiagnosticCode.E_MOD_2451)
{
    let source: SourceText = SourceText.fromLines([
        "[[unknown]]",
        "internal procedure sample() -> bool",
        "|: => @result",
        "{",
        "    return true",
        "}"
    ])

    return compileSource(context, source)
}
```

The `covers(...)` metadata is required for compiler audit tests and optional for ordinary user tests. Source text embedded in tests is the canonical representation for parser, checker, diagnostic, phase-ordering, lowering, backend, runtime, and bootstrap assertions. External test data is reserved for byte-level object, archive, linker, and fixed-point artifact comparisons where inline source cannot represent the checked artifact. Unknown `covers(...)` references fail audit coverage checking.
## Source-Native Conformance Testing

The rebuild uses a specification-owned `[[test]]` procedure attribute, ordinary
procedure contracts as the test oracle, and optional `covers(...)` metadata for
audit coverage. The specification update is recorded in §9.6 and the obligation
ledger has been regenerated.

Specification obligations directly affected by the update:

- Attributes: `def.SpecAttributeRegistry@L26885`, `def.SpecAttributeTargets@L26908`, `def.AttributeStaticSemantics.Helpers2@L27012`.
- Test attributes: `grammar.TestAttribute@L28264`, `parse.TestAttributeByOrdinaryAttributeParser@L28284`, `ast.TestProcedureClassification@L28302`, `def.TestName@L28317`, `def.TestCoverage@L28332`, `req.TestAttributeProcedureTarget@L28349`, `def.TestAttributeArgsOk@L28363`, `req.TestProcedureShape@L28383`, `req.TestContextAuthority@L28405`, `conformance.TestAttributeDynamicSemantics@L28423`, `lowering.TestHarnessGeneration@L28444`, `def.TestDiscoveryOrder@L28503`, `diagnostics.TestAttributes@L28521`.
- Contracts: `grammar.15.Postconditions@L55125`, `def.15.PostconditionProofContext@L55207`, `rule.15.Post-Valid@L55222`, `req.15.ContractResultProperties@L55258`, `req.15.PostconditionResultRuntimeBinding@L55423`.
- Project model and parent assembly loading: `WF-Assembly-Kind@L2394`, `WF-Assembly@L3511`, `Select-By-Name@L3887`.

Compiler conformance tests must live in PascalCase `Tests` submodules inside the parent assembly root that owns the behavior. Test helper code lives in `Compiler/Tests/TestSupport`; parser tests live in `Compiler/Tests/Parser`; checker tests live in `Compiler/Tests/Checker`; lowering and backend tests live in `Compiler/Tests/Lowering` and `Compiler/Tests/Backend`; runtime tests live in `Runtime/Tests`; bootstrap fixed-point tests live in `Tools/Uv/Tests/Bootstrap`.

`Compiler/Tests/TestSupport/TestContext.uv` owns the compiler-rebuild test context type. The generated harness imports `UltravioletCompiler::Tests::TestSupport` and injects `TestContext` only for tests whose single parameter has that exact type. `TestContext` is a compiler test-support API for this rebuild. Parameterless `[[test]]` procedures remain valid. General user-facing test support beyond this rebuild requires a separate approved public tooling/library design.

`uv test` uses a positional target model:

```text
uv test
uv test UltravioletCompiler
uv test UltravioletCompiler::Tests::Parser
uv test Compiler/Tests/Parser/AttributeTests.uv
uv test C:\Dev\Ultraviolet
uv test C:\Dev\Ultraviolet\Compiler\Tests\Parser\AttributeTests.uv
```

Target resolution:

1. No target: discover and run every assembly that owns a `Tests` module subtree.
2. Assembly name: run that assembly's `Tests` module subtree.
3. Module path: run tests under that module subtree.
4. Source file path: run tests in that source file.
5. Directory path: find the nearest `Ultraviolet.toml`; if the directory is the project root, run all test-bearing assemblies, and if it is inside an assembly root, run tests under that directory.

Execution model:

1. Resolve the positional target into one or more test-bearing assembly scopes.
2. Load each selected assembly with ordinary project loading.
3. Discover `[[test]]` procedures under the resolved `Tests` scope.
4. Build an ephemeral test harness module under the selected assembly's build output directory.
5. Compile the selected assembly with the harness entrypoint for test execution.
6. Invoke discovered tests in deterministic order.
7. Report results and coverage through the driver.

The generated harness is build output, not source-controlled project content. Discovery order is module path, file order, declaration span, then fully-qualified procedure symbol. Each discovered test receives a stable identity from its fully-qualified procedure path; `name: "..."` is a display label.

Test result categories:

- `Passed`: the procedure returns normally and its postcondition is satisfied.
- `Failed`: the procedure returns normally and its postcondition is violated.
- `Error`: the test procedure is ill-formed, precondition evaluation fails, execution panics, the compiler crashes, harness generation fails, or required test authority cannot be provided.

Example test shape:

```uv
[[test(
    name: "unknown attribute is rejected",
    covers("AttrList-Unknown@L26976"),
    covers("diagnostics.AttributeDiagnostics@L27117")
)]]
internal procedure unknownAttributeIsRejected(context: TestContext) -> CompileResult
|: =>
    rejected(@result)
    && hasDiagnostic(@result, DiagnosticCode.E_MOD_2451)
{
    let source: SourceText = SourceText.fromLines([
        "[[unknown]]",
        "internal procedure sample() -> bool",
        "|: => @result",
        "{",
        "    return true",
        "}"
    ])

    return compileSource(context, source)
}
```

The `covers(...)` metadata is required for compiler audit tests and optional for ordinary user tests. Source text embedded in tests is the canonical representation for parser, checker, diagnostic, phase-ordering, lowering, backend, runtime, and bootstrap assertions. External test data is reserved for byte-level object, archive, linker, and fixed-point artifact comparisons where inline source cannot represent the checked artifact. Unknown `covers(...)` references fail audit coverage checking.

## Completion Criteria

The rebuild is complete only when all of the following are true:

- The repository tree is PascalCase except externally mandated source, ABI, serialized, or command names.
- `Docs/Audit/MigrationLedger.md` contains one accepted entry for every migrated source object and every required obligation owner in Appendix A.
- The Cursive bootstrap compiler compiles the full Ultraviolet runtime, compiler library, and `uv` executable on `x86_64-win64`.
- The first self-hosted `uv.exe` recompiles itself and the runtime.
- The fixed-point compiler output is stable against the first self-hosted output by normalized diagnostics, conformance traces, IR, and artifact fingerprints.
- The self-hosted compiler generations do not invoke LLVM IR generation, LLVM libraries, `llvm-as`, `lld-link`, `llvm-lib`, or any LLVM-compatible compatibility layer.
- The Cursive bootstrap compiler compiles the full Ultraviolet runtime, compiler library, and `uv` executable on `x86_64-win64`.
- The first self-hosted `uv.exe` recompiles itself and the runtime.
- The fixed-point compiler output is stable against the first self-hosted output by normalized diagnostics, conformance traces, IR, and artifact fingerprints.
- The self-hosted compiler generations do not invoke LLVM IR generation, LLVM libraries, `llvm-as`, `lld-link`, `llvm-lib`, or any LLVM-compatible compatibility layer.
- No reachable duplicate compiler implementation, compatibility shim, fallback path, temporary branch, or test-only path remains.
- Every required obligation ID in Appendix A is implemented, tested, or explicitly classified as outside the compiler/runtime implementation boundary with a spec citation.

## Appendix A - Exact Obligation References By Construct Owner

Reference format: `obligation-id@Linternal_spec_line`. The line number is the `internal_spec_line` from the current obligation ledger, not a Markdown line in this plan. The complete source ledger currently contains 5953 obligations.

### Conformance And Specification Contract

#### `front-matter.document-organization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L19-L19.

- `front-matter.document-organization@L19`

#### `front-matter.feature-section-template`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L77-L77.

- `front-matter.feature-section-template@L77`

#### `front-matter.language-design-contract`

Count: 1 total; 0 required; 1 recommended; 0 informative. Ledger line span: L102-L102.

- `front-matter.language-design-contract@L102`

#### `conformance.definitions`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L151-L181.

- `def.Conforming@L151`, `def.WF@L166`, `def.ReqJudgments@L181`

#### `conformance.translation-phases`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L196-L527.

- `def.Phase1Order@L196`, `def.Phase2Order@L211`, `def.Phase3Checks@L226`, `def.Phase3Order@L245`, `def.Phase4Order@L259`, `def.TranslationPhases@L527`

#### `conformance.constructs`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L274-L495.

- `def.TypeAndStatementNodes@L274`, `def.ItemKind@L289`, `def.TopDeclConstructs@L311`, `def.TypeCtor@L325`, `def.TypeConstructs@L366`, `def.PermissionConstructs@L380`, `def.ExprKind@L399`, `def.StmtKind@L440`
- `def.ExprStmtConstructs@L467`, `def.CapConstructs@L481`, `def.Constructs@L495`

#### `conformance.rejection`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L509-L509.

- `Reject-IllFormed@L509`

#### `conformance.behavior-types`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L544-L644.

- `def.StaticJudgmentSet@L544`, `def.StaticRuleSet@L558`, `def.RuleShape@L572`, `def.JudgmentSubjectAndEnvironment@L587`, `def.RuleSubstitutions@L602`, `def.RuleApplies@L616`, `def.PremisesHold@L630`, `def.IllFormed@L644`

#### `conformance.undefinedness`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L658-L722.

- `def.StaticUndefined@L658`, `def.RuleDiagnosticIdentity@L673`, `def.RuleSectionIndex@L689`, `Static-Undefined@L704`, `Static-Undefined-NoCode@L722`

#### `conformance.outside-conformance`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L740-L814.

- `def.OutsideConformance@L740`, `req.ResourceExhaustionOutsideConformance@L814`

#### `conformance.check-kinds`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L755-L798.

- `def.CheckKind@L755`, `def.StaticCheck@L770`, `def.RuntimeCheck@L784`, `def.RuntimeCheckBehavior@L798`

#### `conformance.error-recovery`

Count: 6 total; 5 required; 1 recommended; 0 informative. Ledger line span: L828-L899.

- `def.LexRecovery@L828`, `def.ParseRecovery@L843`, `def.TypeRecovery@L857`, `def.MaxErrorCount@L871`, `def.SuggestedMaxErrorCount@L885`, `def.AbortOnErrorCount@L899`

#### `conformance.document-conventions`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L915-L976.

- `def.NormativeKeywords@L915`, `conformance.RFC2119@L930`, `def.DiagnosticCodeComponents@L945`, `def.DiagnosticCodeFormat@L962`, `def.DiagnosticCodeDigitParts@L976`

#### `conformance.normative-references`

Count: 3 total; 2 required; 0 recommended; 1 informative. Ledger line span: L993-L1036.

- `refs.NormativeRefs@L993`, `refs.ReferenceDetails@L1015`, `refs.Conformance@L1036`

#### `conformance.phase-ordering`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L1053-L1155.

- `def.CompileTimeTranslationPhases@L1053`, `def.ExecuteComptime@L1071`, `req.Phase1BeforePhase2@L1085`, `req.Phase2DeterministicDependencyOrder@L1099`, `req.Phase2DeclarationsBeforePhase3@L1113`, `req.Phase3UsesExpandedModuleSet@L1127`, `req.Phase4OnlyAcceptedProgram@L1141`, `conformance.ComptimeFormsChapterOwner@L1155`

#### `conformance.target-abi`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L1171-L1265.

- `def.TargetProfile@L1171`, `req.TargetProfileResolution@L1186`, `req.TargetProfileNoHostInference@L1204`, `def.TargetArch@L1218`, `def.TargetMachineModel@L1234`, `def.TargetTriple@L1249`, `conformance.LayoutAndABIOwnerSections@L1265`

### Diagnostics

#### `diagnostics.source-spans`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L1283-L1380.

- `def.SourceLocation@L1283`, `def.Span@L1298`, `def.SpanRange@L1313`, `WF-Location@L1327`, `WF-Span@L1345`, `def.ClampSpan@L1363`, `Span-Of@L1380`

#### `diagnostics.token-spans`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L1400-L1481.

- `def.TokenKind@L1400`, `No-Unknown-Ok@L1415`, `def.RawToken@L1433`, `def.Token@L1448`, `Attach-Token-Ok@L1463`, `Attach-Tokens-Ok@L1481`

#### `diagnostics.records`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L1503-L1560.

- `def.DiagnosticSeverity@L1503`, `def.DiagCodeOpt@L1518`, `def.Diagnostic@L1532`, `req.DiagnosticCodeOwnership@L1546`, `def.DiagnosticStream@L1560`

#### `diagnostics.emission`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L1575-L1656.

- `Emit-Append@L1575`, `def.DiagnosticTableColumns@L1592`, `def.EmitImplicit@L1611`, `def.EmitList@L1626`, `def.DiagnosticMessageLookup@L1641`, `def.CompileStatus@L1656`

#### `diagnostics.code-selection`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L1674-L1741.

- `def.SpecCode@L1674`, `Code@L1690`, `conformance.AppendixADiagnosticIndex@L1708`, `DiagId-Code@L1722`, `def.DiagIdCodeMapping@L1741`

#### `diagnostics.ordering`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L1758-L1758.

- `Order@L1758`

#### `diagnostics.rendering`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L1778-L1965.

- `def.DiagnosticRender@L1778`, `def.DiagnosticRenderComponents@L1794`, `def.DiagnosticRenderRich@L1820`, `def.DiagnosticRenderRichHeader@L1841`, `def.DiagnosticSourceRendering@L1857`, `def.TypeRenderLexemes@L1873`, `def.TypeRenderStateSuffixes@L1891`, `def.ParamRender@L1914`
- `def.TypeRender@L1929`, `def.ModalRefRender@L1965`

#### `diagnostics.no-source-span`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L1982-L1982.

- `NoSpan-External@L1982`

#### `diagnostics.project`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L6767-L6767.

- `diagnostics.ProjectDiagnostics@L6767`

#### `diagnostics.source-loading`

Count: 9 total; 9 required; 0 recommended; 0 informative. Ledger line span: L7832-L7961.

- `def.SourceLoadTemporarySource@L7832`, `def.SourceLoadDiagnosticOffsets@L7846`, `def.SpanAtIndex@L7861`, `def.SpanAtLineStart@L7875`, `req.LeadingBOMWarningPersistence@L7893`, `Span-BOM-Warn@L7907`, `Span-BOM-Embedded@L7925`, `Span-Prohibited@L7943`
- `NoSpan-Decode@L7961`

#### `diagnostics.source-lexical`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L10319-L10319.

- `diagnostics.SourceLexicalDiagnostics@L10319`

#### `diagnostics.parsing`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L11997-L12045.

- `def.ParsingDiagnosticRules@L11997`, `def.GenericParseRules@L12011`, `Parse-Syntax-Err@L12025`, `diagnostics.ParsingDiagnostics@L12045`

#### `diagnostics.memory`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L18366-L18366.

- `diagnostics.RuntimeStateAndMemoryDiagnostics@L18366`

#### `diagnostics.name-resolution`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L23703-L23703.

- `diagnostics.NameResolutionAndReservedNames@L23703`

#### `diagnostics.types`

Count: 2 total; 1 required; 0 recommended; 0 informative. Ledger line span: L26206-L40949.

- `diagnostics.CoreTypeDiagnostics@L26206`, `diagnostics.DataTypesSupplement@L41236`
- `diagnostics.CoreTypeDiagnostics@L26206`, `diagnostics.DataTypesSupplement@L41236`

#### `diagnostics.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27115-L27324.

- `diagnostics.AttributeDiagnostics@L27117`, `diagnostics.VendorAttributeDiagnostics@L27326`
- `diagnostics.AttributeDiagnostics@L27117`, `diagnostics.VendorAttributeDiagnostics@L27326`

#### `diagnostics.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27611-L27611.

- `diagnostics.LayoutAttributeDiagnostics@L27613`
- `diagnostics.LayoutAttributeDiagnostics@L27613`

#### `diagnostics.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27756-L27756.

- `diagnostics.OptimizationAttributeDiagnostics@L27758`
- `diagnostics.OptimizationAttributeDiagnostics@L27758`

#### `diagnostics.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28237-L28237.

- `diagnostics.DiagnosticsMetadataAttributes@L28239`
- `diagnostics.DiagnosticsMetadataAttributes@L28239`

#### `diagnostics.permissions`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L28549-L28702.

- `req.PermissionFormsDiagnosticOwnership@L28836`, `req.AliasExclusivityDiagnosticOwnership@L28989`
- `req.PermissionFormsDiagnosticOwnership@L28836`, `req.AliasExclusivityDiagnosticOwnership@L28989`

### Project, Build, Output, And Linking

#### `project.core-records`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L2004-L2037.

- `def.AssemblyAndLinkKinds@L2004`, `def.ProjectRecord@L2019`, `def.AssemblyClassification@L2037`

#### `project.validation-scope`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L2056-L2056.

- `def.ProjectValidationScope@L2056`

#### `project.command-output`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L2073-L2150.

- `def.DumpProjectOutput@L2073`, `def.ProjectSummaryOutput@L2090`, `def.OutputSummary@L2104`, `def.LinkOutputSummary@L2118`, `def.IROpt@L2134`, `def.ImportLibOpt@L2150`

#### `project.manifest-parser`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L2168-L2219.

- `def.ManifestParsing@L2168`, `Parse-Manifest-Ok@L2183`, `Parse-Manifest-Missing@L2201`, `Parse-Manifest-Err@L2219`

#### `project.manifest-loader`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L2237-L2267.

- `conformance.ManifestRequired@L2237`, `req.ManifestHostPathResolution@L2252`, `req.ManifestNoAdditionalCaseVerification@L2267`

#### `project.root-discovery`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L2281-L3491.

- `req.ProjectRootResolveInputPath@L2281`, `req.ProjectRootFileInputStartsAtParent@L2296`, `req.ProjectRootDirectoryInputStartsAtResolvedPath@L2310`, `def.FindProjectRoot@L2324`, `WF-Project-Root@L3491`

#### `project.manifest-schema`

Count: 12 total; 12 required; 0 recommended; 0 informative. Ledger line span: L2338-L2538.

- `def.ManifestSchema@L2338`, `WF-Assembly-Name@L2358`, `WF-Assembly-Name-Err@L2376`, `WF-Assembly-Kind@L2394`, `WF-Assembly-Kind-Err@L2412`, `WF-Assembly-Root-Path@L2430`, `WF-Assembly-Root-Path-Err@L2448`, `WF-Assembly-OutDir-Path@L2466`
- `WF-Assembly-OutDir-Path-Err@L2484`, `WF-Assembly-EmitIR@L2502`, `WF-Assembly-EmitIR-Err@L2520`, `def.AsmLinkKind@L2538`

#### `project.manifest-validation`

Count: 31 total; 31 required; 0 recommended; 0 informative. Ledger line span: L2555-L3723.

- `def.ManifestTableProjections@L2555`, `def.ManifestTopKeys@L2575`, `WF-TopKeys@L2589`, `WF-TopKeys-Err@L2607`, `WF-Assembly-Table@L2625`, `WF-Assembly-Table-Err@L2643`, `WF-Assembly-Count@L2661`, `WF-Assembly-Count-Err@L2679`
- `WF-Assembly-Name-Dup@L2697`, `WF-Assembly-Name-Dup-Err@L2715`, `def.ManifestAssemblyKeys@L2733`, `WF-Assembly-Keys@L2748`, `WF-Assembly-Keys-Err@L2766`, `WF-Assembly-Required-Types@L2784`, `WF-Assembly-Required-Types-Err@L2802`, `WF-Assembly-Optional-Types@L2820`
- `WF-Assembly-OutDirType-Err@L2838`, `WF-Assembly-EmitIRType@L2856`, `WF-Assembly-EmitIRType-Err@L2873`, `WF-Assembly-LinkKindType@L2891`, `WF-Assembly-LinkKindType-Err@L2908`, `WF-Assembly-LinkKind@L2926`, `WF-Assembly-LinkKind-Err@L2944`, `WF-Assembly-LinkKind-Use-Err@L2962`
- `def.ChecksAsm@L3630`, `def.BaseChecks@L3645`, `def.AsmChecks@L3659`, `def.ManifestChecks@L3675`, `def.FirstFail@L3689`, `ValidateManifest-Ok@L3705`, `ValidateManifest-Err@L3723`

#### `project.toolchain-config`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L2980-L3061.

- `def.ToolchainKeys@L2980`, `def.ToolchainTargetProfileOk@L2995`, `WF-Toolchain@L3009`, `WF-Toolchain-Err@L3027`, `def.ToolchainConfig@L3045`, `def.SelectedTargetProfile@L3061`

#### `project.build-config`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L3078-L3129.

- `def.BuildConfiguration@L3078`, `WF-Build@L3093`, `WF-Build-Err@L3111`, `def.BuildConfigDefaults@L3129`

#### `project.path-resolution`

Count: 20 total; 20 required; 0 recommended; 0 informative. Ledger line span: L3146-L3459.

- `def.PathRootPredicates@L3146`, `def.PathRootTagAndTail@L3165`, `def.PathSegments@L3188`, `def.PathComponents@L3202`, `def.JoinComp@L3218`, `def.Join@L3237`, `def.AbsPath@L3253`, `def.PathFunctionTypes@L3268`
- `def.PathPrefix@L3284`, `def.Normalize@L3298`, `def.Under@L3312`, `def.Canon@L3326`, `def.Drop@L3341`, `def.RelativePathComputation@L3355`, `def.Basename@L3369`, `def.FileExt@L3386`
- `Resolve-Canonical@L3405`, `Resolve-Canonical-Err@L3423`, `WF-RelPath@L3441`, `WF-RelPath-Err@L3459`

#### `project.context`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L3477-L3477.

- `def.ProjectContext@L3477`

#### `project.assembly-loader`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L3511-L3979.

- `WF-Assembly@L3511`, `BuildAssembly-Ok@L3925`, `BuildAssembly-Err-Resolve@L3943`, `BuildAssembly-Err-Root@L3961`, `BuildAssembly-Err-Modules@L3979`

#### `project.loader-state-machine`

Count: 14 total; 14 required; 0 recommended; 0 informative. Ledger line span: L3529-L4017.

- `def.AssemblyTarget@L3529`, `def.ProjLoadState@L3544`, `Step-Parse@L3558`, `Step-Parse-Err@L3576`, `Step-Validate@L3594`, `Step-Validate-Err@L3612`, `Step-Asm-Init@L3741`, `Step-Asm-Cons@L3759`
- `Step-Asm-Err@L3777`, `Step-Asm-Done@L3795`, `Step-Asm-Own-Err@L3813`, `Step-Asm-Done-Err@L3831`, `LoadProject-Ok@L3999`, `LoadProject-Err@L4017`

#### `project.assembly-selection`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L3851-L3905.

- `Select-Only@L3851`, `Select-Only-Exe@L3869`, `Select-By-Name@L3887`, `Select-Err@L3905`

#### `project.assembly-graph`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L4035-L4667.

- `def.AsmDeps@L4035`, `req.AssemblyGraphRejectExecutableImports@L4051`, `req.AssemblyGraphRejectLibraryCycles@L4065`, `req.DependencyAssemblyNoFinalArtifact@L4079`, `def.AssemblyProject@L4501`, `def.ModulePaths@L4515`, `def.AsmImportGraph@L4529`, `def.GraphAccessors@L4545`
- `def.GraphPathReach@L4560`, `def.NoLibraryInterior@L4575`, `def.LibraryBoundaryCycle@L4589`, `def.ImportsExecutable@L4603`, `def.HostedLibraryImportsLinkedLibrary@L4617`, `Assembly-Graph-Ok@L4631`, `Assembly-Graph-Err@L4649`, `Assembly-Graph-HostedImport-Err@L4667`

#### `project.deterministic-ordering`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L4095-L4234.

- `def.FoldPath@L4095`, `def.FileKey@L4109`, `def.FileOrdering@L4125`, `FileOrder-Rel-Fail@L4139`, `def.Fold@L4157`, `def.DirKey@L4172`, `def.DirectoryOrdering@L4188`, `def.DirSeq@L4202`
- `DirSeq-Read-Err@L4216`, `DirSeq-Rel-Fail@L4234`

#### `project.source-roots`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L4254-L4288.

- `def.SourceRootDirectories@L4254`, `WF-Source-Root@L4270`, `WF-Source-Root-Err@L4288`

#### `project.module-discovery`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L4306-L4402.

- `Module-Dir@L4306`, `def.ModuleDirectoryFiles@L4324`, `def.CompilationUnits@L4338`, `CompilationUnit-Rel-Fail@L4352`, `refs.ModuleDiscoverySection@L4370`, `Modules-Ok@L4384`, `Modules-Err@L4402`

#### `project.assembly-ownership`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L4420-L4481.

- `def.AssemblySourceRoots@L4420`, `def.OwnerRoot@L4435`, `def.OwnedModules@L4449`, `OwnAssemblies-Ok@L4463`, `WF-Assembly-Root-Owner-Ambiguous@L4481`

#### `project.output-artifacts`

Count: 43 total; 43 required; 0 recommended; 0 informative. Ledger line span: L4685-L5329.

- `def.EmitAssemblies@L4685`, `def.ImportedLibraries@L4699`, `def.LibraryPred@L4714`, `def.LibraryReady@L4728`, `def.LibraryTopo@L4742`, `def.ReadyLexLeast@L4756`, `EmitModuleList-Ok@L4770`, `def.RequiredOutputs@L4788`
- `def.IRSet@L4802`, `def.PrimaryArtifactSet@L4818`, `def.ImportLibSet@L4834`, `def.OutputRoot@L4850`, `def.OutputHygiene@L4867`, `def.OutputPathsRoot@L4882`, `def.OutputPathsDirectories@L4898`, `def.ProjectOutputBinding@L4915`
- `def.PathToPrefix@L4929`, `def.BMap@L4944`, `def.ModulePathMangle@L4960`, `def.ObjectPath@L4975`, `def.FinalArtifactLibraryName@L4989`, `def.FinalArtifactNames@L5004`, `def.ModuleEmissionPathContext@L5021`, `def.ModuleEmissionOrder@L5037`
- `def.Utf8LexLess@L5052`, `ModuleList-Ok@L5066`, `def.ArtifactPathContext@L5084`, `def.EmitIRExtension@L5100`, `def.ObjPath@L5116`, `def.IRPath@L5130`, `def.ExePath@L5144`, `def.SharedLibPath@L5160`
- `def.StaticLibPath@L5176`, `def.ImportLibPath@L5192`, `def.PrimaryArtifact@L5208`, `def.ArtifactOutputDirectoryUse@L5226`, `def.ObjPaths@L5241`, `def.IRPaths@L5255`, `def.LibraryArtifactInputs@L5269`, `def.EmitModuleIndex@L5283`
- `def.Pad4@L5299`, `def.SymbolName@L5313`, `def.Trunc8@L5329`

#### `project.output-pipeline`

Count: 39 total; 39 required; 0 recommended; 0 informative. Ledger line span: L5425-L6590.

- `def.WriteFileOk@L5425`, `def.Overwrites@L5440`, `def.EnsureDir@L5454`, `def.PathKindChecks@L5469`, `Emit-Objects-Empty@L5486`, `Emit-Objects-Cons@L5503`, `Emit-IR-None@L5523`, `Emit-IR-Cons-LL@L5541`
- `Emit-IR-Cons-BC@L5559`, `def.EmitIRFailLL@L5577`, `def.EmitIRFailBC@L5591`, `Emit-IR-Err@L5609`, `Finalize-Link@L6129`, `Finalize-Archive@L6147`, `def.OutputPipelineBigStepInputs@L6165`, `Output-Pipeline-Linkable@L6182`
- `Output-Pipeline-Dependency@L6200`, `Output-Pipeline-Err@L6218`, `def.OutputPipelineState@L6236`, `def.OutputPipelineSmallStepInputs@L6251`, `Out-Start@L6267`, `Out-Dirs-Ok@L6284`, `Out-Dirs-Err@L6302`, `Out-Obj-Collision@L6320`
- `Out-Obj-Cons@L6338`, `Out-Obj-Err@L6356`, `Out-Obj-Done@L6374`, `Out-IR-None-Finalize@L6391`, `Out-IR-None-NoFinalize@L6409`, `Out-IR-Collision@L6427`, `Out-IR-Cons-LL@L6445`, `Out-IR-Cons-BC@L6463`
- `Out-IR-Err@L6481`, `Out-IR-Done-Finalize@L6500`, `Out-IR-Done-NoFinalize@L6518`, `Out-Final-Link-Ok@L6536`, `Out-Final-Link-Err@L6554`, `Out-Final-Archive-Ok@L6572`, `Out-Final-Archive-Err@L6590`

#### `project.linker`

Count: 31 total; 31 required; 0 recommended; 0 informative. Ledger line span: L5627-L6111.

- `def.LinkJudg@L5627`, `def.RuntimeLibName@L5641`, `def.CompilerExecutableDir@L5655`, `def.CompilerSidecarLayoutPredicates@L5669`, `def.CompilerSupportRoot@L5684`, `def.CompilerRuntimeLibPath@L5701`, `def.RuntimeLibPath@L5716`, `ResolveRuntimeLib-Ok@L5732`
- `ResolveRuntimeLib-Err@L5750`, `Build-LibrariesSeq-Empty@L5768`, `Build-LibrariesSeq-Cons@L5785`, `Build-Libraries-Ok@L5803`, `def.LinkerSymbols@L5821`, `def.LinkObjs@L5837`, `def.LinkMode@L5851`, `def.LinkOutputPath@L5868`
- `def.LinkImportLibOpt@L5885`, `def.LinkInputs@L5901`, `def.ArchiveInputs@L5915`, `def.LinkFlags@L5929`, `def.ArchiveFlags@L5943`, `def.LinkArgsOk@L5957`, `def.ArchiveArgsOk@L5971`, `Link-Ok@L5985`
- `Link-NotFound@L6003`, `Link-Runtime-Missing@L6021`, `Link-Runtime-Incompatible@L6039`, `Link-Fail@L6057`, `Archive-Ok@L6075`, `Archive-NotFound@L6093`, `Archive-Fail@L6111`

#### `project.tool-resolution`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L6610-L6711.

- `def.SearchDirs@L6610`, `def.CompilerToolBinDir@L6627`, `def.ToolVersion@L6643`, `ResolveTool-Ok@L6657`, `ResolveTool-Err-Linker@L6675`, `ResolveTool-Err-Archiver@L6693`, `ResolveTool-Err-IR@L6711`

### Source Text And Lexer

#### `lexer.source-loading`

Count: 35 total; 35 required; 0 recommended; 0 informative. Ledger line span: L6810-L7812.

- `def.SourceFileRecord@L6810`, `def.SourceFileDerivedFields@L6825`, `Decode-Ok@L7003`, `Decode-Err@L7021`, `def.StripLeadBOM@L7039`, `StripBOM-Empty@L7055`, `StripBOM-None@L7072`, `StripBOM-Start@L7090`
- `StripBOM-Embedded@L7108`, `def.LineEndingConstants@L7128`, `Norm-Empty@L7143`, `Norm-CRLF@L7160`, `Norm-CR@L7178`, `Norm-LF@L7196`, `Norm-Other@L7214`, `def.Utf8Offsets@L7232`
- `def.LineStarts@L7248`, `def.LineCount@L7262`, `def.LocateLineColumn@L7276`, `def.ProhibitedCodePoints@L7297`, `def.LiteralSpan@L7311`, `WF-Prohibited@L7325`, `def.SourceLoadState@L7599`, `def.SourceLoadStateComponentDomains@L7613`
- `Step-Size@L7631`, `Step-Decode@L7648`, `Step-Decode-Err@L7666`, `Step-BOM@L7684`, `Step-Norm@L7702`, `Step-EmbeddedBOM-Err@L7720`, `Step-LineMap@L7738`, `Step-Prohibited@L7756`
- `Step-Prohibited-Err@L7774`, `LoadSource-Ok@L7794`, `LoadSource-Err@L7812`

#### `lexer.unicode`

Count: 13 total; 13 required; 0 recommended; 0 informative. Ledger line span: L6841-L7403.

- `def.UnicodeScalarDomains@L6841`, `def.Utf8Len@L6860`, `def.EncodeUTF8Scalar@L6878`, `def.EncodeUTF8Sequence@L6896`, `def.DecodeUTF8@L6911`, `def.Utf8Valid@L6925`, `def.Utf8@L6939`, `def.NormalizeOutsideIdentifiers@L6955`
- `def.NFC@L7345`, `def.CaseFold@L7359`, `def.IdentifierKeys@L7373`, `def.ModulePathKeys@L7388`, `req.NFCAndCaseFoldTotality@L7403`

#### `lexer.unicode-security`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L6972-L6987.

- `def.LexSensitivePos@L6972`, `req.LexSecureSensitivePositions@L6987`

#### `lexer.statement-termination`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L7420-L7579.

- `def.TokenizeOutputConstraints@L7420`, `def.NestingDepth@L7435`, `def.PrevNextNonNewlineTokens@L7454`, `def.NewlineContinuationTokenSets@L7471`, `def.Continue@L7489`, `def.FilterContinuedNewlines@L7503`, `def.StatementTerminatorPredicates@L7517`, `req.RangeContinuationAcrossNewline@L7537`
- `req.BraceNewlineContinuation@L7551`, `req.CommaStatementTermination@L7565`, `Missing-Terminator-Err@L7579`

#### `lexer.records`

Count: 9 total; 9 required; 0 recommended; 0 informative. Ledger line span: L7982-L8117.

- `def.LexerInput@L7982`, `def.LexerOutput@L8000`, `def.TokenEOF@L8016`, `def.LexemeBinding@L8032`, `def.DocCommentRecord@L8048`, `def.DocCommentBody@L8065`, `def.NewlineTokens@L8086`, `def.CommentTokenExclusion@L8102`
- `def.LexerIndicesAndLexemes@L8117`

#### `lexer.character-classes`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L8142-L8254.

- `def.CharacterClassInput@L8142`, `def.Whitespace@L8156`, `def.LineFeed@L8171`, `def.IdentifierCharacters@L8186`, `def.XIDVersion@L8205`, `def.NonCharacter@L8221`, `def.DigitCharacters@L8236`, `def.SensitiveCharacters@L8254`

#### `lexer.reserved-lexemes`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L8271-L8348.

- `def.ReservedKeywords@L8271`, `def.FutureReserved@L8286`, `def.KeywordPredicate@L8300`, `def.ReservedNamespaces@L8315`, `def.UniverseProtectedBindings@L8332`, `req.ReservedPredicateNames@L8348`

#### `lexer.tokens`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L8364-L10851.

- `def.TokenKinds@L8364`, `def.OperatorSet@L8378`, `def.PunctuatorSet@L8393`, `def.OperatorPunctuatorDisjoint@L8408`, `req.FixedIdentifiersTokenizedAsIdentifiers@L10851`

#### `lexer.comments`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L8424-L8594.

- `def.CommentScanInput@L8424`, `Scan-Line-Comment@L8440`, `def.DocCommentClassification@L8458`, `Doc-Comment@L8474`, `def.LineCommentSkipping@L8492`, `def.BlockCommentState@L8507`, `Block-Start@L8522`, `Block-End@L8540`
- `Block-Done@L8558`, `Block-Step@L8576`, `Block-Comment-Unterminated@L8594`

#### `lexer.literals`

Count: 53 total; 53 required; 0 recommended; 0 informative. Ledger line span: L8614-L9476.

- `def.LiteralLexingInput@L8614`, `grammar.LiteralLexing@L8630`, `conformance.FloatSuffixDefaulting@L8667`, `def.SimpleEscape@L8682`, `def.ByteEscapeValidity@L8698`, `def.UnicodeEscapeValidity@L8712`, `def.StringCharacterValidity@L8726`, `def.CharacterLiteralContentValidity@L8740`
- `def.QuotedLiteralCharacterUnits@L8754`, `def.NumericSuffixSets@L8769`, `def.StringHelpers@L8787`, `def.HexDigitValue@L8808`, `def.DecimalDigitValue@L8825`, `def.OctalDigitValue@L8839`, `def.BinaryDigitValue@L8853`, `def.NumericValues@L8867`
- `def.NumericUnderscorePredicates@L8883`, `def.NumericDigitRuns@L8902`, `def.SuffixMatch@L8920`, `def.ExponentScan@L8934`, `def.DecCoreEnd@L8954`, `req.DecimalRangeNoFloatCore@L8970`, `def.NumericCoreEnd@L8984`, `def.NumericScanEnd@L9002`
- `def.NumericFloatCorePredicates@L9016`, `def.NumericKind@L9032`, `Lex-Int@L9048`, `Lex-Float@L9066`, `def.NumericLexemeValidation@L9084`, `Lex-Numeric-Err@L9099`, `def.DecimalLeadingZeros@L9117`, `Warn-DecimalLeadingZero@L9133`
- `def.EscapeSequences@L9150`, `Lex-String@L9173`, `def.BackslashCount@L9191`, `def.StringTerminator@L9205`, `def.LineFeedOrEOFBeforeClose@L9220`, `def.EscapeMatch@L9234`, `def.BadEscapeAt@L9248`, `def.FirstBadStringEscape@L9262`
- `Lex-String-Unterminated@L9276`, `Lex-String-BadEscape@L9294`, `Lex-Char@L9312`, `def.CharacterLiteralValueRange@L9330`, `def.CharacterLiteralRepresentation@L9345`, `def.CharacterLiteralTerminator@L9361`, `def.FirstBadCharEscape@L9376`, `def.CharacterLiteralInvalid@L9390`
- `def.CharacterScalarCount@L9404`, `Lex-Char-Unterminated@L9422`, `Lex-Char-BadEscape@L9440`, `Lex-Char-Invalid@L9458`, `def.LiteralTokenizationHelpers@L9476`

#### `lexer.identifiers`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L9497-L9580.

- `def.IdentifierLexingInput@L9497`, `def.IdentifierScan@L9511`, `Lex-Identifier@L9526`, `Lex-Ident-InvalidUnicode@L9544`, `Lex-Ident-Token@L9562`, `def.KeywordClassification@L9580`

#### `lexer.operators`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L9601-L9631.

- `def.OperatorPunctuatorSets@L9601`, `def.OperatorPunctuatorMatches@L9616`, `def.OperatorPunctuatorTokenCandidates@L9631`

#### `lexer.maximal-munch`

Count: 9 total; 9 required; 0 recommended; 0 informative. Ledger line span: L9648-L9782.

- `def.MaximalMunchInput@L9648`, `def.IsQuote@L9662`, `def.TokenCandidates@L9676`, `def.LongestCandidates@L9695`, `def.KindPriority@L9709`, `def.PickLongest@L9732`, `Max-Munch@L9746`, `Max-Munch-Err@L9764`
- `def.GenericCloseException@L9782`

#### `lexer.security`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L9798-L9982.

- `def.LexicalSecurityInputs@L9798`, `def.LiteralAndCommentRanges@L9813`, `def.SensitivePositionsInSpan@L9832`, `def.UnsafeTokenSpans@L9847`, `def.LexicalSecurityCheck@L9879`, `LexSecure-Err@L9894`, `LexSecure-Warn@L9912`, `def.ConfusableIdentifierChecks@L9930`
- `Confusable-Err@L9946`, `MixedScript-Err@L9964`, `def.LexicalSecurityDiagnosticSpans@L9982`

#### `lexer.tokenization`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L9999-L10299.

- `def.LexState@L9999`, `Lex-Start@L10015`, `Lex-End@L10032`, `Lex-Whitespace@L10050`, `Lex-Newline@L10068`, `Lex-Line-Comment@L10086`, `Lex-Doc-Comment@L10104`, `Lex-Block-Comment@L10122`
- `Lex-String-Unterminated-Recover@L10140`, `Lex-Char-Unterminated-Recover@L10158`, `Lex-Sensitive@L10176`, `def.SensitiveTokenPositions@L10194`, `conformance.TupleProjectionLexicalDisambiguation@L10210`, `Lex-Token@L10225`, `Lex-Token-Err@L10243`, `Tokenize-Ok@L10263`
- `Tokenize-Secure-Err@L10281`, `Tokenize-Err@L10299`

### Parser, AST, Attributes, And Grammar

#### `parser.phase`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L10357-L10422.

- `def.ParseInputs@L10357`, `def.ParseOutputs@L10371`, `Phase1-File@L10387`, `Phase1-Forward-Refs@L10405`, `refs.FeatureParseRules@L10422`

#### `parser.ast`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L10438-L10591.

- `def.ASTNodeUniverse@L10438`, `def.ASTParseCtorDefaults@L10454`, `def.DocList@L10480`, `def.ASTItemSet@L10494`, `def.ErrorItem@L10508`, `def.TypeASTVariants@L10525`, `def.TypeASTRecordShapes@L10540`, `def.SharedDeps@L10557`
- `def.TypePermissionAndStateOptions@L10572`, `def.ParamTypeMode@L10591`

#### `parser.state`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L10609-L10777.

- `def.ParserStateRecord@L10609`, `def.ParserStateAccessors@L10624`, `def.ParserTok@L10643`, `def.ParserSourceAndEOFSpan@L10660`, `def.ParserStateOperations@L10675`, `def.ParserIndexInvariant@L10691`, `def.ParserAdvanceOrEOF@L10706`, `def.ParserSpanBetween@L10722`
- `def.SplitSpan2@L10740`, `def.SplitShiftR@L10761`, `def.ParseJudgmentSet@L10777`

#### `parser.shared-grammar`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L10794-L10898.

- `def.ParserLexemePredicates@L10794`, `def.ContextualKeywords@L10813`, `def.FixedIdentifierLexemes@L10830`, `def.UnionPropagationTokens@L10865`, `def.TypeTokenPredicates@L10881`, `refs.TrailingCommaRules@L10898`

#### `parser.shared-helpers`

Count: 17 total; 17 required; 0 recommended; 0 informative. Ledger line span: L10912-L11200.

- `Parse-Ident@L10912`, `Parse-Ident-Err@L10930`, `Parse-TypePath@L10948`, `Parse-ClassPath@L10966`, `Parse-TypePathTail-End@L10984`, `Parse-TypePathTail-Cons@L11002`, `Parse-QualifiedHead@L11020`, `Parse-Vis-Opt@L11038`
- `Parse-Vis-Default@L11056`, `Parse-ModalOpt-Yes@L11074`, `Parse-ModalOpt-No@L11092`, `Parse-AliasOpt-None@L11110`, `Parse-AliasOpt-Yes@L11128`, `Parse-TypeAnnotOpt-None@L11146`, `Parse-TypeAnnotOpt-Yes@L11164`, `Parse-KeyBoundaryOpt-Yes@L11182`
- `Parse-KeyBoundaryOpt-No@L11200`

#### `parser.token-consumption`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L11220-L11302.

- `def.TokenConsumptionState@L11220`, `def.ParseRejectRules@L11234`, `Tok-Consume-Kind@L11248`, `Tok-Consume-Keyword@L11266`, `Tok-Consume-Operator@L11284`, `Tok-Consume-Punct@L11302`

#### `parser.list-parsing`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L11320-L11472.

- `def.ListParsingState@L11320`, `List-Start@L11335`, `List-Cons@L11352`, `List-Done@L11370`, `def.ListEndSet@L11388`, `req.ListOpeningParserStateConvention@L11402`, `def.TrailingComma@L11416`, `def.TokensBetween@L11430`
- `def.TokLine@L11444`, `def.TrailingCommaAllowed@L11458`, `Trailing-Comma-Err@L11472`

#### `parser.file`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L11492-L11509.

- `def.ParseFileBigStep@L11492`, `ParseFile-Ok@L11509`

#### `parser.items`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L11529-L11547.

- `ParseItems-Empty@L11529`, `ParseItems-Cons@L11547`

#### `parser.terminators`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L11567-L11686.

- `def.StatementTerminatorTokens@L11567`, `def.StatementRequiresTerminator@L11582`, `ConsumeTerminatorOpt-Req-Yes@L11596`, `ConsumeTerminatorOpt-Req-No@L11614`, `ConsumeTerminatorOpt-Opt-Yes@L11632`, `ConsumeTerminatorOpt-Opt-No@L11650`, `ConsumeTerminatorReq-Yes@L11668`, `ConsumeTerminatorReq-No@L11686`

#### `parser.doc-comments`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L11706-L11772.

- `def.DocCommentAssociation@L11706`, `Attach-Doc-Line@L11721`, `def.LineDocTargets@L11739`, `Attach-Doc-Module@L11754`, `def.ModuleDocs@L11772`

#### `parser.recovery`

Count: 12 total; 12 required; 0 recommended; 0 informative. Ledger line span: L11790-L11980.

- `def.StatementSynchronizationSet@L11790`, `def.ItemSynchronizationSet@L11806`, `def.TypeSynchronizationSet@L11822`, `Sync-Stmt-Stop@L11836`, `Sync-Stmt-Consume@L11854`, `Sync-Stmt-Advance@L11872`, `Sync-Item-Stop@L11890`, `Sync-Item-Advance@L11908`
- `Sync-Type-Stop@L11926`, `Sync-Type-Consume@L11944`, `Sync-Type-Advance@L11962`, `def.ParseErrorRuleBindings@L11980`

#### `parser.attributes`

Count: 42 total; 42 required; 0 recommended; 0 informative. Ledger line span: L26234-L27256.

- `grammar.AttributeSyntaxAndPlacement@L26234`, `req.AttributeReservedLeafNames@L26265`, `req.AttributeImmediateTargetPlacement@L26279`, `Parse-AttrListOpt-None@L26295`, `Parse-AttrListOpt-Yes@L26313`, `Parse-AttrList-Cons@L26331`, `Parse-AttrListTail-End@L26349`, `Parse-AttrListTail-Cons@L26367`
- `Parse-AttrBlock@L26385`, `Parse-AttrSpecList-Cons@L26403`, `Parse-AttrSpecListTail-End@L26421`, `Parse-AttrSpecListTail-TrailingComma@L26439`, `Parse-AttrSpecListTail-Comma@L26457`, `Parse-AttrSpec@L26475`, `Parse-AttrArgsOpt-None@L26493`, `Parse-AttrArgsOpt-Yes@L26511`
- `Parse-AttrArgList-Cons@L26529`, `Parse-AttrArgListTail-End@L26547`, `Parse-AttrArgListTail-TrailingComma@L26565`, `Parse-AttrArgListTail-Comma@L26583`, `Parse-AttrArg-Named-Literal@L26601`, `Parse-AttrArg-Named-Ident@L26619`, `Parse-AttrArg-Named-Call@L26637`, `Parse-AttrArg-Literal@L26655`
- `Parse-AttrArg-Ident@L26673`, `def.AttributeAstRepresentation@L26694`, `def.AttributeVendorPrefixAst@L26713`, `def.AttributeArgumentAst@L26727`, `def.AttributeSpecAst@L26741`, `def.AttributeListAst@L26755`, `def.ExpressionAttributes@L26770`, `def.AttachExpressionAttributes@L26784`
- `def.ItemAttributeList@L26798`, `def.AttributeByName@L26813`, `conformance.VendorAttributeSyntaxReuse@L27137`, `req.VendorAttributeParserReuse@L27155`, `def.AttributeLeafToken@L27169`, `Parse-AttrName-Plain@L27183`, `Parse-AttrName-Vendor@L27201`, `Parse-VendorPrefixTail-End@L27219`
- `Parse-VendorPrefixTail-Cons@L27237`, `def.VendorAttributeAst@L27258`
- `def.ItemAttributeList@L26798`, `def.AttributeByName@L26813`, `conformance.VendorAttributeSyntaxReuse@L27137`, `req.VendorAttributeParserReuse@L27155`, `def.AttributeLeafToken@L27169`, `Parse-AttrName-Plain@L27183`, `Parse-AttrName-Vendor@L27201`, `Parse-VendorPrefixTail-End@L27219`
- `Parse-VendorPrefixTail-Cons@L27237`, `def.VendorAttributeAst@L27258`

#### `parser.attributes.layout`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L27344-L27383.

- `grammar.LayoutAttributeSyntax@L27346`, `req.LayoutAttributeParserReuse@L27369`, `def.LayoutAttributeAstAttachment@L27385`
- `grammar.LayoutAttributeSyntax@L27346`, `req.LayoutAttributeParserReuse@L27369`, `def.LayoutAttributeAstAttachment@L27385`

#### `parser.attributes.optimization`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L27633-L27672.

- `grammar.OptimizationAttributeSyntax@L27635`, `req.OptimizationAttributeParserReuse@L27658`, `def.OptimizationAttributeAstAttachment@L27674`
- `grammar.OptimizationAttributeSyntax@L27635`, `req.OptimizationAttributeParserReuse@L27658`, `def.OptimizationAttributeAstAttachment@L27674`

#### `parser.attributes.metadata`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L27774-L27823.

- `req.DiagnosticsMetadataSyntaxParsingAst@L27776`, `req.DiagnosticsMetadataParserReuse@L27794`, `def.ExpressionAttributeList@L27810`, `def.ExpressionAttributeByName@L27825`
- `req.DiagnosticsMetadataSyntaxParsingAst@L27776`, `req.DiagnosticsMetadataParserReuse@L27794`, `def.ExpressionAttributeList@L27810`, `def.ExpressionAttributeByName@L27825`

#### `parser.permissions`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L28262-L28752.

- `grammar.PermissionFormsSyntax@L28549`, `req.PermissionQualifierTypeGrammarPlacement@L28568`, `req.PermissionParserOwnership@L28584`, `req.ParseReceiverCanonicalOwner@L28598`, `def.PermissionAstForms@L28614`, `def.PermissionQualifiedTypeAst@L28632`, `req.AliasExclusivityNoParsingRules@L28871`, `req.AliasExclusivityNoAstForms@L28887`
- `req.BindingActivityNoParsingRules@L29023`, `req.BindingActivityNoAstNode@L29039`
- `grammar.PermissionFormsSyntax@L28549`, `req.PermissionQualifierTypeGrammarPlacement@L28568`, `req.PermissionParserOwnership@L28584`, `req.ParseReceiverCanonicalOwner@L28598`, `def.PermissionAstForms@L28614`, `def.PermissionQualifiedTypeAst@L28632`, `req.AliasExclusivityNoParsingRules@L28871`, `req.AliasExclusivityNoAstForms@L28887`
- `req.BindingActivityNoParsingRules@L29023`, `req.BindingActivityNoAstNode@L29039`

#### `parser`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L28918-L30777.

- `req.PermissionAdmissibilityNoAdditionalParsing@L29205`, `grammar.ImportDeclarationSyntax@L29442`, `req.ImportDeclarationParserBranch@L29462`, `grammar.UsingDeclarationSyntax@L29686`, `grammar.StaticDeclarationSyntax@L30196`, `req.StaticDeclParserOwnership@L30229`, `grammar.ExternBlockShellSyntax@L31064`
- `req.PermissionAdmissibilityNoAdditionalParsing@L29205`, `grammar.ImportDeclarationSyntax@L29442`, `req.ImportDeclarationParserBranch@L29462`, `grammar.UsingDeclarationSyntax@L29686`, `grammar.StaticDeclarationSyntax@L30196`, `req.StaticDeclParserOwnership@L30229`, `grammar.ExternBlockShellSyntax@L31064`

#### `parser.ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L30797-L30797.

- `req.ExternProcedureDeclSyntaxOwnedByFfiChapter@L31084`
- `req.ExternProcedureDeclSyntaxOwnedByFfiChapter@L31084`

#### `parser.modules`

Count: 58 total; 58 required; 0 recommended; 0 informative. Ledger line span: L31081-L32075.

- `grammar.ModulePathSyntax@L31368`, `req.ModuleToFileMappingNoSurfaceSyntax@L31387`, `req.ModulePathParserOwnership@L31401`, `Parse-ModulePath@L31417`, `Parse-ModulePathTail-End@L31435`, `Parse-ModulePathTail-Cons@L31453`, `def.PathAstAliases@L31471`, `def.ASTModule@L31492`
- `def.ASTFile@L31509`, `Module-Path-Root@L31545`, `Module-Path-Rel@L31563`, `Module-Path-Rel-Fail@L31581`, `def.ModuleDirOf@L31599`, `def.ProjectModuleView@L31614`, `def.SourceRootOfModule@L31632`, `def.WFModulePathJudgementSet@L31646`
- `WF-Module-Path-Ok@L31660`, `WF-Module-Path-Reserved@L31678`, `WF-Module-Path-Ident-Err@L31696`, `WF-Module-Path-Collision@L31714`, `def.ModuleAggregationInputsOutputs@L31732`, `def.ModuleMap@L31749`, `def.ASTModuleOfProjectPath@L31763`, `def.PathOfModule@L31777`
- `def.ParseModuleRuleReference@L31807`, `def.ParseModuleBigStepInputs@L31821`, `ReadBytes-Ok@L31838`, `ReadBytes-Err@L31856`, `def.BytesOfFile@L31874`, `ParseModule-Ok@L31888`, `ParseModule-Err-Read@L31906`, `ParseModule-Err-Load@L31924`
- `req.LoadSourceShortCircuit@L31942`, `ParseModule-Err-Unit@L31957`, `ParseModule-Err-Parse@L31975`, `def.ParseFileBestEffort@L31993`, `def.ParseFileOk@L32007`, `def.ParseFileDiag@L32021`, `def.HasErrorDiagnostics@L32035`, `def.ModState@L32049`
- `Mod-Start@L32064`, `Mod-Start-Err-Unit@L32082`, `Mod-Scan@L32100`, `Mod-Scan-Err-Read@L32118`, `Mod-Scan-Err-Load@L32136`, `Mod-Scan-Err-Parse@L32154`, `Mod-Done@L32172`, `def.ParseModulesBigStepInputs@L32189`
- `ParseModules-Ok@L32205`, `ParseModules-Err@L32223`, `def.DiscState@L32241`, `Disc-Start@L32255`, `Disc-Skip@L32272`, `Disc-Add@L32290`, `Disc-Collision@L32308`, `Disc-Invalid-Component@L32326`
- `Disc-Rel-Fail@L32344`, `Disc-Done@L32362`
- `grammar.ModulePathSyntax@L31368`, `req.ModuleToFileMappingNoSurfaceSyntax@L31387`, `req.ModulePathParserOwnership@L31401`, `Parse-ModulePath@L31417`, `Parse-ModulePathTail-End@L31435`, `Parse-ModulePathTail-Cons@L31453`, `def.PathAstAliases@L31471`, `def.ASTModule@L31492`
- `def.ASTFile@L31509`, `Module-Path-Root@L31545`, `Module-Path-Rel@L31563`, `Module-Path-Rel-Fail@L31581`, `def.ModuleDirOf@L31599`, `def.ProjectModuleView@L31614`, `def.SourceRootOfModule@L31632`, `def.WFModulePathJudgementSet@L31646`
- `WF-Module-Path-Ok@L31660`, `WF-Module-Path-Reserved@L31678`, `WF-Module-Path-Ident-Err@L31696`, `WF-Module-Path-Collision@L31714`, `def.ModuleAggregationInputsOutputs@L31732`, `def.ModuleMap@L31749`, `def.ASTModuleOfProjectPath@L31763`, `def.PathOfModule@L31777`
- `def.ParseModuleRuleReference@L31807`, `def.ParseModuleBigStepInputs@L31821`, `ReadBytes-Ok@L31838`, `ReadBytes-Err@L31856`, `def.BytesOfFile@L31874`, `ParseModule-Ok@L31888`, `ParseModule-Err-Read@L31906`, `ParseModule-Err-Load@L31924`
- `req.LoadSourceShortCircuit@L31942`, `ParseModule-Err-Unit@L31957`, `ParseModule-Err-Parse@L31975`, `def.ParseFileBestEffort@L31993`, `def.ParseFileOk@L32007`, `def.ParseFileDiag@L32021`, `def.HasErrorDiagnostics@L32035`, `def.ModState@L32049`
- `Mod-Start@L32064`, `Mod-Start-Err-Unit@L32082`, `Mod-Scan@L32100`, `Mod-Scan-Err-Read@L32118`, `Mod-Scan-Err-Load@L32136`, `Mod-Scan-Err-Parse@L32154`, `Mod-Done@L32172`, `def.ParseModulesBigStepInputs@L32189`
- `ParseModules-Ok@L32205`, `ParseModules-Err@L32223`, `def.DiscState@L32241`, `Disc-Start@L32255`, `Disc-Skip@L32272`, `Disc-Add@L32290`, `Disc-Collision@L32308`, `Disc-Invalid-Component@L32326`
- `Disc-Rel-Fail@L32344`, `Disc-Done@L32362`

#### `parser.types`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L34302-L40557.

- `grammar.PrimitiveTypeSyntax@L34589`, `def.PrimLexemeSet@L34616`, `grammar.TupleSyntax@L34930`, `req.TupleSingletonCommaIllFormed@L34954`, `def.TupleScanDepthAndStep@L35066`, `def.TupleScanPredicates@L35082`, `grammar.ArraySyntax@L35793`, `grammar.SliceSyntax@L36491`
- `grammar.RangeSyntax@L37060`, `req.RangeTypeParserOwnership@L37084`, `grammar.RecordSyntax@L38137`, `grammar.EnumSyntax@L39112`, `req.EnumVariantSeparatorSyntax@L39137`, `req.EnumTopLevelCommaSeparatorRejected@L39369`, `grammar.UnionTypeSyntax@L40222`, `grammar.TypeAliasSyntax@L40844`
- `grammar.PrimitiveTypeSyntax@L34589`, `def.PrimLexemeSet@L34616`, `grammar.TupleSyntax@L34930`, `req.TupleSingletonCommaIllFormed@L34954`, `def.TupleScanDepthAndStep@L35066`, `def.TupleScanPredicates@L35082`, `grammar.ArraySyntax@L35793`, `grammar.SliceSyntax@L36491`
- `grammar.RangeSyntax@L37060`, `req.RangeTypeParserOwnership@L37084`, `grammar.RecordSyntax@L38137`, `grammar.EnumSyntax@L39112`, `req.EnumVariantSeparatorSyntax@L39137`, `req.EnumTopLevelCommaSeparatorRejected@L39369`, `grammar.UnionTypeSyntax@L40222`, `grammar.TypeAliasSyntax@L40844`

#### `spec.grammar`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L98736-L99400.

- `grammar.B.1.LexicalGrammar@L99024`, `grammar.B.2.TypeGrammar@L99074`, `req.B.2.ClosureTypeUnionParameterParentheses@L99134`, `grammar.B.2.GenericRefinementModalTypeGrammar@L99147`, `grammar.B.3.ExpressionGrammar@L99180`, `req.B.3.ClosureExprUnionParameterParentheses@L99258`, `grammar.B.3.ControlAndSpecialExpressionGrammar@L99271`, `grammar.B.4.PatternGrammar@L99305`
- `grammar.B.5.StatementGrammar@L99335`, `grammar.B.6.DeclarationGrammar@L99372`, `grammar.B.7.ContractGrammar@L99456`, `grammar.B.8.AttributeGrammar@L99483`, `grammar.B.9.KeySystemGrammar@L99539`, `grammar.B.10.ConcurrencyGrammar@L99569`, `grammar.B.11.AsyncGrammar@L99603`, `grammar.B.12.MetaprogrammingGrammar@L99632`
- `grammar.B.13.FFIGrammar@L99665`, `grammar.B.14.RegionGrammar@L99691`
- `grammar.B.1.LexicalGrammar@L99024`, `grammar.B.2.TypeGrammar@L99074`, `req.B.2.ClosureTypeUnionParameterParentheses@L99134`, `grammar.B.2.GenericRefinementModalTypeGrammar@L99147`, `grammar.B.3.ExpressionGrammar@L99180`, `req.B.3.ClosureExprUnionParameterParentheses@L99258`, `grammar.B.3.ControlAndSpecialExpressionGrammar@L99271`, `grammar.B.4.PatternGrammar@L99305`
- `grammar.B.5.StatementGrammar@L99335`, `grammar.B.6.DeclarationGrammar@L99372`, `grammar.B.7.ContractGrammar@L99456`, `grammar.B.8.AttributeGrammar@L99483`, `grammar.B.9.KeySystemGrammar@L99539`, `grammar.B.10.ConcurrencyGrammar@L99569`, `grammar.B.11.AsyncGrammar@L99603`, `grammar.B.12.MetaprogrammingGrammar@L99632`
- `grammar.B.13.FFIGrammar@L99665`, `grammar.B.14.RegionGrammar@L99691`

### Authority, Abstract Machine, And Runtime State

#### `authority.model`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L12070-L12070.

- `conformance.NoAmbientAuthorityDiscipline@L12070`

#### `authority.capabilities`

Count: 8 total; 7 required; 0 recommended; 0 informative. Ledger line span: L12086-L12193.

- `def.CapToken@L12086`, `def.CapInTypeSignature@L12100`, `def.CapInTypeRootMappings@L12114`, `def.CapInTypePermission@L12134`, `def.CapInTypeStructuralCollections@L12148`, `def.CapInTypeStructuralNominal@L12164`, `req.CapInTypeLeastFixedPointAllowed@L12179`, `req.CapInTypeCyclesTerminate@L12193`

#### `authority.no-ambient-authority`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L12209-L12270.

- `NAA-1@L12209`, `NAA-2@L12224`, `NAA-3@L12239`, `def.CapReq@L12256`, `req.DirectCallCapabilityInclusion@L12270`

#### `authority.attenuation`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L12286-L12365.

- `def.AttenuationOperations@L12286`, `req.AttenuationMonotoneAuthority@L12307`, `req.AttenuationChildLiveness@L12321`, `req.AttenuationChildDropDoesNotDiminishParent@L12337`, `req.AttenuationParentDropRejectedWithLiveChildren@L12351`, `req.AttenuationDelegationSubsetRouting@L12365`

#### `abstract-machine.observable-behavior`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L12381-L12454.

- `def.ObservableEffect@L12381`, `def.ObservableEvent@L12401`, `def.ObservableBehavior@L12420`, `req.AsIfObservablePreservation@L12434`, `req.AsIfForbiddenObservableChanges@L12454`

#### `abstract-machine.sequence-points`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L12475-L12513.

- `def.SequencePoint@L12475`, `def.CanonicalSequencePoints@L12492`, `req.SequencePointLTRPreservation@L12513`

#### `authority.unsafe-ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L12529-L12529.

- `conformance.UnsafeForeignAuthority@L12529`

#### `runtime.host-primitives`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L12545-L12636.

- `def.FilesystemHostPrimitiveSets@L12545`, `def.NonFilesystemHostPrimitiveSets@L12561`, `def.HostPrimitiveUniverse@L12579`, `def.DiagnosticHostPrimitives@L12593`, `def.RuntimeHostPrimitives@L12607`, `def.HostPrimitiveMappingAndFailure@L12621`, `req.UnmappedHostPrimitiveFailureIllFormed@L12636`

#### `runtime.filesystem-primitives`

Count: 56 total; 56 required; 0 recommended; 0 informative. Ledger line span: L12652-L13561.

- `conformance.FilesystemPrimitiveOwnership@L12652`, `def.FilesystemPrimitiveJudgments@L12666`, `def.FilesystemPrimitiveResultTypes@L12680`, `def.FilesystemStateTypes@L12716`, `def.FilesystemStateProjections@L12732`, `def.FilesystemEntryQueries@L12750`, `def.FileHandleQueries@L12770`, `def.DirectoryIteratorQueries@L12795`
- `def.FileFlushedQuery@L12821`, `def.FilesystemExplicitStateJudgments@L12835`, `def.FilesystemStateElidedEquivalences@L12851`, `def.RestrictPathSuccess@L12886`, `def.RestrictPathFailure@L12900`, `def.FilesystemPathOperationSet@L12914`, `FSRestrict-Delegate@L12928`, `FSRestrict-InvalidPath@L12943`
- `FSRestrict-ExistsInvalidFalse@L12958`, `def.FilesystemPathErrorOperationSets@L12973`, `def.FilesystemPathErrorPredicates@L12989`, `FSPath-InvalidPath-NoData@L13007`, `FSPath-InvalidPath-Data@L13022`, `FSPath-NotFound@L13037`, `FSPath-PermissionDenied@L13052`, `FSPath-CreateWriteAlreadyExists@L13067`
- `FSPath-CreateDirAlreadyExistsNonDir@L13082`, `FSPath-OpenDirInvalidNonDir@L13097`, `FSPath-Busy@L13112`, `FSPath-OtherFailure@L13127`, `FSReadFile-InvalidUtf8@L13142`, `FileReadAll-InvalidUtf8@L13157`, `FSExists-True@L13172`, `FSExists-False@L13187`
- `def.FileHandleStateAndMode@L13202`, `def.FileLengthAndByteLength@L13221`, `def.DirectoryEntryOrdering@L13241`, `FSOpenRead-HandleState@L13261`, `FSOpenWrite-HandleState@L13276`, `FSOpenAppend-HandleState@L13291`, `FSCreateWrite-HandleState@L13306`, `FSReadFile-DelegatesThroughHandle@L13321`
- `FSReadBytes-DelegatesThroughHandle@L13336`, `FileReadAll-ClosedFailure@L13351`, `FileReadAllBytes-ClosedFailure@L13366`, `FileWrite-ClosedFailure@L13381`, `FileFlush-ClosedFailure@L13396`, `FileReadAll-AdvancesToEnd@L13411`, `FileReadAllBytes-AdvancesToEnd@L13426`, `FileWrite-PositionUpdate@L13441`
- `FileWrite-LengthUpdate@L13456`, `FileFlush-RecordsFlushed@L13471`, `FileClose-ClosesHandle@L13486`, `FSOpenDir-IteratorState@L13501`, `DirNext-ClosedFailure@L13516`, `DirNext-Exhausted@L13531`, `DirNext-YieldsEntry@L13546`, `DirClose-ClosesIterator@L13561`

#### `runtime.system-primitives`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L13578-L13694.

- `def.SystemState@L13578`, `def.SystemPrimitiveJudgments@L13595`, `def.SystemPrimitiveStateElidedEquivalences@L13610`, `def.EmptyStringVal@L13626`, `System-GetEnv-Ok@L13640`, `System-GetEnv-None@L13658`, `System-Exit@L13676`, `System-Run@L13694`

#### `runtime.network-primitives`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L13714-L13785.

- `def.NetworkPrimitiveJudgments@L13714`, `req.NetRestrictHostRequiredSemantics@L13728`, `req.NetRestrictHostSubsetAuthority@L13742`, `req.NetRestrictHostEqualityEnforcement@L13757`, `req.NetRestrictRejectBeforeObservableEffect@L13771`, `req.NetRestrictNoUnrelatedMutation@L13785`

#### `runtime.primitive-method-application`

Count: 35 total; 35 required; 0 recommended; 0 informative. Ledger line span: L13801-L14407.

- `def.PrimitiveReceiverHandleHelpers@L13801`, `def.PrimitiveMethodNameAndOwner@L13816`, `def.PrimCallJudgment@L13835`, `Prim-FS-OpenRead@L13849`, `Prim-FS-OpenWrite@L13867`, `Prim-FS-OpenAppend@L13885`, `Prim-FS-CreateWrite@L13903`, `Prim-FS-ReadFile@L13921`
- `Prim-FS-ReadBytes@L13939`, `Prim-FS-WriteFile@L13957`, `Prim-FS-WriteStdout@L13975`, `Prim-FS-WriteStderr@L13993`, `Prim-FS-Exists@L14011`, `Prim-FS-Remove@L14029`, `Prim-FS-OpenDir@L14047`, `Prim-FS-CreateDir@L14065`
- `Prim-FS-EnsureDir@L14083`, `Prim-FS-Kind@L14101`, `Prim-FS-Restrict@L14119`, `Prim-File-ReadAll@L14137`, `Prim-File-ReadAllBytes@L14155`, `Prim-File-Write@L14173`, `Prim-File-Flush@L14191`, `Prim-File-Write-Append@L14209`
- `Prim-File-Flush-Append@L14227`, `Prim-File-Close-Read@L14245`, `Prim-File-Close-Write@L14263`, `Prim-File-Close-Append@L14281`, `Prim-Dir-Next@L14299`, `Prim-Dir-Close@L14317`, `Prim-System-GetEnv@L14335`, `Prim-System-Exit@L14353`
- `Prim-System-Run@L14371`, `Prim-Network-RestrictHost@L14389`, `req.PrimSystemExitAbortOutcome@L14407`

#### `runtime.binding-store`

Count: 35 total; 35 required; 0 recommended; 0 informative. Ledger line span: L16858-L17386.

- `def.ScopeEntry@L16858`, `def.DynamicScopeStack@L16877`, `def.UpdateScopeStack@L16895`, `def.RuntimeScopePushPop@L16909`, `def.AppendCleanup@L16924`, `def.CleanupList@L16938`, `def.ScopeById@L16952`, `def.ReplaceScopeById@L16969`
- `def.SetCleanupList@L16986`, `def.PoisonedModule@L17000`, `req.HostedSessionPoisonFlagLocalization@L17014`, `def.PoisonedModules@L17028`, `def.RuntimeBindingIdentityAndValue@L17042`, `def.FreshBindId@L17057`, `def.Last@L17071`, `def.NearestScope@L17086`
- `def.LookupBind@L17103`, `def.RuntimeBindingValueLookup@L17117`, `def.RuntimeBindingStateLookup@L17131`, `LookupVal-Bind-Value@L17145`, `LookupVal-Bind-Alias@L17163`, `LookupVal-Path@L17181`, `LookupValPath-Builtin@L17199`, `LookupValPath-Static@L17217`
- `LookupValPath-Proc@L17235`, `LookupValPath-RecordCtor@L17253`, `def.ScopeValueAndStateUpdate@L17271`, `def.UpdateRuntimeBindingValue@L17286`, `def.SetRuntimeBindingState@L17300`, `def.RuntimeBindingTypeAndInfo@L17314`, `def.BindRuntimeValue@L17329`, `def.BindPatternValue@L17343`
- `def.PatternBindingOrder@L17357`, `def.BindRuntimeList@L17371`, `def.BindPattern@L17386`

#### `runtime.regions`

Count: 45 total; 45 required; 0 recommended; 0 informative. Ledger line span: L17402-L18064.

- `def.RuntimeRegionEntry@L17402`, `def.RuntimeAddressTags@L17420`, `def.RuntimeRegionStack@L17435`, `def.RegionArena@L17449`, `def.UpdateRegionArena@L17464`, `def.ArenaNew@L17478`, `def.FreshRuntimeAddress@L17492`, `def.Prefix@L17506`
- `def.ArenaAppend@L17520`, `def.ArenaMark@L17534`, `def.ArenaResetTo@L17548`, `def.ArenaClear@L17562`, `def.ArenaRemove@L17576`, `def.RegionValue@L17590`, `def.ResolveRuntimeRegionEntry@L17605`, `def.ActiveRuntimeRegion@L17622`
- `def.ResolveRuntimeRegionTargetAndTag@L17637`, `def.FreshRuntimeRegionTagAndArena@L17652`, `def.UpdateRegionStack@L17667`, `def.RegionNew@L17681`, `def.RegionOpen@L17695`, `def.FrameEnter@L17709`, `def.BindRegionAlias@L17723`, `def.TagAddr@L17738`
- `def.TagAddrFrom@L17752`, `def.RegionAlloc@L17766`, `def.FreshRuntimeRegionTags@L17780`, `def.RetagRegions@L17794`, `def.RegionReset@L17811`, `def.PopRegions@L17825`, `def.RegionFree@L17842`, `def.FrameMark@L17856`
- `def.PopRegionScope@L17870`, `def.ReleaseArena@L17887`, `def.ResetArena@L17901`, `req.RegionRuntimeOwnershipBoundary@L17915`, `req.RegionReleaseCleanupBeforeArenaReclaim@L17931`, `req.ArenaReclaimNoDrop@L17946`, `def.RegionProcedureJudgements@L17960`, `Region-New-Scoped@L17974`
- `Region-Alloc-Proc@L17992`, `Region-Reset-Proc@L18010`, `Region-Freeze-Proc@L18028`, `Region-Thaw-Proc@L18046`, `Region-Free-Proc@L18064`

#### `runtime.value-model`

Count: 17 total; 17 required; 0 recommended; 0 informative. Ledger line span: L18082-L18347.

- `def.BlockEnter@L18082`, `def.ScalarRuntimeValues@L18096`, `def.PointerRuntimeValues@L18118`, `def.AggregateRuntimeValues@L18132`, `def.RuntimeValueDomain@L18155`, `def.TupleValueOperations@L18169`, `def.RecordFieldValueOperations@L18184`, `def.IndexAndSliceValueOperations@L18199`
- `def.AddressPrimitiveJudgments@L18217`, `def.AddressArithmetic@L18231`, `def.ElementType@L18245`, `def.AggregateAddressCalculation@L18259`, `def.PointerStateAndAddress@L18278`, `def.BindingAddresses@L18297`, `def.RuntimeAddressTagLookup@L18315`, `def.RuntimeTagActive@L18332`
- `def.DynamicAddressState@L18347`

#### `runtime.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27083-L27292.

- `conformance.AttributeDynamicSemantics@L27085`, `conformance.VendorAttributeDynamicSemantics@L27294`
- `conformance.AttributeDynamicSemantics@L27085`, `conformance.VendorAttributeDynamicSemantics@L27294`

#### `runtime.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27579-L27579.

- `conformance.LayoutAttributeDynamicSemantics@L27581`
- `conformance.LayoutAttributeDynamicSemantics@L27581`

#### `runtime.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27724-L27724.

- `conformance.OptimizationAttributeDynamicSemantics@L27726`
- `conformance.OptimizationAttributeDynamicSemantics@L27726`

#### `runtime.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28203-L28203.

- `conformance.DiagnosticsMetadataDynamicSemantics@L28205`
- `conformance.DiagnosticsMetadataDynamicSemantics@L28205`

#### `runtime.permissions`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L28451-L28670.

- `conformance.PermissionDynamicSemantics@L28738`, `conformance.AliasExclusivityDynamicSemantics@L28957`
- `conformance.PermissionDynamicSemantics@L28738`, `conformance.AliasExclusivityDynamicSemantics@L28957`

#### `runtime`

Count: 23 total; 23 required; 0 recommended; 0 informative. Ledger line span: L29083-L40526.

- `conformance.PermissionAdmissibilityRuntimeIdentity@L29370`, `conformance.ImportDeclarationDynamicSemantics@L29619`, `conformance.UsingDeclarationDynamicSemantics@L30126`, `conformance.StaticDeclarationDynamicSemantics@L30419`, `req.HostedLibraryStaticAddrSessionLocal@L30713`, `conformance.ExternBlockDynamicSemantics@L31317`, `conformance.ModuleAggregationDynamicSemantics@L34514`, `def.PrimitiveValueTypes@L34840`
- `req.PrimitiveOperationEvaluationOwnership@L34863`, `def.TupleValueType@L35544`, `def.ArrayValueType@L36265`, `def.ArrayIndexRuntimeHelpers@L36281`, `def.SliceValueType@L36875`, `def.SliceRuntimeIndexHelpers@L36891`, `def.SliceIndexUpdate@L37009`, `def.RangeValueTypes@L37534`
- `def.SliceBoundsRaw@L37623`, `def.SliceBounds@L37642`, `def.RecordValueType@L38731`, `def.RecordDefaultInits@L38783`, `def.EnumValueType@L39906`, `def.UnionCase@L40464`, `def.UnionValueType@L40813`
- `conformance.PermissionAdmissibilityRuntimeIdentity@L29370`, `conformance.ImportDeclarationDynamicSemantics@L29619`, `conformance.UsingDeclarationDynamicSemantics@L30126`, `conformance.StaticDeclarationDynamicSemantics@L30419`, `req.HostedLibraryStaticAddrSessionLocal@L30713`, `conformance.ExternBlockDynamicSemantics@L31317`, `conformance.ModuleAggregationDynamicSemantics@L34514`, `def.PrimitiveValueTypes@L34840`
- `req.PrimitiveOperationEvaluationOwnership@L34863`, `def.TupleValueType@L35544`, `def.ArrayValueType@L36265`, `def.ArrayIndexRuntimeHelpers@L36281`, `def.SliceValueType@L36875`, `def.SliceRuntimeIndexHelpers@L36891`, `def.SliceIndexUpdate@L37009`, `def.RangeValueTypes@L37534`
- `def.SliceBoundsRaw@L37623`, `def.SliceBounds@L37642`, `def.RecordValueType@L38731`, `def.RecordDefaultInits@L38783`, `def.EnumValueType@L39906`, `def.UnionCase@L40464`, `def.UnionValueType@L40813`

### Modules, Name Resolution, And Visibility

#### `checker.name-resolution`

Count: 307 total; 307 required; 0 recommended; 0 informative. Ledger line span: L18399-L23682.

- `def.ScopeKeyConstraint@L18399`, `def.GlobalResolutionTables@L18414`, `def.ResolutionContext@L18431`, `def.ResolutionEntity@L18449`, `def.ResolutionScope@L18467`, `def.ResolutionScopeStack@L18481`, `def.UniverseBindings@L18499`, `def.BytePrefix@L18514`
- `def.ReservedIdentifiers@L18529`, `def.ReservedModulePath@L18545`, `def.BuiltinTypeNameSets@L18561`, `req.PredicateNameUniverseReservation@L18577`, `def.NameResolutionKeywordKeys@L18591`, `def.ScopeDomain@L18611`, `def.NameIntroductionScopeSequence@L18625`, `def.InScope@L18639`
- `def.InOuter@L18653`, `Intro-Ok@L18667`, `Intro-Dup@L18685`, `Intro-Outer-Err@L18703`, `Intro-Reserved-Gen-Err@L18721`, `Intro-Reserved-Ultraviolet-Err@L18739`, `req.IntroRulePriority@L18757`, `def.UsingAlias@L18773`
- `Using-Alias-Ok@L18789`, `Using-Alias-Unresolved@L18807`, `Using-Alias-Dup@L18825`, `Using-Alias-Reserved@L18843`, `req.UsingAliasRulePriority@L18861`, `def.ModuleNameValidationHelpers@L18875`, `Validate-Module-Ok@L18889`, `Validate-Module-Keyword-Err@L18907`
- `req.UniverseScopeReuseHandledByIntro@L18925`, `def.LookupScopeSequence@L18941`, `def.LookupNearestScopeIndex@L18955`, `Lookup-Unqualified@L18969`, `Lookup-Unqualified-None@L18987`, `def.EntityKindPredicates@L19005`, `def.RegionAliasName@L19023`, `Resolve-Value-Name@L19037`
- `Resolve-Type-Name@L19055`, `Resolve-Class-Name@L19073`, `Resolve-Module-Name@L19091`, `def.QualifiedResolutionProjectInput@L19109`, `def.QualifiedResolutionCurrentModule@L19123`, `def.QualifiedResolutionVisibleModules@L19137`, `def.QualifiedResolutionAliasMap@L19152`, `def.QualifiedResolutionKindDomain@L19166`
- `req.ModuleVisibilityJudgementOwnership@L19180`, `req.ResolveModulePathCanonicalOwner@L19194`, `Resolve-Qualified@L19208`, `prop.CollectNamesOrderIndependence@L19464`, `def.BindingKindDomain@L19478`, `def.BindingSourceDomain@L19492`, `def.NameInfoShape@L19506`, `def.ModuleNameMap@L19520`
- `def.AliasMap@L19534`, `def.UsingMap@L19548`, `def.UsingValueMap@L19562`, `def.UsingTypeMap@L19576`, `def.TypeMap@L19590`, `def.ClassMap@L19604`, `PatNames-IdentifierPattern@L19618`, `PatNames-WildcardPattern@L19633`
- `PatNames-LiteralPattern@L19648`, `PatNames-TuplePattern@L19663`, `PatNames-RecordFieldPattern@L19680`, `PatNames-RecordFieldShorthand@L19697`, `PatNames-RecordPattern@L19712`, `PatNames-EnumNoPayload@L19729`, `PatNames-EnumTuplePayload@L19744`, `PatNames-EnumRecordPayload@L19761`
- `PatNames-RangePattern@L19778`, `def.AllModuleNames@L19795`, `def.VisibleModuleNames@L19809`, `def.LastPathComponent@L19823`, `def.IsModulePath@L19837`, `def.SplitLastPathComponent@L19851`, `def.ModuleByPath@L32624`, `def.ItemNames@L32652`
- `def.UsingSpecName@L29917`, `def.UsingSpecNames@L29933`, `DeclNames-Empty@L19923`, `DeclNames-Using@L19938`, `DeclNames-Item@L19956`, `def.ModuleDeclNames@L19974`, `req.ImportUsingJudgementCanonicalOwner@L19988`, `Bind-Procedure@L20002`
- `Bind-ExternBlock@L31245`, `Bind-Record@L38443`, `Bind-Enum@L39452`, `Bind-Class@L20071`, `Bind-TypeAlias@L40918`, `Bind-Static@L30311`, `Bind-Import@L29566`, `Bind-Import-Err@L29584`
- `Bind-Using@L30073`, `Bind-Using-Err@L30091`, `Bind-ErrorItem@L20195`, `Collect-Ok@L20212`, `Collect-Scan@L20230`, `Collect-Using-Import-Dup@L20248`, `Collect-Dup@L20266`, `Collect-Err@L20284`
- `PatNames-RangePattern@L19778`, `def.AllModuleNames@L19795`, `def.VisibleModuleNames@L19809`, `def.LastPathComponent@L19823`, `def.IsModulePath@L19837`, `def.SplitLastPathComponent@L19851`, `def.ModuleByPath@L32624`, `def.ItemNames@L32652`
- `def.UsingSpecName@L29917`, `def.UsingSpecNames@L29933`, `DeclNames-Empty@L19923`, `DeclNames-Using@L19938`, `DeclNames-Item@L19956`, `def.ModuleDeclNames@L19974`, `req.ImportUsingJudgementCanonicalOwner@L19988`, `Bind-Procedure@L20002`
- `Bind-ExternBlock@L31245`, `Bind-Record@L38443`, `Bind-Enum@L39452`, `Bind-Class@L20071`, `Bind-TypeAlias@L40918`, `Bind-Static@L30311`, `Bind-Import@L29566`, `Bind-Import-Err@L29584`
- `Bind-Using@L30073`, `Bind-Using-Err@L30091`, `Bind-ErrorItem@L20195`, `Collect-Ok@L20212`, `Collect-Scan@L20230`, `Collect-Using-Import-Dup@L20248`, `Collect-Dup@L20266`, `Collect-Err@L20284`
- `def.BindingNameSet@L20302`, `def.NoDuplicateBindingNames@L20316`, `def.DisjointBindingNames@L20330`, `def.NameMapUnion@L20344`, `def.NameInfoOfBinding@L20358`, `def.BindingNameSource@L20372`, `def.NameMapSource@L20386`, `def.UsingImportConflict@L20400`
- `def.NameCollectionStateDomain@L20414`, `Names-Start@L20428`, `Names-Step@L20445`, `Names-Step-Using-Import-Dup@L20463`, `Names-Step-Dup@L20481`, `Names-Step-Err@L20499`, `Names-Done@L20517`, `def.ResolveQualifiedFormSignature@L20536`
- `def.ResolveArgsSignature@L20550`, `def.ResolveFieldInitsSignature@L20564`, `def.ResolveRecordPathSignature@L20578`, `def.ResolveEnumUnitSignature@L20592`, `def.ResolveEnumTupleSignature@L20606`, `def.ResolveEnumRecordSignature@L20620`, `def.ResolvePathJudgementSet@L20634`, `ResolveArgs-Empty@L20648`
- `ResolveArgs-Cons@L20665`, `ResolveFieldInits-Empty@L20683`, `ResolveFieldInits-Cons@L20700`, `Resolve-RecordPath@L38460`, `Resolve-EnumUnit@L39487`, `Resolve-EnumTuple@L39505`, `Resolve-EnumRecord@L39523`, `def.BuiltinValuePath@L20790`
- `ResolveQual-Name-Builtin@L20804`, `ResolveQual-Name-Value@L20822`, `ResolveQual-Name-Record@L38478`, `ResolveQual-Name-Enum@L39541`, `ResolveQual-Name-Err@L20876`, `def.SharedResolutionProjectInput@L20897`, `def.SharedResolutionCurrentModule@L20911`, `def.SharedResolutionAstModule@L20925`
- `ResolveArgs-Cons@L20665`, `ResolveFieldInits-Empty@L20683`, `ResolveFieldInits-Cons@L20700`, `Resolve-RecordPath@L38460`, `Resolve-EnumUnit@L39487`, `Resolve-EnumTuple@L39505`, `Resolve-EnumRecord@L39523`, `def.BuiltinValuePath@L20790`
- `ResolveQual-Name-Builtin@L20804`, `ResolveQual-Name-Value@L20822`, `ResolveQual-Name-Record@L38478`, `ResolveQual-Name-Enum@L39541`, `ResolveQual-Name-Err@L20876`, `def.SharedResolutionProjectInput@L20897`, `def.SharedResolutionCurrentModule@L20911`, `def.SharedResolutionAstModule@L20925`
- `def.SharedResolutionInputs@L20939`, `def.SharedResolutionOutputs@L20953`, `def.PathOfModuleReference@L20967`, `def.TypeParamBindings@L20981`, `ResolveGenericParamsOpt-None@L20996`, `ResolvePredicateClauseOpt-None@L21011`, `ResolveContractClauseOpt-None@L21026`, `ResolveInvariantOpt-None@L21041`
- `ResolveTypeOpt-None@L21056`, `ResolveExprOpt-None-Judgement@L21071`, `def.ResolveExprOptNone@L21086`, `def.ResolveExprOptSome@L21100`, `ResolveGenericParamsOpt-Yes@L21114`, `ResolveTypeParam@L21132`, `ResolveTypeParamList-Empty@L21150`, `ResolveTypeParamList-Cons@L21165`
- `ResolvePredicateClauseOpt-Yes@L21183`, `ResolvePredicateReqList-Empty@L21201`, `ResolvePredicateReq-Predicate@L21216`, `ResolvePredicateReqList-Cons@L21234`, `ResolveContractClauseOpt-Yes@L21252`, `ResolveInvariantOpt-Yes@L21270`, `ResolveTypePath-Ident@L21288`, `ResolveTypePath-Ident-Local@L21306`
- `ResolveTypePath-Qual@L21324`, `def.LocalTypePath@L21342`, `ResolveClassPath-Ident@L21356`, `ResolveClassPath-Qual@L21374`, `ResolveType-Path@L21392`, `ResolveType-Dynamic@L21410`, `ResolveType-Apply@L21428`, `ResolveType-ModalState@L21446`
- `def.ResolveModalRef@L21464`, `ResolveType-Hom@L21480`, `ResolveTypeList-Empty@L21498`, `ResolveTypeList-Cons@L21513`, `ResolveParam@L21531`, `ResolveParams-Empty@L21549`, `ResolveParams-Cons@L21564`, `def.ResolvePatternSignature@L21583`
- `ResolvePat-Wildcard@L21597`, `ResolvePat-Identifier@L21612`, `ResolvePat-Literal@L21627`, `ResolvePat-Tuple@L21642`, `ResolvePat-Record@L21660`, `ResolvePat-Enum@L21678`, `ResolvePat-Modal@L21696`, `ResolvePat-Range@L21714`
- `ResolvePatternList-Empty@L21732`, `ResolveFieldPatternList-Empty@L21747`, `ResolvePatternList-Cons@L21762`, `ResolveFieldPattern-Implicit@L21780`, `ResolveFieldPattern-Explicit@L21797`, `ResolveFieldPatternList-Cons@L21815`, `ResolveEnumPayloadPattern-None@L21833`, `ResolveEnumPayloadPattern-Tuple@L21848`
- `ResolveEnumPayloadPattern-Record@L21866`, `ResolveFieldPatternListOpt-None@L21884`, `ResolveFieldPatternListOpt-Some@L21899`, `ResolveExpr-Ident@L21918`, `ResolveExpr-Ident-Err@L21936`, `ResolveExpr-Qualified@L21954`, `def.ResolveArgsReference@L21972`, `def.ResolveFieldInitsReference@L21986`
- `ResolveExprList-Empty@L22000`, `ResolveExprList-Cons@L22015`, `def.ResolveExprListJudgementSet@L22033`, `def.ResolveEnumPayloadJudgementSet@L22047`, `ResolveEnumPayload-None@L22061`, `ResolveEnumPayload-Tuple@L22078`, `ResolveEnumPayload-Record@L22096`, `def.ResolveKeyPathJudgementSet@L22114`
- `ResolveKeySeg-Field@L22128`, `ResolveKeySeg-Index@L22146`, `ResolveKeySegs-Empty@L22164`, `ResolveKeySegs-Cons@L22181`, `ResolveKeyPathExpr@L22199`, `ResolveKeyPathExpr-Err@L22217`, `ResolveKeyPathList-Empty@L22235`, `ResolveKeyPathList-Cons@L22252`
- `def.ResolveParallelOptJudgementSet@L22270`, `ResolveParallelOpt-Cancel@L22284`, `ResolveParallelOpt-Name@L22302`, `ResolveParallelOpts-Empty@L22319`, `ResolveParallelOpts-Cons@L22336`, `def.ResolveSpawnOptJudgementSet@L22354`, `ResolveSpawnOpt-Name@L22368`, `ResolveSpawnOpt-Affinity@L22385`
- `ResolveSpawnOpt-Priority@L22403`, `ResolveSpawnOpts-Empty@L22421`, `ResolveSpawnOpts-Cons@L22438`, `def.ResolveDispatchOptJudgementSet@L22456`, `ResolveDispatchOpt-Reduce@L22470`, `ResolveDispatchOpt-Ordered@L22487`, `ResolveDispatchOpt-Chunk@L22504`, `ResolveDispatchOpts-Empty@L22522`
- `ResolveDispatchOpts-Cons@L22539`, `def.ResolveRaceJudgementSet@L22557`, `ResolveRaceHandler-Return@L22571`, `ResolveRaceHandler-Yield@L22589`, `ResolveRaceArm@L22607`, `ResolveRaceArms-Empty@L22625`, `ResolveRaceArms-Cons@L22642`, `def.ResolveAllExprListJudgementSet@L22660`
- `ResolveAllExprList-Empty@L22674`, `ResolveAllExprList-Cons@L22691`, `def.ResolveCalleeJudgementSet@L22709`, `ResolveCallee-Ident-Value@L22723`, `ResolveCallee-Ident-Record@L22741`, `ResolveCallee-Path-Value@L22759`, `ResolveCallee-Path-Builtin@L22777`, `ResolveCallee-Path-Record@L22795`
- `ResolveCallee-Other@L22813`, `ResolveExpr-Call@L22831`, `ResolveExpr-Call-TypeArgs@L22849`, `ResolveExpr-RecordExpr@L22868`, `ResolveExpr-EnumLiteral@L22886`, `def.ResolveIfCaseJudgementSet@L22904`, `ResolveIfCase@L22918`, `ResolveIfCases-Empty@L22936`
- `ResolveIfCases-Cons@L22953`, `ResolveElseBlockOpt-None@L22971`, `ResolveElseBlockOpt-Some@L22988`, `ResolveExpr-IfIs@L23006`, `ResolveExpr-IfCase@L23024`, `ResolveExpr-LoopInfinite@L23042`, `ResolveExpr-LoopConditional@L23060`, `ResolveExpr-LoopIter@L23078`
- `ResolveExpr-Parallel@L23096`, `ResolveExpr-Spawn@L23114`, `ResolveExpr-Wait@L23132`, `def.ResolveKeyClauseJudgementSet@L23150`, `ResolveKeyClauseOpt-None@L23164`, `ResolveKeyClauseOpt-Yes@L23182`, `ResolveExpr-Dispatch@L23200`, `ResolveExpr-Yield@L23218`
- `ResolveExpr-YieldFrom@L23236`, `ResolveExpr-Sync@L23254`, `ResolveExpr-Race@L23272`, `ResolveExpr-All@L23290`, `ResolveExpr-Alloc-Explicit-ByAlias@L23308`, `def.ResolveExprRuleSet@L23326`, `def.NoSpecificResolveExpr@L23340`, `ResolveExpr-Hom@L23354`
- `ResolveExpr-Alloc-Implicit@L23372`, `ResolveExpr-Alloc-Explicit@L23390`, `def.ResolveStmtSeqJudgementSet@L23408`, `ResolveStmtSeq-Empty@L23422`, `ResolveStmtSeq-Cons@L23439`, `ResolveExpr-Block@L23457`, `Validate-ModulePath-Ok@L23476`, `Validate-ModulePath-Reserved-Err@L23494`
- `req.ResolveItemFeatureOwnership@L23512`, `ResolveModule-Ok@L23526`, `ResolveItems-Empty@L23544`, `ResolveItems-Cons@L23559`, `def.ResolutionStateDomain@L23577`, `Res-Start@L23591`, `Res-Names@L23608`, `Res-Items@L23626`
- `ResolveModules-Ok@L23646`, `ResolveModules-Err-Parse@L23664`, `ResolveModules-Err-Resolve@L23682`

#### `checker.visibility`

Count: 15 total; 15 required; 0 recommended; 0 informative. Ledger line span: L19228-L19444.

- `def.DeclOfModuleItem@L19228`, `def.DeclOfExternProc@L19242`, `def.ModuleOfItem@L19256`, `def.ModuleOfExternProc@L19270`, `def.ExternBlockOfProc@L19284`, `def.ExternProcName@L19298`, `def.VisibilityOfDeclaration@L19312`, `def.SameAssembly@L19326`
- `Access-Public@L19340`, `Access-Internal@L19358`, `Access-Private@L19376`, `Access-Internal-Err@L19394`, `Access-Err@L19412`, `def.TopLevelDeclarationPredicate@L19430`, `TopLevelVis-Ok@L19444`

#### `checker.modules`

Count: 209 total; 209 required; 0 recommended; 0 informative. Ledger line span: L29191-L34273.

- `Parse-Import@L29478`, `def.ImportDeclAst@L29496`, `req.ImportDeclarationBindingSemanticsScope@L29514`, `Import-Path@L29530`, `Import-Path-Err@L29548`, `Bind-Import@L29566`, `Bind-Import-Err@L29584`, `ResolveItem-Import@L29602`
- `diagnostics.ImportDeclarations@L29651`, `req.ImportDeclarationDiagnosticOwnership@L29669`, `Parse-Using-Wildcard@L29713`, `Parse-Using-List@L29731`, `Parse-Using-Item@L29749`, `Parse-UsingSpec@L29767`, `Parse-UsingList-Empty@L29785`, `Parse-UsingList-Cons@L29803`
- `Parse-UsingListTail-End@L29821`, `Parse-UsingListTail-TrailingComma@L29839`, `Parse-UsingListTail-Comma@L29857`, `def.UsingDeclAst@L29875`, `req.UsingDeclarationBindingSemanticsScope@L29901`, `def.UsingSpecName@L29917`, `def.UsingSpecNames@L29933`, `Using-Item@L29947`
- `Using-Item-Public-Err@L29965`, `Using-List@L29983`, `Using-Wildcard-Warn@L30001`, `Using-Wildcard@L30019`, `Using-List-Dup@L30037`, `Using-List-Public-Err@L30055`, `Bind-Using@L30073`, `Bind-Using-Err@L30091`
- `ResolveItem-Using@L30109`, `diagnostics.UsingDeclarations@L30158`, `req.UsingDeclarationDiagnosticOwnership@L30179`, `def.StaticDeclTopLevelItems@L30215`, `Parse-Static-Decl@L30245`, `def.StaticDeclAst@L30263`, `req.StaticDeclModuleScopeBindingSemantics@L30281`, `def.StaticVisOk@L30297`
- `Bind-Static@L30311`, `WF-StaticDecl@L30329`, `WF-StaticDecl-Ann-Mismatch@L30347`, `WF-StaticDecl-MissingType@L30365`, `StaticVisOk-Err@L30383`, `ResolveItem-Static@L30401`, `def.StaticBindTypes@L30481`, `def.StaticBindList@L30495`
- `Emit-Static-Const@L30539`, `Emit-Static-Init@L30557`, `Emit-Static-Multi@L30575`, `InitFn@L30607`, `DeinitFn@L30639`, `def.StaticItems@L30657`, `def.StaticItemOf@L30671`, `def.StaticType@L30741`
- `def.StaticBindInfo@L30755`, `Lower-StaticInit-Item@L30799`, `Lower-StaticInitItems-Empty@L30817`, `Lower-StaticInitItems-Cons@L30834`, `Lower-StaticInit@L30852`, `InitCallIR@L30870`, `Lower-StaticDeinitNames-Empty@L30903`, `Lower-StaticDeinitNames-Cons-Resp@L30920`
- `Lower-StaticDeinitNames-Cons-NoResp@L30938`, `Lower-StaticDeinit-Item@L30956`, `Lower-StaticDeinitItems-Empty@L30974`, `Lower-StaticDeinitItems-Cons@L30991`, `Lower-StaticDeinit@L31009`, `diagnostics.StaticDeclarations@L31027`, `req.StaticDeclarationDiagnosticOwnership@L31047`, `Parse-ExternBlock@L31100`
- `Parse-ExternAbiOpt-None@L31118`, `Parse-ExternAbiOpt-String@L31136`, `Parse-ExternAbiOpt-Ident@L31154`, `Parse-ExternItemList-End@L31172`, `Parse-ExternItemList-Cons@L31190`, `def.ExternBlockAst@L31208`, `req.ExternBlockStaticSemanticsScope@L31229`, `Bind-ExternBlock@L31245`
- `WF-ExternBlock@L31263`, `ExternAbi-Unknown-Err@L31281`, `ResolveItem-ExternBlock@L31299`, `req.ModuleAggregationStaticSemanticsScope@L31529`, `def.NameCollectAfterParse@L31791`, `def.QualifiedLookupContext@L32380`, `def.ModuleAssemblyPathHelpers@L32397`, `def.ImportDeclarationsOfModule@L32413`
- `def.VisibleModulesAndNames@L32428`, `def.ModulePathPrefix@L32450`, `AliasExpand-None@L32466`, `AliasExpand-Yes@L32484`, `def.CurrentAsmPath@L32502`, `ModulePrefix-Direct@L32516`, `ModulePrefix-Current@L32534`, `ModulePrefix-None@L32552`
- `Resolve-ModulePath-Direct@L32570`, `Resolve-ModulePath-Current@L32588`, `ResolveModulePath-Err@L32606`, `def.ModuleByPath@L32624`, `def.ModuleOfPath@L32638`, `def.ItemNames@L32652`, `ItemOfPath@L32666`, `ItemOfPath-None@L32684`
- `def.ImportCoveragePredicates@L32702`, `def.ImportOkJudgementSet@L32717`, `Import-Ok-Local@L32731`, `Import-Ok-Covered@L32749`, `Import-Ok-Err@L32767`, `Resolve-Import-Direct@L32785`, `Resolve-Import-Current@L32803`, `Resolve-Import-Err@L32821`
- `Resolve-Using-Ok@L32839`, `Resolve-Using-Err@L32857`, `req.ResolvedItemAccessibilityOwnedByVisibilityChapter@L32875`, `def.ModuleInitializationDependencyEnvironment@L32890`, `Reachable-Edge@L32906`, `Reachable-Step@L32924`, `def.ModuleInitializationPathHelpers@L32942`, `def.TypeRefsJudgementSet@L32958`
- `def.TypeReferenceEnvironmentAliases@L32972`, `TypeRef-Path@L32988`, `TypeRef-Using@L33006`, `TypeRef-Path-Local@L33024`, `TypeRef-Dynamic@L33042`, `TypeRef-ModalState@L33060`, `TypeRef-Apply@L33078`, `TypeRef-Perm@L33096`
- `TypeRef-Prim@L33114`, `TypeRef-Tuple@L33131`, `TypeRef-Array@L33149`, `TypeRef-Slice@L33167`, `TypeRef-Union@L33185`, `TypeRef-Func@L33203`, `TypeRef-String@L33221`, `TypeRef-Bytes@L33238`
- `TypeRef-Ptr@L33255`, `TypeRef-RawPtr@L33273`, `TypeRef-Range@L33291`, `TypeRef-RangeInclusive@L33309`, `TypeRef-RangeFrom@L33327`, `TypeRef-RangeTo@L33345`, `TypeRef-RangeToInclusive@L33363`, `TypeRef-RangeFull@L33381`
- `TypeRef-Ref-Path@L33398`, `TypeRef-Ref-Apply@L33416`, `TypeRef-Ref-ModalState@L33434`, `TypeRef-RecordExpr@L33452`, `TypeRef-EnumLiteral@L33470`, `TypeRef-QualBrace@L33488`, `TypeRef-Cast@L33506`, `TypeRef-Transmute@L33524`
- `TypeRef-CallTypeArgs@L33542`, `def.TypeRefsExprRules@L33560`, `def.NoSpecificTypeRefsExpr@L33574`, `TypeRef-Expr-Sub@L33588`, `TypeRef-RecordPattern@L33606`, `TypeRef-EnumPattern@L33624`, `TypeRef-LiteralPattern@L33642`, `TypeRef-WildcardPattern@L33659`
- `TypeRef-IdentifierPattern@L33676`, `TypeRef-TuplePattern@L33693`, `TypeRef-ModalPattern-None@L33711`, `TypeRef-ModalPattern-Record@L33728`, `TypeRef-RangePattern@L33746`, `TypeRef-Field-Explicit@L33764`, `TypeRef-Field-Implicit@L33782`, `TypeRefsExprs-Empty@L33799`
- `TypeRefsExprs-Cons@L33816`, `def.TypeRefsArgsJudgementSet@L33834`, `TypeRefsArgs-Empty@L33848`, `TypeRefsArgs-Cons@L33865`, `TypeRefsEnumPayload-None@L33883`, `TypeRefsEnumPayload-Tuple@L33900`, `TypeRefsEnumPayload-Record@L33918`, `TypeRefsFields-Empty@L33936`
- `TypeRefsFields-Cons@L33953`, `TypeRefsPayload-None@L33971`, `TypeRefsPayload-Tuple@L33988`, `TypeRefsPayload-Record@L34006`, `def.ValueReferenceEnvironmentAliases@L34024`, `def.ValueRefsJudgementSet@L34038`, `ValueRef-Ident@L34052`, `ValueRef-Ident-Local@L34070`
- `ValueRef-Qual@L34088`, `ValueRef-Qual-Local@L34106`, `ValueRef-QualApply@L34124`, `ValueRef-QualApply-Local@L34142`, `ValueRef-QualApply-Brace@L34160`, `def.ValueRefsRules@L34178`, `def.NoSpecificValueRefsExpr@L34192`, `ValueRef-Expr-Sub@L34206`
- `ValueRefsArgs-Empty@L34224`, `ValueRefsArgs-Cons@L34241`, `ValueRefsFields-Empty@L34259`, `ValueRefsFields-Cons@L34276`, `def.AstTraversalNodeHelpers@L34294`, `def.EnumVariantTypeSets@L34320`, `def.GeneralTypePositionSetHelpers@L34337`, `def.RecordMemberTypeSets@L34356`
- `def.ClassItemTypeSets@L34374`, `def.DeclarationTypePositions@L34392`, `def.TypePositionExpressions@L34413`, `def.TypeDeps@L34429`, `def.ValueDependencyExpressionSets@L34443`, `def.ValueDepsEagerLazy@L34461`, `def.ModuleDependencyGraphs@L34476`, `WF-Acyclic-Eager@L34496`
- `diagnostics.ModuleAggregation@L34560`
- `Parse-Import@L29478`, `def.ImportDeclAst@L29496`, `req.ImportDeclarationBindingSemanticsScope@L29514`, `Import-Path@L29530`, `Import-Path-Err@L29548`, `Bind-Import@L29566`, `Bind-Import-Err@L29584`, `ResolveItem-Import@L29602`
- `diagnostics.ImportDeclarations@L29651`, `req.ImportDeclarationDiagnosticOwnership@L29669`, `Parse-Using-Wildcard@L29713`, `Parse-Using-List@L29731`, `Parse-Using-Item@L29749`, `Parse-UsingSpec@L29767`, `Parse-UsingList-Empty@L29785`, `Parse-UsingList-Cons@L29803`
- `Parse-UsingListTail-End@L29821`, `Parse-UsingListTail-TrailingComma@L29839`, `Parse-UsingListTail-Comma@L29857`, `def.UsingDeclAst@L29875`, `req.UsingDeclarationBindingSemanticsScope@L29901`, `def.UsingSpecName@L29917`, `def.UsingSpecNames@L29933`, `Using-Item@L29947`
- `Using-Item-Public-Err@L29965`, `Using-List@L29983`, `Using-Wildcard-Warn@L30001`, `Using-Wildcard@L30019`, `Using-List-Dup@L30037`, `Using-List-Public-Err@L30055`, `Bind-Using@L30073`, `Bind-Using-Err@L30091`
- `ResolveItem-Using@L30109`, `diagnostics.UsingDeclarations@L30158`, `req.UsingDeclarationDiagnosticOwnership@L30179`, `def.StaticDeclTopLevelItems@L30215`, `Parse-Static-Decl@L30245`, `def.StaticDeclAst@L30263`, `req.StaticDeclModuleScopeBindingSemantics@L30281`, `def.StaticVisOk@L30297`
- `Bind-Static@L30311`, `WF-StaticDecl@L30329`, `WF-StaticDecl-Ann-Mismatch@L30347`, `WF-StaticDecl-MissingType@L30365`, `StaticVisOk-Err@L30383`, `ResolveItem-Static@L30401`, `def.StaticBindTypes@L30481`, `def.StaticBindList@L30495`
- `Emit-Static-Const@L30539`, `Emit-Static-Init@L30557`, `Emit-Static-Multi@L30575`, `InitFn@L30607`, `DeinitFn@L30639`, `def.StaticItems@L30657`, `def.StaticItemOf@L30671`, `def.StaticType@L30741`
- `def.StaticBindInfo@L30755`, `Lower-StaticInit-Item@L30799`, `Lower-StaticInitItems-Empty@L30817`, `Lower-StaticInitItems-Cons@L30834`, `Lower-StaticInit@L30852`, `InitCallIR@L30870`, `Lower-StaticDeinitNames-Empty@L30903`, `Lower-StaticDeinitNames-Cons-Resp@L30920`
- `Lower-StaticDeinitNames-Cons-NoResp@L30938`, `Lower-StaticDeinit-Item@L30956`, `Lower-StaticDeinitItems-Empty@L30974`, `Lower-StaticDeinitItems-Cons@L30991`, `Lower-StaticDeinit@L31009`, `diagnostics.StaticDeclarations@L31027`, `req.StaticDeclarationDiagnosticOwnership@L31047`, `Parse-ExternBlock@L31100`
- `Parse-ExternAbiOpt-None@L31118`, `Parse-ExternAbiOpt-String@L31136`, `Parse-ExternAbiOpt-Ident@L31154`, `Parse-ExternItemList-End@L31172`, `Parse-ExternItemList-Cons@L31190`, `def.ExternBlockAst@L31208`, `req.ExternBlockStaticSemanticsScope@L31229`, `Bind-ExternBlock@L31245`
- `WF-ExternBlock@L31263`, `ExternAbi-Unknown-Err@L31281`, `ResolveItem-ExternBlock@L31299`, `req.ModuleAggregationStaticSemanticsScope@L31529`, `def.NameCollectAfterParse@L31791`, `def.QualifiedLookupContext@L32380`, `def.ModuleAssemblyPathHelpers@L32397`, `def.ImportDeclarationsOfModule@L32413`
- `def.VisibleModulesAndNames@L32428`, `def.ModulePathPrefix@L32450`, `AliasExpand-None@L32466`, `AliasExpand-Yes@L32484`, `def.CurrentAsmPath@L32502`, `ModulePrefix-Direct@L32516`, `ModulePrefix-Current@L32534`, `ModulePrefix-None@L32552`
- `Resolve-ModulePath-Direct@L32570`, `Resolve-ModulePath-Current@L32588`, `ResolveModulePath-Err@L32606`, `def.ModuleByPath@L32624`, `def.ModuleOfPath@L32638`, `def.ItemNames@L32652`, `ItemOfPath@L32666`, `ItemOfPath-None@L32684`
- `def.ImportCoveragePredicates@L32702`, `def.ImportOkJudgementSet@L32717`, `Import-Ok-Local@L32731`, `Import-Ok-Covered@L32749`, `Import-Ok-Err@L32767`, `Resolve-Import-Direct@L32785`, `Resolve-Import-Current@L32803`, `Resolve-Import-Err@L32821`
- `Resolve-Using-Ok@L32839`, `Resolve-Using-Err@L32857`, `req.ResolvedItemAccessibilityOwnedByVisibilityChapter@L32875`, `def.ModuleInitializationDependencyEnvironment@L32890`, `Reachable-Edge@L32906`, `Reachable-Step@L32924`, `def.ModuleInitializationPathHelpers@L32942`, `def.TypeRefsJudgementSet@L32958`
- `def.TypeReferenceEnvironmentAliases@L32972`, `TypeRef-Path@L32988`, `TypeRef-Using@L33006`, `TypeRef-Path-Local@L33024`, `TypeRef-Dynamic@L33042`, `TypeRef-ModalState@L33060`, `TypeRef-Apply@L33078`, `TypeRef-Perm@L33096`
- `TypeRef-Prim@L33114`, `TypeRef-Tuple@L33131`, `TypeRef-Array@L33149`, `TypeRef-Slice@L33167`, `TypeRef-Union@L33185`, `TypeRef-Func@L33203`, `TypeRef-String@L33221`, `TypeRef-Bytes@L33238`
- `TypeRef-Ptr@L33255`, `TypeRef-RawPtr@L33273`, `TypeRef-Range@L33291`, `TypeRef-RangeInclusive@L33309`, `TypeRef-RangeFrom@L33327`, `TypeRef-RangeTo@L33345`, `TypeRef-RangeToInclusive@L33363`, `TypeRef-RangeFull@L33381`
- `TypeRef-Ref-Path@L33398`, `TypeRef-Ref-Apply@L33416`, `TypeRef-Ref-ModalState@L33434`, `TypeRef-RecordExpr@L33452`, `TypeRef-EnumLiteral@L33470`, `TypeRef-QualBrace@L33488`, `TypeRef-Cast@L33506`, `TypeRef-Transmute@L33524`
- `TypeRef-CallTypeArgs@L33542`, `def.TypeRefsExprRules@L33560`, `def.NoSpecificTypeRefsExpr@L33574`, `TypeRef-Expr-Sub@L33588`, `TypeRef-RecordPattern@L33606`, `TypeRef-EnumPattern@L33624`, `TypeRef-LiteralPattern@L33642`, `TypeRef-WildcardPattern@L33659`
- `TypeRef-IdentifierPattern@L33676`, `TypeRef-TuplePattern@L33693`, `TypeRef-ModalPattern-None@L33711`, `TypeRef-ModalPattern-Record@L33728`, `TypeRef-RangePattern@L33746`, `TypeRef-Field-Explicit@L33764`, `TypeRef-Field-Implicit@L33782`, `TypeRefsExprs-Empty@L33799`
- `TypeRefsExprs-Cons@L33816`, `def.TypeRefsArgsJudgementSet@L33834`, `TypeRefsArgs-Empty@L33848`, `TypeRefsArgs-Cons@L33865`, `TypeRefsEnumPayload-None@L33883`, `TypeRefsEnumPayload-Tuple@L33900`, `TypeRefsEnumPayload-Record@L33918`, `TypeRefsFields-Empty@L33936`
- `TypeRefsFields-Cons@L33953`, `TypeRefsPayload-None@L33971`, `TypeRefsPayload-Tuple@L33988`, `TypeRefsPayload-Record@L34006`, `def.ValueReferenceEnvironmentAliases@L34024`, `def.ValueRefsJudgementSet@L34038`, `ValueRef-Ident@L34052`, `ValueRef-Ident-Local@L34070`
- `ValueRef-Qual@L34088`, `ValueRef-Qual-Local@L34106`, `ValueRef-QualApply@L34124`, `ValueRef-QualApply-Local@L34142`, `ValueRef-QualApply-Brace@L34160`, `def.ValueRefsRules@L34178`, `def.NoSpecificValueRefsExpr@L34192`, `ValueRef-Expr-Sub@L34206`
- `ValueRefsArgs-Empty@L34224`, `ValueRefsArgs-Cons@L34241`, `ValueRefsFields-Empty@L34259`, `ValueRefsFields-Cons@L34276`, `def.AstTraversalNodeHelpers@L34294`, `def.EnumVariantTypeSets@L34320`, `def.GeneralTypePositionSetHelpers@L34337`, `def.RecordMemberTypeSets@L34356`
- `def.ClassItemTypeSets@L34374`, `def.DeclarationTypePositions@L34392`, `def.TypePositionExpressions@L34413`, `def.TypeDeps@L34429`, `def.ValueDependencyExpressionSets@L34443`, `def.ValueDepsEagerLazy@L34461`, `def.ModuleDependencyGraphs@L34476`, `WF-Acyclic-Eager@L34496`
- `diagnostics.ModuleAggregation@L34560`

### Types, Permissions, Declarations, And Static Semantics

#### `checker.binding-state`

Count: 72 total; 72 required; 0 recommended; 0 informative. Ledger line span: L14425-L15866.

- `def.BindingStateDomain@L14425`, `def.BindInfo@L14439`, `def.BindingEnvironment@L14456`, `def.BindingScopeStackOps@L14471`, `def.BindingLookup@L14486`, `def.BindingUpdate@L14503`, `def.BindingIntro@L14520`, `def.BindingStateJoin@L14629`
- `def.BindingStateTransitionSet@L14648`, `Trans-Move-Whole@L14662`, `Trans-Move-Field@L14680`, `Trans-Move-Field-Partial@L14698`, `Trans-Partial-To-Moved@L14716`, `Trans-Reassign@L14734`, `Trans-Moved-NoAccess@L14752`, `Trans-Partial-NoAccess@L14770`
- `Trans-Let-NoReassign@L14788`, `def.BindingInfoJoin@L14806`, `def.BindingScopeJoin@L14822`, `def.BindingEnvironmentJoin@L14838`, `def.FieldHead@L14921`, `def.FieldPathOf@L14941`, `def.PlacePath@L14960`, `def.ArgumentPassExpression@L15040`
- `def.AccessStateOk@L15058`, `def.PartialMoveStateUpdate@L15074`, `def.ExpressionTypeLookupForAccess@L15090`, `def.AccessOk@L15105`, `def.BindingMovabilityOperator@L15121`, `def.MoveExpressionPredicate@L15136`, `def.InitializationResponsibility@L15151`, `def.BindingInitializerExpression@L15168`
- `def.BindingInitializerScope@L15184`, `def.TemporaryScope@L15200`, `def.TemporaryValuePredicate@L15216`, `def.TemporaryEvaluationOrder@L15230`, `def.ControlStatementExpression@L15251`, `def.TemporaryDropOrder@L15267`, `def.OptionalExpressionList@L15282`, `def.StatementExpressions@L15297`
- `def.StatementAndBindingScopes@L15324`, `def.BlockStatements@L15341`, `def.StatementBlocks@L15355`, `def.StatementSubExpressions@L15373`, `def.StatementSubStatements@L15389`, `def.SubBlocks@L15407`, `def.MapEntries@L15423`, `def.MapUnion@L15437`
- `def.IntroduceAllBindings@L15451`, `def.BindInfoMap@L15465`, `def.EffectiveMovability@L15479`, `def.BindingNames@L15514`, `def.JoinAllBindings@L15528`, `def.ConsumeOnMove@L15590`, `def.MoveExpressionInnerPlace@L15606`, `def.BindingJudgmentSet@L15620`
- `def.StaticBindingMaps@L15634`, `def.ProcedureEntryBindingScopes@L15652`, `def.ParameterBindingMap@L15667`, `def.MethodParameterBindingMap@L15682`, `def.ParameterTypeMap@L15696`, `def.ParameterMoveAndResponsibility@L15711`, `def.InitialBindingEnvironment@L15726`, `def.BindCheck@L15754`
- `def.ProcedureBindingCheck@L15768`, `def.MethodParametersForBinding@L15782`, `def.MethodBindingCheck@L15796`, `def.ClassMethodBindingCheck@L15810`, `def.StateMethodBindingCheck@L15824`, `def.TransitionBindingCheck@L15838`, `def.BindingDiagnosticReferences@L15852`, `req.FeatureSpecificBJudgmentOwnership@L15866`

#### `checker.permission-state`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L14536-L15740.

- `def.PermissionOfType@L14536`, `def.PermissionActivityDomain@L14551`, `def.PermissionEnvironment@L14565`, `def.PermissionScopeStackOps@L14581`, `def.PermissionLookup@L14596`, `def.PermissionUpdate@L14613`, `def.PermissionStateJoin@L14856`, `def.PermissionAtScope@L14871`
- `def.PermissionScopeJoin@L14887`, `def.PermissionEnvironmentJoin@L14901`, `def.AccessPathPrefixes@L14976`, `def.AccessPathOk@L14992`, `def.SuspendUniquePath@L15006`, `def.ReactivatePermissionKeys@L15025`, `def.JoinAllPermissions@L15544`, `def.PermissionTopScopeOps@L15560`
- `def.PermissionRoots@L15576`, `def.InitialPermissionEnvironment@L15740`

#### `checker.regions`

Count: 12 total; 12 required; 0 recommended; 0 informative. Ledger line span: L15494-L16037.

- `def.RegionBindingInfo@L15494`, `def.RegionOptionsFields@L15884`, `def.RegionOptionsDecl@L15901`, `def.RegionPreallocation@L15916`, `def.RegionActiveType@L15931`, `def.FreshRegion@L15945`, `def.RegionOptionsExpression@L15959`, `def.RegionBind@L15974`
- `def.InnermostActiveRegion@L15990`, `def.FrameBind@L16007`, `req.RegionSyntheticIdentifierRestriction@L16023`, `req.FrameSyntheticIdentifierRestriction@L16037`

#### `checker.provenance`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L16053-L16840.

- `def.ProvenanceTags@L16053`, `def.RegionNesting@L16067`, `def.StrictProvenanceLifetimeOrder@L16081`, `def.ProvenanceLifetimeOrder@L16095`, `def.FrameTarget@L16109`, `def.FrameTargetProvenanceOrder@L16124`, `def.ProvenanceJoin@L16139`, `def.JoinAllProvenance@L16156`
- `def.ProvenanceEnvironmentShape@L16174`, `def.ProvenanceScopeAccessors@L16192`, `def.ProvenanceScopeStackOps@L16209`, `def.ProvenanceLookup@L16224`, `def.ProvenanceIntro@L16240`, `def.ProvenanceIntroAll@L16254`, `def.ParameterProvenanceInitialization@L16269`, `def.ProvenanceRegionEntryResolution@L16284`
- `def.ProvenanceRegionAliasIntro@L16303`, `def.FreshRegionTag@L16317`, `def.AllocationRegionTagSelection@L16331`, `def.FreshRegionExpression@L16349`, `def.ProvenanceJudgmentSets@L16363`, `def.CaseBodyProvenance@L16380`, `def.CasePatternProvenanceEnvironment@L16395`, `def.CaseProvenance@L16409`
- `def.CaseElseProvenance@L16423`, `rules.ProvenanceChecking@L16438`, `P-If-Is@L16456`, `P-If-Cases@L16474`, `def.ClosureCaptureProvenance@L16494`, `def.ClosureTargetProvenance@L16508`, `def.ClosureLocalSharedCaptures@L16524`, `def.ClosureEscapeCheck@L16538`
- `P-Closure-NonCapturing@L16554`, `P-Closure-Capturing@L16572`, `P-Closure-Escape-Err@L16591`, `def.FrameProvenance@L16610`, `def.BreakProvenance@L16628`, `def.IteratorElementProvenance@L16642`, `def.InfiniteLoopProvenance@L16656`, `def.FiniteLoopProvenance@L16671`
- `def.ExtendProvenanceForPattern@L16686`, `P-Loop-Infinite@L16700`, `P-Loop-Conditional@L16718`, `P-Loop-Iter@L16736`, `def.ProvenanceEscapeHelpers@L16754`, `req.NoGeneralHeapEscapeConversion@L16768`, `def.BindingProvenance@L16782`, `def.StaticBindingProvenance@L16798`
- `def.AssignmentProvenanceEscapeCheck@L16812`, `def.ProvenanceEscapeJudgmentSet@L16826`, `req.ProvenanceEscapeCheckPurpose@L16840`

#### `checker.types`

Count: 283 total; 181 required; 0 recommended; 0 informative. Ledger line span: L23734-L40932.

- `def.TypeEquivalenceJudgementSet@L23734`, `def.ConstLenJudgementSet@L23748`, `ConstLen-Lit@L23762`, `ConstLen-Path@L23780`, `ConstLen-Err@L23798`, `def.UnionMembersEquivalence@L23816`, `T-Equiv-Prim@L23830`, `T-Equiv-Perm@L23848`
- `T-Equiv-Tuple@L23866`, `T-Equiv-Array@L23884`, `T-Equiv-Slice@L23902`, `T-Equiv-Func@L23920`, `T-Equiv-Closure@L23938`, `T-Equiv-Union@L23956`, `T-Equiv-Path@L23974`, `T-Equiv-ModalState@L23992`
- `T-Equiv-String@L24010`, `T-Equiv-Bytes@L24028`, `T-Equiv-Range@L24046`, `T-Equiv-RangeInclusive@L24064`, `T-Equiv-RangeFrom@L24082`, `T-Equiv-RangeTo@L24100`, `T-Equiv-RangeToInclusive@L24118`, `T-Equiv-RangeFull@L24136`
- `T-Equiv-Ptr@L24154`, `T-Equiv-RawPtr@L24172`, `T-Equiv-Dynamic@L24190`, `T-Equiv-Apply@L24208`, `T-Equiv-Opaque@L24226`, `T-Equiv-Refine@L24244`, `def.PredicateEquivalence@L24262`, `T-Equiv-Refine-Norm@L24276`
- `T-Equiv-Refl@L24294`, `T-Equiv-Sym@L24312`, `T-Equiv-Trans@L24330`, `def.SubtypingJudgementSet@L24351`, `req.NoIntegerNumericSubtyping@L24365`, `req.NoFloatNumericSubtyping@L24379`, `req.PermissionAdmissibilityOwnedByChapter10@L24393`, `Sub-Perm@L24407`
- `Sub-Never@L24425`, `Sub-Tuple@L24443`, `Sub-Array@L24461`, `Sub-Slice@L24479`, `Sub-Range@L24497`, `Sub-RangeInclusive@L24515`, `Sub-RangeFrom@L24533`, `Sub-RangeTo@L24551`
- `Sub-RangeToInclusive@L24569`, `Sub-RangeFull@L24587`, `Sub-Ptr-State@L24605`, `Sub-Modal-Niche@L24623`, `Sub-Func@L24641`, `Sub-Closure@L24659`, `Sub-Async@L24677`, `def.UnionMember@L40364`
- `Sub-Member-Union@L40378`, `Sub-Union-Width@L40396`, `def.VarianceDomain@L24746`, `def.VarianceOfSignature@L24760`, `def.VarianceOf@L24774`, `def.VarianceSatisfied@L24788`, `Sub-Generic@L24806`, `Sub-Generic-Invariant-Err@L24824`
- `Sub-RangeToInclusive@L24569`, `Sub-RangeFull@L24587`, `Sub-Ptr-State@L24605`, `Sub-Modal-Niche@L24623`, `Sub-Func@L24641`, `Sub-Closure@L24659`, `Sub-Async@L24677`, `def.UnionMember@L40364`
- `Sub-Member-Union@L40378`, `Sub-Union-Width@L40396`, `def.VarianceDomain@L24746`, `def.VarianceOfSignature@L24760`, `def.VarianceOf@L24774`, `def.VarianceSatisfied@L24788`, `Sub-Generic@L24806`, `Sub-Generic-Invariant-Err@L24824`
- `Sub-Generic-Covariant-Err@L24842`, `Sub-Generic-Contravariant-Err@L24860`, `Sub-Refl@L24878`, `Sub-Trans@L24896`, `def.TypeInferenceJudgementSet@L24917`, `def.TypeEqualityConstraint@L24931`, `def.TypeEqualityConstraintSet@L24945`, `req.ConstraintGenerationFeatureLocal@L24959`
- `def.TypeVariableDomain@L24973`, `def.TypeVariablesOfType@L24987`, `def.SubstitutionDomain@L25001`, `def.SubstitutionDefinedDomain@L25015`, `def.IdentitySubstitution@L25029`, `def.SubstitutionApplication@L25043`, `def.SubstitutionComposition@L25067`, `def.UnificationStateDomain@L25081`
- `Unify-Empty@L25095`, `Unify-Eq@L25112`, `Unify-Var-L@L25130`, `Unify-Var-R@L25148`, `Unify-Occurs-Fail@L25166`, `Unify-Tuple@L25184`, `Unify-Tuple-Fail@L25202`, `Unify-Array@L25220`
- `Unify-Array-Len-Fail@L25238`, `Unify-Slice@L25256`, `Unify-Perm@L25274`, `Unify-Perm-Fail@L25292`, `Unify-Func@L25310`, `Unify-Func-Fail@L25329`, `Unify-Closure@L25347`, `Unify-Closure-Fail@L25365`
- `Unify-Ptr@L25383`, `Unify-Ptr-State-Fail@L25401`, `Unify-RawPtr@L25419`, `Unify-RawPtr-Qual-Fail@L25437`, `Unify-Apply@L25455`, `Unify-Apply-Fail@L25473`, `Unify-Range@L25491`, `Unify-RangeInclusive@L25509`
- `Unify-RangeFrom@L25527`, `Unify-RangeTo@L25545`, `Unify-RangeToInclusive@L25563`, `Unify-Refine@L25581`, `Unify-Refine-Pred-Fail@L25599`, `Unify-Prim-Fail@L25617`, `Unify-Rigid-Fail@L25635`, `Unify-Ctor-Mismatch@L25660`
- `Unify-Ok@L25678`, `Unify-Err@L25696`, `Solve-Unify@L25714`, `Solve-Fail@L25732`, `Syn-Expr@L25750`, `Syn-Ident@L25768`, `Syn-Unit@L25786`, `Syn-Tuple@L25803`
- `Syn-Call@L25821`, `Syn-Call-Err@L25839`, `Chk-Subsumption-Modal-NonNiche@L25857`, `Chk-Subsumption@L25875`, `Chk-Null-Ptr@L25893`, `def.PtrNullExpectedType@L25911`, `Syn-PtrNull-Err@L25925`, `Chk-PtrNull-Err@L25943`
- `req.FeatureLocalSynthesisAndCheckingOwnership@L25961`, `property.TypeSystemMetatheory.Intro@L25978`, `Progress@L25992`, `Preservation@L26011`, `No-Use-After-Free@L26027`, `No-Double-Free@L26043`, `No-Dangling-Pointers@L26059`, `Exclusivity-Invariant@L26075`
- `Permission-Preservation@L26091`, `State-Determinism@L26107`, `No-Resurrection@L26123`, `Data-Race-Freedom@L26139`, `Fork-Join-Guarantee@L26155`, `Key-Serialization@L26171`, `Async-Key-Safety@L26187`, `req.PermissionQualifiedSubtypingPermissionEquality@L29356`
- `Parse-Record@L38163`, `Parse-RecordBody@L38181`, `Parse-RecordMemberList-End@L38199`, `Parse-RecordMemberList-Cons@L38217`, `Parse-RecordMember-Method@L38235`, `Parse-RecordMember-AssociatedType@L38253`, `Parse-RecordMember-Field@L38271`, `Parse-RecordFieldDeclAfterVis@L38289`
- `Parse-RecordFieldInitOpt-None@L38307`, `Parse-RecordFieldInitOpt-Yes@L38325`, `Parse-Record-Literal@L38343`, `def.RecordDeclAst@L38361`, `def.RecordMemberAst@L38378`, `def.RecordExprAst@L38396`, `def.RecordMembersSelectors@L38410`, `def.RecordPath@L38425`
- `Bind-Record@L38443`, `Resolve-RecordPath@L38460`, `ResolveQual-Name-Record@L38478`, `ResolveQual-Apply-RecordLit@L38496`, `ResolveItem-Record@L38514`, `def.RecordFieldInitOk@L38532`, `def.RecordFieldVisibility@L38546`, `WF-Record@L38561`
- `WF-Record-DupField@L38579`, `WF-RecordDecl@L38597`, `FieldVisOk-Err@L38615`, `def.RecordDefaultConstructible@L38633`, `def.RecordCallee@L38647`, `T-Record-Default@L38661`, `def.RecordFieldNameSets@L38679`, `def.RecordFieldLookup@L38697`
- `T-Record-Literal@L38713`, `EvalSigma-Record@L38747`, `EvalSigma-Record-Ctrl@L38765`, `ApplyRecordCtorSigma@L38797`, `ApplyRecordCtorSigma-Ctrl@L38815`, `Layout-Record-Empty@L38856`, `Layout-Record-Cons@L38873`, `Size-Record@L38891`
- `Align-Record@L38909`, `Layout-Record@L38927`, `LowerFieldInits-Empty@L39015`, `LowerFieldInits-Cons@L39032`, `Lower-Expr-Record@L39050`, `Lower-CallIR-RecordCtor@L39068`, `diagnostics.Records@L39086`, `Parse-Enum@L39153`
- `Parse-EnumBody@L39171`, `Parse-VariantMembers-Empty@L39189`, `Parse-VariantMembers-Cons@L39207`, `Parse-VariantSep-End@L39225`, `Parse-VariantSep-Terminator@L39243`, `Parse-Variant@L39261`, `Parse-VariantPayloadOpt-None@L39279`, `Parse-VariantPayloadOpt-Tuple@L39297`
- `Parse-VariantPayloadOpt-Record@L39315`, `Parse-VariantDiscriminantOpt-None@L39333`, `Parse-VariantDiscriminantOpt-Yes@L39351`, `req.EnumLiteralResolutionOwnership@L39383`, `def.EnumDeclAst@L39397`, `def.VariantDeclAst@L39414`, `def.EnumVariantHelpers@L39430`, `Bind-Enum@L39452`
- `def.EnumPayloadWellFormedness@L39469`, `Resolve-EnumUnit@L39487`, `Resolve-EnumTuple@L39505`, `Resolve-EnumRecord@L39523`, `ResolveQual-Name-Enum@L39541`, `ResolveQual-Apply-Enum-Tuple@L39559`, `ResolveQual-Apply-Enum-Record@L39577`, `ResolveItem-Enum@L39595`
- `def.EnumDiscriminantSequence@L39613`, `Enum-Disc-NotInt@L39635`, `Enum-Disc-Invalid@L39653`, `Enum-Disc-Negative@L39671`, `Enum-Disc-Dup@L39689`, `Enum-Empty-Err@L39707`, `Enum-Variant-Dup@L39725`, `def.EnumDiscriminantType@L39743`
- `WF-EnumDecl@L39762`, `def.EnumLiteralPayloadHelpers@L39780`, `T-Enum-Lit-Unit@L39798`, `Enum-Lit-Unknown@L39816`, `T-Enum-Lit-Tuple@L39834`, `Enum-Lit-Tuple-Arity-Err@L39852`, `T-Enum-Lit-Record@L39870`, `Enum-Lit-Record-MissingField@L39888`
- `EvalSigma-Enum-Unit@L39922`, `EvalSigma-Enum-Tuple@L39939`, `EvalSigma-Enum-Tuple-Ctrl@L39957`, `EvalSigma-Enum-Record@L39975`, `EvalSigma-Enum-Record-Ctrl@L39993`, `Layout-Enum-Tagged@L40040`, `Size-Enum@L40058`, `Align-Enum@L40076`
- `Layout-Enum@L40094`, `Lower-Expr-Enum-Unit@L40142`, `Lower-Expr-Enum-Tuple@L40159`, `Lower-Expr-Enum-Record@L40177`, `diagnostics.Enums@L40195`, `req.UnionIntroductionSemantic@L40242`, `Parse-UnionTail-None@L40258`, `Parse-UnionTail-Cons@L40276`
- `def.TypeUnionAst@L40294`, `def.UnionMemberSets@L40310`, `WF-Union@L40328`, `WF-Union-TooFew@L40346`, `def.UnionMember@L40364`, `Sub-Member-Union@L40378`, `Sub-Union-Width@L40396`, `T-Union-Intro@L40414`
- `Union-DirectAccess-Err@L40432`, `req.UnionMatchingPropagationOwnership@L40450`, `Layout-Union-Niche@L40637`, `Layout-Union-Tagged@L40655`, `Size-Union@L40673`, `Align-Union@L40691`, `Layout-Union@L40709`, `req.UnionDiagnosticOwnership@L40827`
- `Parse-Type-Alias@L40866`, `def.TypeAliasDeclAst@L40884`, `def.TypeAliasAccessors@L40900`, `Bind-TypeAlias@L40918`, `ResolveItem-TypeAlias@L40935`, `def.AliasNormalization@L40953`, `def.AliasPathNormalization@L40989`, `def.AliasTransparent@L41006`
- `def.AliasGraph@L41020`, `def.TypePaths@L41034`, `def.TypePathsOfModalRef@L41070`, `def.AliasCycle@L41085`, `TypeAlias-Ok@L41099`, `TypeAlias-Recursive-Err@L41116`, `req.TypeAliasDynamicSemantics@L41133`, `Size-Alias@L41151`
- `Align-Alias@L41169`, `Layout-Alias@L41187`, `req.TypeAliasDiagnosticOwnership@L41219`
- `Permission-Preservation@L26091`, `State-Determinism@L26107`, `No-Resurrection@L26123`, `Data-Race-Freedom@L26139`, `Fork-Join-Guarantee@L26155`, `Key-Serialization@L26171`, `Async-Key-Safety@L26187`, `req.PermissionQualifiedSubtypingPermissionEquality@L29356`
- `Parse-Record@L38163`, `Parse-RecordBody@L38181`, `Parse-RecordMemberList-End@L38199`, `Parse-RecordMemberList-Cons@L38217`, `Parse-RecordMember-Method@L38235`, `Parse-RecordMember-AssociatedType@L38253`, `Parse-RecordMember-Field@L38271`, `Parse-RecordFieldDeclAfterVis@L38289`
- `Parse-RecordFieldInitOpt-None@L38307`, `Parse-RecordFieldInitOpt-Yes@L38325`, `Parse-Record-Literal@L38343`, `def.RecordDeclAst@L38361`, `def.RecordMemberAst@L38378`, `def.RecordExprAst@L38396`, `def.RecordMembersSelectors@L38410`, `def.RecordPath@L38425`
- `Bind-Record@L38443`, `Resolve-RecordPath@L38460`, `ResolveQual-Name-Record@L38478`, `ResolveQual-Apply-RecordLit@L38496`, `ResolveItem-Record@L38514`, `def.RecordFieldInitOk@L38532`, `def.RecordFieldVisibility@L38546`, `WF-Record@L38561`
- `WF-Record-DupField@L38579`, `WF-RecordDecl@L38597`, `FieldVisOk-Err@L38615`, `def.RecordDefaultConstructible@L38633`, `def.RecordCallee@L38647`, `T-Record-Default@L38661`, `def.RecordFieldNameSets@L38679`, `def.RecordFieldLookup@L38697`
- `T-Record-Literal@L38713`, `EvalSigma-Record@L38747`, `EvalSigma-Record-Ctrl@L38765`, `ApplyRecordCtorSigma@L38797`, `ApplyRecordCtorSigma-Ctrl@L38815`, `Layout-Record-Empty@L38856`, `Layout-Record-Cons@L38873`, `Size-Record@L38891`
- `Align-Record@L38909`, `Layout-Record@L38927`, `LowerFieldInits-Empty@L39015`, `LowerFieldInits-Cons@L39032`, `Lower-Expr-Record@L39050`, `Lower-CallIR-RecordCtor@L39068`, `diagnostics.Records@L39086`, `Parse-Enum@L39153`
- `Parse-EnumBody@L39171`, `Parse-VariantMembers-Empty@L39189`, `Parse-VariantMembers-Cons@L39207`, `Parse-VariantSep-End@L39225`, `Parse-VariantSep-Terminator@L39243`, `Parse-Variant@L39261`, `Parse-VariantPayloadOpt-None@L39279`, `Parse-VariantPayloadOpt-Tuple@L39297`
- `Parse-VariantPayloadOpt-Record@L39315`, `Parse-VariantDiscriminantOpt-None@L39333`, `Parse-VariantDiscriminantOpt-Yes@L39351`, `req.EnumLiteralResolutionOwnership@L39383`, `def.EnumDeclAst@L39397`, `def.VariantDeclAst@L39414`, `def.EnumVariantHelpers@L39430`, `Bind-Enum@L39452`
- `def.EnumPayloadWellFormedness@L39469`, `Resolve-EnumUnit@L39487`, `Resolve-EnumTuple@L39505`, `Resolve-EnumRecord@L39523`, `ResolveQual-Name-Enum@L39541`, `ResolveQual-Apply-Enum-Tuple@L39559`, `ResolveQual-Apply-Enum-Record@L39577`, `ResolveItem-Enum@L39595`
- `def.EnumDiscriminantSequence@L39613`, `Enum-Disc-NotInt@L39635`, `Enum-Disc-Invalid@L39653`, `Enum-Disc-Negative@L39671`, `Enum-Disc-Dup@L39689`, `Enum-Empty-Err@L39707`, `Enum-Variant-Dup@L39725`, `def.EnumDiscriminantType@L39743`
- `WF-EnumDecl@L39762`, `def.EnumLiteralPayloadHelpers@L39780`, `T-Enum-Lit-Unit@L39798`, `Enum-Lit-Unknown@L39816`, `T-Enum-Lit-Tuple@L39834`, `Enum-Lit-Tuple-Arity-Err@L39852`, `T-Enum-Lit-Record@L39870`, `Enum-Lit-Record-MissingField@L39888`
- `EvalSigma-Enum-Unit@L39922`, `EvalSigma-Enum-Tuple@L39939`, `EvalSigma-Enum-Tuple-Ctrl@L39957`, `EvalSigma-Enum-Record@L39975`, `EvalSigma-Enum-Record-Ctrl@L39993`, `Layout-Enum-Tagged@L40040`, `Size-Enum@L40058`, `Align-Enum@L40076`
- `Layout-Enum@L40094`, `Lower-Expr-Enum-Unit@L40142`, `Lower-Expr-Enum-Tuple@L40159`, `Lower-Expr-Enum-Record@L40177`, `diagnostics.Enums@L40195`, `req.UnionIntroductionSemantic@L40242`, `Parse-UnionTail-None@L40258`, `Parse-UnionTail-Cons@L40276`
- `def.TypeUnionAst@L40294`, `def.UnionMemberSets@L40310`, `WF-Union@L40328`, `WF-Union-TooFew@L40346`, `def.UnionMember@L40364`, `Sub-Member-Union@L40378`, `Sub-Union-Width@L40396`, `T-Union-Intro@L40414`
- `Union-DirectAccess-Err@L40432`, `req.UnionMatchingPropagationOwnership@L40450`, `Layout-Union-Niche@L40637`, `Layout-Union-Tagged@L40655`, `Size-Union@L40673`, `Align-Union@L40691`, `Layout-Union@L40709`, `req.UnionDiagnosticOwnership@L40827`
- `Parse-Type-Alias@L40866`, `def.TypeAliasDeclAst@L40884`, `def.TypeAliasAccessors@L40900`, `Bind-TypeAlias@L40918`, `ResolveItem-TypeAlias@L40935`, `def.AliasNormalization@L40953`, `def.AliasPathNormalization@L40989`, `def.AliasTransparent@L41006`
- `def.AliasGraph@L41020`, `def.TypePaths@L41034`, `def.TypePathsOfModalRef@L41070`, `def.AliasCycle@L41085`, `TypeAlias-Ok@L41099`, `TypeAlias-Recursive-Err@L41116`, `req.TypeAliasDynamicSemantics@L41133`, `Size-Alias@L41151`
- `Align-Alias@L41169`, `Layout-Alias@L41187`, `req.TypeAliasDiagnosticOwnership@L41219`

#### `checker.attributes`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L26829-L27272.

- `req.MalformedAttributeSyntaxIllFormed@L26829`, `def.AttributeTargetDomain@L26843`, `def.AttributeRegistry@L26857`, `def.VendorAttributeRegistryInitial@L26871`, `def.SpecAttributeRegistry@L26885`, `def.SpecAttributeTargets@L26908`, `def.AttributeListWellFormedJudgementSet@L26944`, `AttrList-Ok@L26958`
- `AttrList-Unknown@L26976`, `AttrList-Target-Err@L26994`, `def.AttributeStaticSemantics.Helpers2@L27012`, `req.MemoryOrderAttributeTargets@L27026`, `req.AttributeListWellFormednessCheck@L27040`, `req.MultipleAttributeListConcatenation@L27054`, `req.FfiAttributeOwnership@L27068`, `conformance.VendorAttributeStaticSemantics@L27274`
- `req.MalformedAttributeSyntaxIllFormed@L26829`, `def.AttributeTargetDomain@L26843`, `def.AttributeRegistry@L26857`, `def.VendorAttributeRegistryInitial@L26871`, `def.SpecAttributeRegistry@L26885`, `def.SpecAttributeTargets@L26908`, `def.AttributeListWellFormedJudgementSet@L26944`, `AttrList-Ok@L26958`
- `AttrList-Unknown@L26976`, `AttrList-Target-Err@L26994`, `def.AttributeStaticSemantics.Helpers2@L27012`, `req.MemoryOrderAttributeTargets@L27026`, `req.AttributeListWellFormednessCheck@L27040`, `req.MultipleAttributeListConcatenation@L27054`, `req.FfiAttributeOwnership@L27068`, `conformance.VendorAttributeStaticSemantics@L27274`

#### `checker.attributes.layout`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L27401-L27559.

- `req.LayoutCRecordSemantics@L27403`, `req.LayoutCEnumSemantics@L27420`, `req.LayoutExplicitEnumDiscriminant@L27437`, `req.LayoutPackedRecordSemantics@L27456`, `req.PackedFieldReferenceRequiresUnsafe@L27473`, `req.LayoutAlignSemantics@L27489`, `def.ValidLayoutAttributeCombinations@L27508`, `def.InvalidLayoutAttributeCombinations@L27528`
- `def.LayoutAttributeApplicability@L27543`, `req.LayoutAttributeConstraints@L27561`
- `req.LayoutCRecordSemantics@L27403`, `req.LayoutCEnumSemantics@L27420`, `req.LayoutExplicitEnumDiscriminant@L27437`, `req.LayoutPackedRecordSemantics@L27456`, `req.PackedFieldReferenceRequiresUnsafe@L27473`, `req.LayoutAlignSemantics@L27489`, `def.ValidLayoutAttributeCombinations@L27508`, `def.InvalidLayoutAttributeCombinations@L27528`
- `def.LayoutAttributeApplicability@L27543`, `req.LayoutAttributeConstraints@L27561`

#### `checker.attributes.optimization`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27688-L27708.

- `req.InlineAttributeSemantics@L27690`, `req.ColdAttributeSemantics@L27710`
- `req.InlineAttributeSemantics@L27690`, `req.ColdAttributeSemantics@L27710`

#### `checker.attributes.metadata`

Count: 23 total; 23 required; 0 recommended; 0 informative. Ledger line span: L27837-L28187.

- `def.DynamicDeclarationPredicate@L27839`, `def.DynamicExpressionPredicate@L27853`, `def.DynamicScopePredicate@L27867`, `def.InDynamicContext@L27881`, `req.DeprecatedAttributeSemantics@L27897`, `req.DynamicAttributeSemantics@L27911`, `req.DynamicScopeDetermination@L27925`, `def.ComputeDynamicContext@L27941`
- `def.FindInnermostDynamic@L27964`, `def.MinimalSpan@L27985`, `DynamicContext-Override@L28003`, `req.DynamicContextOverridePropagation@L28023`, `DynamicContext-NoInherit-Call@L28037`, `req.DynamicContextLexicalNoCallPropagation@L28057`, `req.DynamicEffectsAndRestrictions@L28071`, `req.DynamicTargetRestrictions@L28088`
- `req.EmptyDynamicScopeWarning@L28105`, `req.StaleOkAttributeSemantics@L28119`, `req.VerificationModeAttributeSemantics@L28133`, `req.ReflectAttributeSemantics@L28147`, `req.DeriveAttributeSemantics@L28161`, `req.EmitAttributeSemantics@L28175`, `req.FilesAttributeSemantics@L28189`
- `def.DynamicDeclarationPredicate@L27839`, `def.DynamicExpressionPredicate@L27853`, `def.DynamicScopePredicate@L27867`, `def.InDynamicContext@L27881`, `req.DeprecatedAttributeSemantics@L27897`, `req.DynamicAttributeSemantics@L27911`, `req.DynamicScopeDetermination@L27925`, `def.ComputeDynamicContext@L27941`
- `def.FindInnermostDynamic@L27964`, `def.MinimalSpan@L27985`, `DynamicContext-Override@L28003`, `req.DynamicContextOverridePropagation@L28023`, `DynamicContext-NoInherit-Call@L28037`, `req.DynamicContextLexicalNoCallPropagation@L28057`, `req.DynamicEffectsAndRestrictions@L28071`, `req.DynamicTargetRestrictions@L28088`
- `req.EmptyDynamicScopeWarning@L28105`, `req.StaleOkAttributeSemantics@L28119`, `req.VerificationModeAttributeSemantics@L28133`, `req.ReflectAttributeSemantics@L28147`, `req.DeriveAttributeSemantics@L28161`, `req.EmitAttributeSemantics@L28175`, `req.FilesAttributeSemantics@L28189`

#### `checker.permissions`

Count: 32 total; 32 required; 0 recommended; 0 informative. Ledger line span: L28361-L29129.

- `def.PermissionQualifierSemantics@L28648`, `req.PermissionRegimesDistinct@L28662`, `def.PermissionRegimeProperties@L28676`, `req.PermissionRegimeConstraints@L28696`, `def.SharedPermissionOperationMatrix@L28714`, `Layout-Perm@L28772`, `SizeOf-Perm@L28788`, `AlignOf-Perm@L28804`
- `conformance.AliasAndExclusivityRules@L28853`, `def.AliasingByOverlappingStorage@L28905`, `req.UniqueExclusivityInvariant@L28921`, `def.PermissionCoexistenceMatrix@L28937`, `req.BindingActivityNoConcreteSyntax@L29005`, `def.UniqueBindingActivityStates@L29055`, `Inactive-Enter@L29074`, `Inactive-Exit@L29092`
- `req.InactiveUniqueBindingNoDirectUse@L29110`, `req.BindingActivityNoAliasCreation@L29126`, `req.BindingActivityDeterministicReactivation@L29140`, `conformance.BindingActivityLowering@L29154`, `req.BindingActivityDiagnosticOwnership@L29170`, `req.PermissionAdmissibilityNoAdditionalSyntax@L29189`, `def.PermissionAdmissibilityAstInputs@L29221`, `req.PermissionAdmissibilityScope@L29240`
- `def.PermAdmitsJudgementSet@L29256`, `def.PermissionAdmissibilityPairs@L29270`, `req.PermAdmitsUseSitesNoTypeRewrite@L29290`, `def.MethodReceiverPermissionAdmissibility@L29304`, `def.MethodReceiverPermissionMatrix@L29322`, `req.PermissionAdmissibilityNoImplicitConversion@L29340`, `conformance.PermissionAdmissibilitySharedKeyGate@L29386`, `diagnostics.PermissionAdmissibility@L29416`
- `def.PermissionQualifierSemantics@L28648`, `req.PermissionRegimesDistinct@L28662`, `def.PermissionRegimeProperties@L28676`, `req.PermissionRegimeConstraints@L28696`, `def.SharedPermissionOperationMatrix@L28714`, `Layout-Perm@L28772`, `SizeOf-Perm@L28788`, `AlignOf-Perm@L28804`
- `conformance.AliasAndExclusivityRules@L28853`, `def.AliasingByOverlappingStorage@L28905`, `req.UniqueExclusivityInvariant@L28921`, `def.PermissionCoexistenceMatrix@L28937`, `req.BindingActivityNoConcreteSyntax@L29005`, `def.UniqueBindingActivityStates@L29055`, `Inactive-Enter@L29074`, `Inactive-Exit@L29092`
- `req.InactiveUniqueBindingNoDirectUse@L29110`, `req.BindingActivityNoAliasCreation@L29126`, `req.BindingActivityDeterministicReactivation@L29140`, `conformance.BindingActivityLowering@L29154`, `req.BindingActivityDiagnosticOwnership@L29170`, `req.PermissionAdmissibilityNoAdditionalSyntax@L29189`, `def.PermissionAdmissibilityAstInputs@L29221`, `req.PermissionAdmissibilityScope@L29240`
- `def.PermAdmitsJudgementSet@L29256`, `def.PermissionAdmissibilityPairs@L29270`, `req.PermAdmitsUseSitesNoTypeRewrite@L29290`, `def.MethodReceiverPermissionAdmissibility@L29304`, `def.MethodReceiverPermissionMatrix@L29322`, `req.PermissionAdmissibilityNoImplicitConversion@L29340`, `conformance.PermissionAdmissibilitySharedKeyGate@L29386`, `diagnostics.PermissionAdmissibility@L29416`

#### `checker.ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L31062-L31062.

- `req.ExternBlockDiagnosticOwnership@L31349`
- `req.ExternBlockDiagnosticOwnership@L31349`

#### `checker.types.primitive`

Count: 13 total; 13 required; 0 recommended; 0 informative. Ledger line span: L34345-L34624.

- `Parse-Prim-Type@L34632`, `Parse-Unit-Type@L34650`, `Parse-Never-Type@L34668`, `def.PrimitiveTypeName@L34686`, `def.TypePrimAst@L34702`, `def.NumericPrimitiveTypeSets@L34716`, `def.TypeWFJudgementSet@L34734`, `WF-Prim@L34750`
- `def.PrimitiveFloatRepresentation@L34768`, `def.DefaultNumericTypes@L34791`, `def.PrimitiveIntegerWidths@L34806`, `def.PrimitiveRangeOf@L34822`, `req.PrimitiveTypeDiagnosticOwnership@L34911`
- `Parse-Prim-Type@L34632`, `Parse-Unit-Type@L34650`, `Parse-Never-Type@L34668`, `def.PrimitiveTypeName@L34686`, `def.TypePrimAst@L34702`, `def.NumericPrimitiveTypeSets@L34716`, `def.TypeWFJudgementSet@L34734`, `WF-Prim@L34750`
- `def.PrimitiveFloatRepresentation@L34768`, `def.DefaultNumericTypes@L34791`, `def.PrimitiveIntegerWidths@L34806`, `def.PrimitiveRangeOf@L34822`, `req.PrimitiveTypeDiagnosticOwnership@L34911`

#### `checker.types.tuples`

Count: 41 total; 41 required; 0 recommended; 0 informative. Ledger line span: L34683-L35482.

- `Parse-Tuple-Type@L34970`, `Parse-TupleTypeElems-Empty@L34988`, `Parse-TupleTypeElems-One@L35006`, `Parse-TupleTypeElems-Many@L35024`, `def.TupleScanDelimiterDeltas@L35042`, `TupleScan-EOF@L35100`, `TupleScan-EndParen@L35118`, `TupleScan-SingletonComma@L35135`
- `TupleScan-Separator@L35152`, `TupleScan-Advance@L35169`, `def.TupleParen@L35186`, `Parse-Tuple-Literal@L35200`, `Parse-TupleExprElems-Empty@L35218`, `Parse-TupleExprElems-Single@L35236`, `Parse-TupleExprElems-Many@L35254`, `Postfix-TupleIndex@L35272`
- `def.TupleTypeAst@L35290`, `def.TupleExpressionAst@L35306`, `WF-Tuple@L35337`, `T-Tuple-Unit@L35355`, `T-Tuple@L35372`, `T-Tuple-Index@L35390`, `T-Tuple-Index-Perm@L35408`, `P-Tuple-Index@L35426`
- `P-Tuple-Index-Perm@L35444`, `def.ConstTupleIndex@L35462`, `TupleIndex-NonConst@L35476`, `TupleIndex-OOB@L35494`, `TupleAccess-NotTuple@L35512`, `req.TuplePatternRulesOwnership@L35530`, `EvalSigma-Tuple@L35560`, `EvalSigma-Tuple-Ctrl@L35578`
- `EvalSigma-TupleAccess@L35596`, `EvalSigma-TupleAccess-Ctrl@L35614`, `Layout-Tuple-Empty@L35648`, `Layout-Tuple-Cons@L35665`, `Size-Tuple@L35683`, `Align-Tuple@L35701`, `Layout-Tuple@L35719`, `Lower-Expr-Tuple@L35751`
- `diagnostics.Tuples@L35769`
- `Parse-Tuple-Type@L34970`, `Parse-TupleTypeElems-Empty@L34988`, `Parse-TupleTypeElems-One@L35006`, `Parse-TupleTypeElems-Many@L35024`, `def.TupleScanDelimiterDeltas@L35042`, `TupleScan-EOF@L35100`, `TupleScan-EndParen@L35118`, `TupleScan-SingletonComma@L35135`
- `TupleScan-Separator@L35152`, `TupleScan-Advance@L35169`, `def.TupleParen@L35186`, `Parse-Tuple-Literal@L35200`, `Parse-TupleExprElems-Empty@L35218`, `Parse-TupleExprElems-Single@L35236`, `Parse-TupleExprElems-Many@L35254`, `Postfix-TupleIndex@L35272`
- `def.TupleTypeAst@L35290`, `def.TupleExpressionAst@L35306`, `WF-Tuple@L35337`, `T-Tuple-Unit@L35355`, `T-Tuple@L35372`, `T-Tuple-Index@L35390`, `T-Tuple-Index-Perm@L35408`, `P-Tuple-Index@L35426`
- `P-Tuple-Index-Perm@L35444`, `def.ConstTupleIndex@L35462`, `TupleIndex-NonConst@L35476`, `TupleIndex-OOB@L35494`, `TupleAccess-NotTuple@L35512`, `req.TuplePatternRulesOwnership@L35530`, `EvalSigma-Tuple@L35560`, `EvalSigma-Tuple-Ctrl@L35578`
- `EvalSigma-TupleAccess@L35596`, `EvalSigma-TupleAccess-Ctrl@L35614`, `Layout-Tuple-Empty@L35648`, `Layout-Tuple-Cons@L35665`, `Size-Tuple@L35683`, `Align-Tuple@L35701`, `Layout-Tuple@L35719`, `Lower-Expr-Tuple@L35751`
- `diagnostics.Tuples@L35769`

#### `checker.types.arrays`

Count: 34 total; 34 required; 0 recommended; 0 informative. Ledger line span: L35530-L36185.

- `Parse-Array-Type@L35817`, `Parse-Array-Segment-Elem@L35835`, `Parse-Array-Segment-Repeat@L35853`, `Parse-Array-Segment-List-Empty@L35871`, `Parse-Array-Segment-List-Single@L35888`, `Parse-Array-Segment-List-Comma@L35906`, `Parse-Array-Literal@L35924`, `Postfix-Index@L35942`
- `def.ArrayAstForms@L35960`, `req.IndexAccessArrayOwnership@L35978`, `def.ConstIndex@L35992`, `WF-Array@L36008`, `def.ArraySegmentLength@L36026`, `T-Array-Literal-Segments@L36041`, `T-Index-Array@L36067`, `T-Index-Array-Dynamic@L36085`
- `T-Index-Array-Perm@L36103`, `T-Index-Array-Perm-Dynamic@L36121`, `P-Index-Array@L36139`, `P-Index-Array-Perm@L36157`, `P-Index-Array-Dynamic@L36175`, `P-Index-Array-Perm-Dynamic@L36193`, `Index-Array-NonConst-Err@L36211`, `Index-Array-OOB-Err@L36229`
- `Index-Array-NonUsize@L36247`, `EvalSigma-Array@L36298`, `EvalSigma-Array-Ctrl@L36316`, `EvalSigma-Index@L36334`, `EvalSigma-Index-OOB@L36352`, `Size-Array@L36372`, `Align-Array@L36390`, `Layout-Array@L36408`
- `Lower-Expr-Array@L36454`, `req.ArrayDiagnosticOwnership@L36472`
- `Parse-Array-Type@L35817`, `Parse-Array-Segment-Elem@L35835`, `Parse-Array-Segment-Repeat@L35853`, `Parse-Array-Segment-List-Empty@L35871`, `Parse-Array-Segment-List-Single@L35888`, `Parse-Array-Segment-List-Comma@L35906`, `Parse-Array-Literal@L35924`, `Postfix-Index@L35942`
- `def.ArrayAstForms@L35960`, `req.IndexAccessArrayOwnership@L35978`, `def.ConstIndex@L35992`, `WF-Array@L36008`, `def.ArraySegmentLength@L36026`, `T-Array-Literal-Segments@L36041`, `T-Index-Array@L36067`, `T-Index-Array-Dynamic@L36085`
- `T-Index-Array-Perm@L36103`, `T-Index-Array-Perm-Dynamic@L36121`, `P-Index-Array@L36139`, `P-Index-Array-Perm@L36157`, `P-Index-Array-Dynamic@L36175`, `P-Index-Array-Perm-Dynamic@L36193`, `Index-Array-NonConst-Err@L36211`, `Index-Array-OOB-Err@L36229`
- `Index-Array-NonUsize@L36247`, `EvalSigma-Array@L36298`, `EvalSigma-Array-Ctrl@L36316`, `EvalSigma-Index@L36334`, `EvalSigma-Index-OOB@L36352`, `Size-Array@L36372`, `Align-Array@L36390`, `Layout-Array@L36408`
- `Lower-Expr-Array@L36454`, `req.ArrayDiagnosticOwnership@L36472`

#### `checker.types.slices`

Count: 28 total; 28 required; 0 recommended; 0 informative. Ledger line span: L36224-L36756.

- `req.ArrayToSliceCoercionSemantic@L36511`, `Parse-Slice-Type@L36527`, `req.IndexAccessSliceOwnership@L36545`, `def.TypeSliceAst@L36559`, `req.IndexAccessSliceExpressionSemantics@L36575`, `def.RangeIndexType@L36589`, `WF-Slice@L36605`, `T-Index-Slice@L36623`
- `T-Index-Slice-Perm@L36641`, `T-Slice-From-Array@L36659`, `T-Slice-From-Array-Perm@L36677`, `T-Slice-From-Slice@L36695`, `T-Slice-From-Slice-Perm@L36713`, `P-Index-Slice@L36731`, `P-Index-Slice-Perm@L36749`, `P-Slice-From-Array@L36767`
- `P-Slice-From-Array-Perm@L36785`, `P-Slice-From-Slice@L36803`, `P-Slice-From-Slice-Perm@L36821`, `Coerce-Array-Slice@L36839`, `Index-NonIndexable@L36857`, `EvalSigma-Index-Range@L36906`, `EvalSigma-Index-Range-OOB@L36924`, `Size-Slice@L36944`
- `Align-Slice@L36961`, `Layout-Slice@L36978`, `Index-Slice-NonUsize@L37025`, `req.SliceDiagnosticOwnership@L37043`
- `req.ArrayToSliceCoercionSemantic@L36511`, `Parse-Slice-Type@L36527`, `req.IndexAccessSliceOwnership@L36545`, `def.TypeSliceAst@L36559`, `req.IndexAccessSliceExpressionSemantics@L36575`, `def.RangeIndexType@L36589`, `WF-Slice@L36605`, `T-Index-Slice@L36623`
- `T-Index-Slice-Perm@L36641`, `T-Slice-From-Array@L36659`, `T-Slice-From-Array-Perm@L36677`, `T-Slice-From-Slice@L36695`, `T-Slice-From-Slice-Perm@L36713`, `P-Index-Slice@L36731`, `P-Index-Slice-Perm@L36749`, `P-Slice-From-Array@L36767`
- `P-Slice-From-Array-Perm@L36785`, `P-Slice-From-Slice@L36803`, `P-Slice-From-Slice-Perm@L36821`, `Coerce-Array-Slice@L36839`, `Index-NonIndexable@L36857`, `EvalSigma-Index-Range@L36906`, `EvalSigma-Index-Range-OOB@L36924`, `Size-Slice@L36944`
- `Align-Slice@L36961`, `Layout-Slice@L36978`, `Index-Slice-NonUsize@L37025`, `req.SliceDiagnosticOwnership@L37043`

#### `checker.types.ranges`

Count: 55 total; 55 required; 0 recommended; 0 informative. Ledger line span: L36813-L37833.

- `Parse-Range-To@L37100`, `Parse-Range-ToInc@L37118`, `Parse-Range-Full@L37136`, `Parse-Range-Lhs@L37154`, `Parse-RangeTail-None@L37172`, `Parse-RangeTail-From@L37190`, `Parse-RangeTail-Exclusive@L37208`, `Parse-RangeTail-Inclusive@L37226`
- `def.RangeSurfaceTypeElaboration@L37244`, `def.RangeTypeAst@L37265`, `def.RangeExprAst@L37281`, `def.IsRangeType@L37295`, `def.RangeFullExprType@L37309`, `def.RangeToExprType@L37325`, `def.RangeToInclusiveExprType@L37339`, `def.RangeFromExprType@L37353`
- `def.RangeExclusiveExprType@L37367`, `def.RangeInclusiveExprType@L37381`, `T-Range-Lift@L37395`, `Range-Full@L37413`, `Range-To@L37430`, `Range-ToInclusive@L37448`, `Range-From@L37466`, `Range-Exclusive@L37484`
- `Range-Inclusive@L37502`, `req.RangePatternSemanticsOwnership@L37520`, `EvalSigma-Range@L37555`, `EvalSigma-Range-Ctrl@L37573`, `EvalSigma-Range-Ctrl-Hi@L37591`, `def.RangeInc@L37609`, `Lower-Range-Full@L37692`, `Lower-Range-To@L37709`
- `Lower-Range-ToInclusive@L37727`, `Lower-Range-From@L37745`, `Lower-Range-Inclusive@L37763`, `Lower-Range-Exclusive@L37781`, `Size-Range@L37799`, `Align-Range@L37817`, `Layout-Range@L37835`, `Size-RangeInclusive@L37853`
- `Align-RangeInclusive@L37871`, `Layout-RangeInclusive@L37889`, `Size-RangeFrom@L37907`, `Align-RangeFrom@L37925`, `Layout-RangeFrom@L37943`, `Size-RangeTo@L37961`, `Align-RangeTo@L37979`, `Layout-RangeTo@L37997`
- `Size-RangeToInclusive@L38015`, `Align-RangeToInclusive@L38033`, `Layout-RangeToInclusive@L38051`, `Size-RangeFull@L38069`, `Align-RangeFull@L38086`, `Layout-RangeFull@L38103`, `req.RangeDiagnosticOwnership@L38120`
- `Parse-Range-To@L37100`, `Parse-Range-ToInc@L37118`, `Parse-Range-Full@L37136`, `Parse-Range-Lhs@L37154`, `Parse-RangeTail-None@L37172`, `Parse-RangeTail-From@L37190`, `Parse-RangeTail-Exclusive@L37208`, `Parse-RangeTail-Inclusive@L37226`
- `def.RangeSurfaceTypeElaboration@L37244`, `def.RangeTypeAst@L37265`, `def.RangeExprAst@L37281`, `def.IsRangeType@L37295`, `def.RangeFullExprType@L37309`, `def.RangeToExprType@L37325`, `def.RangeToInclusiveExprType@L37339`, `def.RangeFromExprType@L37353`
- `def.RangeExclusiveExprType@L37367`, `def.RangeInclusiveExprType@L37381`, `T-Range-Lift@L37395`, `Range-Full@L37413`, `Range-To@L37430`, `Range-ToInclusive@L37448`, `Range-From@L37466`, `Range-Exclusive@L37484`
- `Range-Inclusive@L37502`, `req.RangePatternSemanticsOwnership@L37520`, `EvalSigma-Range@L37555`, `EvalSigma-Range-Ctrl@L37573`, `EvalSigma-Range-Ctrl-Hi@L37591`, `def.RangeInc@L37609`, `Lower-Range-Full@L37692`, `Lower-Range-To@L37709`
- `Lower-Range-ToInclusive@L37727`, `Lower-Range-From@L37745`, `Lower-Range-Inclusive@L37763`, `Lower-Range-Exclusive@L37781`, `Size-Range@L37799`, `Align-Range@L37817`, `Layout-Range@L37835`, `Size-RangeInclusive@L37853`
- `Align-RangeInclusive@L37871`, `Layout-RangeInclusive@L37889`, `Size-RangeFrom@L37907`, `Align-RangeFrom@L37925`, `Layout-RangeFrom@L37943`, `Size-RangeTo@L37961`, `Align-RangeTo@L37979`, `Layout-RangeTo@L37997`
- `Size-RangeToInclusive@L38015`, `Align-RangeToInclusive@L38033`, `Layout-RangeToInclusive@L38051`, `Size-RangeFull@L38069`, `Align-RangeFull@L38086`, `Layout-RangeFull@L38103`, `req.RangeDiagnosticOwnership@L38120`

#### `spec.modal-special`

Count: 386 total; 386 required; 0 recommended; 0 informative. Ledger line span: L40979-L46752.

- `grammar.ModalDeclarations.Syntax@L41266`, `rule.13.Parse-Modal@L41288`, `rule.13.Parse-ModalBody@L41304`, `rule.13.Parse-StateBlock@L41320`, `def.ModalTypeRef.Parser@L41338`, `rule.13.Parse-Modal-State-Type@L41350`, `rule.13.Parse-Record-Literal-ModalState@L41366`, `req.ModalStateMemberDispatch@L41382`
- `def.ModalDeclAst@L41396`, `def.StateBlockAst@L41409`, `def.StateMemberAst@L41421`, `def.ModalRefAst@L41437`, `def.TypeRefModalStateAst@L41449`, `def.ModalStateAccessors@L41461`, `def.ModalStatePayload@L41478`, `def.BuiltinModalSet@L41490`
- `def.ModalPath@L41502`, `def.ModalSelfRef@L41515`, `def.ModalSelfTypes@L41530`, `def.ModalRefAccessors@L41543`, `def.ModalDeclOf@L41560`, `def.ModalRefSubst@L41572`, `def.ModalPayloadSubstitution@L41584`, `def.PayloadMap@L41596`
- `def.ModalPayloadMap@L41610`, `rule.13.WF-Modal-Payload@L41626`, `rule.13.Modal-Payload-DupField@L41642`, `rule.13.WF-ModalState@L41658`, `rule.13.WF-ModalState-ArgCount-Err@L41674`, `def.StateMemberVisOk@L41690`, `rule.13.WF-ModalDecl@L41702`, `rule.13.StateMemberVisOk-Err@L41718`
- `rule.13.Modal-WF@L41734`, `rule.13.Modal-NoStates-Err@L41750`, `rule.13.Modal-DupState-Err@L41766`, `rule.13.Modal-StateName-Err@L41782`, `rule.13.State-Specific-WF@L41798`, `def.ModalPayloadNames@L41814`, `rule.13.T-Modal-State-Intro@L41827`, `rule.13.Record-FileDir-Err@L41843`
- `def.RegionPayload@L41861`, `def.RegionProcSet@L41876`, `def.RegionNewScopedProcSig@L41888`, `def.RegionAllocProcSig@L41900`, `def.RegionResetUncheckedProcSig@L41912`, `def.RegionFreezeProcSig@L41924`, `def.RegionThawProcSig@L41936`, `def.RegionFreeUncheckedProcSig@L41948`
- `def.RegionProvenanceTypeHelpers@L41960`, `def.RegionNonBitcopy@L41974`, `req.RegionAllocProvenance@L41988`, `req.RegionInactiveDereferenceSemantics@L42000`, `req.RegionFreeAtScopeExit@L42012`, `rule.13.Region-Unchecked-Unsafe-Err@L42024`, `def.CancelTokenTypePresence@L42040`, `def.CancelTokenPayload@L42053`
- `def.CancelTokenMembersAndDecl@L42066`, `def.CancelTokenTypeBinding@L42088`, `def.CancelTokenProcSignatures@L42100`, `def.SpawnedTypePresence@L42113`, `def.SpawnedPayload@L42126`, `def.SpawnedMembersAndDecl@L42142`, `def.SpawnedTypeBinding@L42162`, `def.TrackedTypePresence@L42174`
- `def.TrackedPayload@L42187`, `def.TrackedMembersAndDecl@L42203`, `def.TrackedTypeBinding@L42223`, `def.DirIterMembersAndDecl@L42235`, `def.FileMembersAndDecl@L42257`, `def.DirIterAndFileTypeBindings@L42294`, `req.AsyncModalDefinedInChapter21@L42307`, `def.ModalValRuntime@L42321`
- `def.RecordModalStateValueType@L42333`, `def.ModalValValueType@L42345`, `req.ModalRuntimeRepresentation@L42357`, `def.ModalDiscType@L42371`, `def.ModalStateLayoutMetrics@L42383`, `def.ModalSingleFieldPayload@L42396`, `def.ModalEmptyState@L42409`, `def.ModalPayloadState@L42421`
- `def.ModalNicheApplies@L42433`, `def.ModalStateValueBits@L42445`, `def.ModalEmptyStates@L42457`, `def.EmptyRecordVal@L42469`, `def.ModalNicheBits@L42481`, `def.ModalBits@L42493`, `def.ModalAlign@L42505`, `def.ModalSize@L42517`
- `def.ModalPayloadSize@L42529`, `def.ModalPayloadAlign@L42541`, `def.StateRecordBits@L42553`, `def.ModalPayloadBits@L42565`, `def.ModalLayoutJudgementSet@L42577`, `rule.13.Layout-Modal-Niche@L42589`, `rule.13.Layout-Modal-Tagged@L42605`, `rule.13.Size-Modal@L42621`
- `rule.13.Align-Modal@L42637`, `rule.13.Layout-Modal@L42653`, `rule.13.Size-ModalState@L42669`, `rule.13.Align-ModalState@L42685`, `rule.13.Layout-ModalState@L42701`, `def.ModalStateLayoutEquation@L42717`, `def.EmptyModalStateSizeEquation@L42729`, `def.ModalBaseLayoutEquation@L42741`
- `req.ModalTaggedPaddingZero@L42755`, `def.ModalTaggedBits@L42766`, `def.ModalRefValueBits@L42778`, `diag.ModalDeclarations@L42792`, `grammar.StateFields.Syntax@L42808`, `rule.13.Parse-StateMember-Field@L42824`, `def.StateFieldDeclAst@L42842`, `def.PayloadNameHelpers@L42854`
- `def.ModalFieldVisible@L42869`, `rule.13.T-Modal-Field@L42881`, `rule.13.T-Modal-Field-Perm@L42897`, `rule.13.Modal-Field-Missing@L42913`, `rule.13.Modal-Field-General-Err@L42929`, `rule.13.Modal-Field-NotVisible@L42945`, `req.StateFieldDynamic@L42963`, `req.StateFieldLowering@L42977`
- `diag.StateFields@L42991`, `grammar.StateMethods.Syntax@L43007`, `rule.13.Parse-StateMember-Method@L43024`, `req.StateMethodSignatureParser@L43040`, `def.StateMethodDeclAst@L43054`, `def.StateMethodCollections@L43066`, `def.StateMethodSig@L43079`, `def.LookupStateMethod@L43093`
- `def.StateMemberVisible@L43108`, `rule.13.StateMethod-Dup@L43120`, `rule.13.WF-State-Method@L43136`, `rule.13.T-Modal-Method@L43152`, `rule.13.Modal-Method-RecvPerm-Err@L43168`, `rule.13.Modal-Method-NotFound@L43184`, `rule.13.Modal-Method-NotVisible@L43200`, `rule.13.T-Modal-Method-Body@L43216`
- `def.StateMethodTarget@L43234`, `rule.13.ApplyMethodSigma@L43246`, `req.BuiltinStateMethodCalling@L43262`, `req.StateMethodLowering@L43276`, `diag.StateMethods@L43290`, `grammar.Transitions.Syntax@L43306`, `rule.13.Parse-StateMember-Transition@L43322`, `def.TransitionDeclAst@L43340`
- `def.TransitionCollections@L43352`, `def.LookupTransition@L43366`, `def.TransitionSig@L43379`, `rule.13.Transition-Dup@L43398`, `rule.13.StateMember-Name-Conflict@L43414`, `rule.13.WF-Transition@L43430`, `rule.13.Transition-Target-Err@L43446`, `rule.13.T-Modal-Transition@L43462`
- `rule.13.Transition-Source-Err@L43478`, `rule.13.Transition-NotVisible@L43494`, `rule.13.T-Modal-Transition-Body@L43510`, `rule.13.Transition-Body-Err@L43526`, `def.TransitionMethodTarget@L43544`, `req.TransitionRuntimeSemantics@L43556`, `def.IsTransition@L43568`, `def.TransitionTarget@L43580`
- `rule.13.ApplyTransitionSigma@L43592`, `def.ExtractReturnValue@L43610`, `def.ValidateModalState@L43623`, `req.TransitionLowering@L43637`, `diag.Transitions@L43651`, `grammar.ModalWidening.Syntax@L43667`, `rule.13.Parse-Unary-Widen@L43683`, `def.ModalWidening.AST@L43701`
- `def.ModalWideningThreshold@L43715`, `rule.13.T-Modal-Widen@L43727`, `rule.13.T-Modal-Widen-Perm@L43743`, `rule.13.Widen-AlreadyGeneral@L43759`, `rule.13.Widen-NonModal@L43775`, `def.NicheCompatible@L43791`, `rule.13.Chk-Subsumption-Modal-NonNiche@L43803`, `def.WidenWarnCond@L43819`
- `rule.13.Warn-Widen-LargePayload@L43831`, `rule.13.Warn-Widen-Ok@L43847`, `def.ModalWideningDynamic@L43865`, `req.ModalWideningLowering@L43879`, `def.ModalStateSizeBound@L43891`, `diag.ModalWidening@L43905`, `grammar.StringTypes.Syntax@L43921`, `rule.13.Parse-String-Type@L43938`
- `rule.13.Parse-StringState-None@L43956`, `rule.13.Parse-StringState-Managed@L43972`, `rule.13.Parse-StringState-View@L43988`, `def.TypeStringAst@L44006`, `def.StringStateSet@L44018`, `def.StringBuiltinTable@L44030`, `def.StringBuiltinSig@L44051`, `rule.13.WF-String@L44065`
- `rule.13.Sub-String-State@L44081`, `req.StringBuiltinsTyping@L44095`, `def.StringLiteralVal@L44109`, `def.StringBytesStoreDomains@L44121`, `def.ViewBytes@L44137`, `def.ByteSeqOf@L44149`, `def.ByteLen@L44164`, `def.StringValueTypes@L44176`
- `def.StringBytesJudgementSet@L44892`, `req.StringLiteralStorage@L44210`, `rule.13.StringFrom-Ok@L44224`, `rule.13.StringFrom-Err@L44240`, `rule.13.StringAsView-Ok@L44256`, `rule.13.StringToManaged-Ok@L44272`, `rule.13.StringToManaged-Err@L44288`, `rule.13.StringCloneWith-Ok@L44304`
- `rule.13.StringCloneWith-Err@L44320`, `rule.13.StringAppend-Ok@L44336`, `rule.13.StringAppend-Err@L44352`, `rule.13.StringLength@L44368`, `rule.13.StringIsEmpty@L44384`, `def.StringViewOf@L44400`, `def.StringRuntimeLength@L44414`, `def.StringManagedLoweringLayout@L44429`
- `def.StringViewLoweringLayout@L44443`, `rule.13.Size-String-Managed@L44457`, `rule.13.Align-String-Managed@L44473`, `rule.13.Layout-String-Managed@L44489`, `rule.13.Size-String-View@L44505`, `rule.13.Align-String-View@L44521`, `rule.13.Layout-String-View@L44537`, `rule.13.Size-String-Modal@L44553`
- `rule.13.Align-String-Modal@L44569`, `def.StringValueBits@L44585`, `def.DropManagedString@L44597`, `diag.StringTypes@L44611`, `grammar.BytesTypes.Syntax@L44627`, `rule.13.Parse-Bytes-Type@L44644`, `rule.13.Parse-BytesState-None@L44662`, `rule.13.Parse-BytesState-Managed@L44678`
- `rule.13.Parse-BytesState-View@L44694`, `def.TypeBytesAst@L44712`, `def.BytesStateSet@L44724`, `def.BytesBuiltinTable@L44736`, `def.StringBytesBuiltinTable@L44760`, `def.BytesBuiltinSig@L44772`, `def.StringBytesBuiltinSig@L44784`, `rule.13.WF-Bytes@L44799`
- `rule.13.Sub-Bytes-State@L44815`, `req.BytesBuiltinsTyping@L44829`, `def.SliceBytes@L44843`, `def.BytesValueTypes@L44855`, `def.BytesJudgementSet@L44869`, `def.StringBytesJudgementSet@L44892`, `rule.13.BytesWithCapacity-Ok@L44904`, `rule.13.BytesWithCapacity-Err@L44920`
- `rule.13.BytesFromSlice-Ok@L44936`, `rule.13.BytesFromSlice-Err@L44952`, `rule.13.BytesAsView-Ok@L44968`, `rule.13.BytesToManaged-Ok@L44984`, `rule.13.BytesToManaged-Err@L45000`, `rule.13.BytesView-Ok@L45016`, `rule.13.BytesViewString-Ok@L45032`, `rule.13.BytesAsSlice-Ok@L45048`
- `rule.13.BytesAppend-Ok@L45064`, `rule.13.BytesAppend-Err@L45080`, `rule.13.BytesLength@L45096`, `rule.13.BytesIsEmpty@L45112`, `def.BytesViewOf@L45128`, `def.BytesRuntimeLength@L45142`, `def.BytesViewConversions@L45155`, `def.BytesManagedLoweringLayout@L45170`
- `def.BytesViewLoweringLayout@L45184`, `rule.13.Size-Bytes-Managed@L45198`, `rule.13.Align-Bytes-Managed@L45214`, `rule.13.Layout-Bytes-Managed@L45230`, `rule.13.Size-Bytes-View@L45246`, `rule.13.Align-Bytes-View@L45262`, `rule.13.Layout-Bytes-View@L45278`, `rule.13.Size-Bytes-Modal@L45294`
- `rule.13.Align-Bytes-Modal@L45310`, `def.BytesValueBits@L45326`, `def.DropManagedBytes@L45338`, `diag.BytesTypes@L45352`, `grammar.SafePointerTypes.Syntax@L45368`, `rule.13.Parse-Safe-Pointer-Type-ShiftSplit@L45385`, `rule.13.Parse-Safe-Pointer-Type@L45401`, `rule.13.Parse-PtrState-None@L45419`
- `rule.13.Parse-PtrState-Valid@L45435`, `rule.13.Parse-PtrState-Null@L45451`, `rule.13.Parse-PtrState-Expired@L45467`, `def.PtrStateSet@L45485`, `def.SafePointerTypeForms@L45497`, `rule.13.WF-Ptr@L45512`, `def.SafePointerTraits@L45528`, `rule.13.Sub-Ptr-State@L45542`
- `def.SafePointerRuntimeConstructors@L45560`, `def.SafePointerValueType@L45574`, `def.PtrStateImmediate@L45586`, `def.PtrStateValid@L45599`, `def.PtrAddrJudgementSet@L45614`, `rule.13.ReadPtr-Safe@L45626`, `rule.13.WritePtr-Safe@L45642`, `rule.13.ReadPtr-Null@L45658`
- `rule.13.ReadPtr-Expired@L45674`, `rule.13.WritePtr-Null@L45690`, `rule.13.WritePtr-Expired@L45706`, `rule.13.Size-Ptr@L45724`, `rule.13.Align-Ptr@L45740`, `rule.13.Layout-Ptr@L45756`, `def.SafePointerSizeAlignEquations@L45772`, `def.PtrDiagRefs@L45784`
- `def.SafePointerNicheSet@L45796`, `def.SafePointerValidValue@L45809`, `def.SafePointerValueBits@L45824`, `diag.SafePointerTypes@L45841`, `grammar.RawPointerTypes.Syntax@L45857`, `rule.13.Parse-Raw-Pointer-Type@L45873`, `def.RawPointerTypes.AST@L45891`, `rule.13.WF-RawPtr@L45905`
- `rule.13.T-Deref-Raw@L45921`, `rule.13.P-Deref-Raw-Imm@L45937`, `rule.13.P-Deref-Raw-Mut@L45953`, `rule.13.Deref-Raw-Unsafe@L45969`, `def.RawPointerRuntimeValue@L45987`, `rule.13.ReadPtr-Raw@L45999`, `rule.13.WritePtr-Raw@L46015`, `rule.13.ReadPtr-Raw-Invalid@L46031`
- `rule.13.WritePtr-Raw-Imm@L46047`, `rule.13.WritePtr-Raw-Invalid@L46063`, `rule.13.Size-RawPtr@L46081`, `rule.13.Align-RawPtr@L46097`, `rule.13.Layout-RawPtr@L46113`, `def.RawPointerValidValue@L46129`, `def.RawPointerValueBits@L46141`, `req.RawPointerLowering@L46152`
- `diag.RawPointerTypes@L46166`, `grammar.FunctionTypes.Syntax@L46182`, `req.FunctionTypeTrailingComma@L46198`, `rule.13.Parse-Func-Type@L46212`, `rule.13.Parse-ParamType-Move@L46228`, `rule.13.Parse-ParamType-Plain@L46244`, `rule.13.Parse-ParamTypeList-Empty@L46260`, `rule.13.Parse-ParamTypeList-Cons@L46276`
- `rule.13.Parse-ParamTypeListTail-End@L46292`, `rule.13.Parse-ParamTypeListTail-TrailingComma@L46308`, `rule.13.Parse-ParamTypeListTail-Cons@L46324`, `def.FunctionTypes.AST@L46342`, `rule.13.WF-Func@L46356`, `rule.13.T-Equiv-Func@L46372`, `rule.13.Sub-Func@L46388`, `rule.13.T-Proc-As-Value@L46404`
- `diag.FunctionTypeCalls@L46420`, `def.FunctionRuntimeValue@L46434`, `rule.13.EvalSigma-Call-Proc@L46446`, `req.NamedProceduresFirstClass@L46462`, `rule.13.Size-Func@L46476`, `rule.13.Align-Func@L46492`, `rule.13.Layout-Func@L46508`, `req.FunctionTypeCallLowering@L46524`
- `diag.FunctionTypes@L46538`, `grammar.ClosureTypes.Syntax@L46554`, `req.ClosureParamUnionParentheses@L46571`, `rule.13.Parse-Closure-Type@L46585`, `rule.13.Parse-Closure-Type-Empty@L46601`, `rule.13.Parse-ClosureParamType-Grouped@L46617`, `rule.13.Parse-ClosureParamType-Plain@L46633`, `rule.13.Parse-ClosureParamTypeList-Empty@L46649`
- `rule.13.Parse-ClosureParamTypeList-Cons@L46665`, `rule.13.Parse-ClosureParamTypeListTail-End@L46681`, `rule.13.Parse-ClosureParamTypeListTail-TrailingComma@L46697`, `rule.13.Parse-ClosureParamTypeListTail-Comma@L46713`, `rule.13.Parse-ClosureDepsOpt-None@L46729`, `rule.13.Parse-ClosureDepsOpt-Some@L46745`, `rule.13.Parse-SharedDepList-Empty@L46761`, `rule.13.Parse-SharedDepList-Single@L46777`
- `rule.13.Parse-SharedDepList-Cons@L46793`, `rule.13.Parse-SharedDep@L46809`, `def.TypeClosureAst@L46827`, `def.ClosureDepsOpt@L46839`, `req.ClosureTypeOwnershipBoundaries@L46851`, `rule.13.WF-Closure@L46865`, `rule.13.T-Equiv-Closure@L46881`, `rule.13.Sub-Closure@L46897`
- `req.ClosureExpressionOwnership@L46913`, `def.ClosureRuntimeValue@L46927`, `req.ClosureOperationOwnership@L46939`, `def.ClosureLoweringRep@L46953`, `rule.13.Size-Closure@L46965`, `rule.13.Align-Closure@L46981`, `rule.13.Layout-Closure@L46997`, `req.ClosureLoweringOwnership@L47013`
- `diag.ClosureTypes@L47027`, `diagnostics.ModalPointerSupplement@L47039`
- `grammar.ModalDeclarations.Syntax@L41266`, `rule.13.Parse-Modal@L41288`, `rule.13.Parse-ModalBody@L41304`, `rule.13.Parse-StateBlock@L41320`, `def.ModalTypeRef.Parser@L41338`, `rule.13.Parse-Modal-State-Type@L41350`, `rule.13.Parse-Record-Literal-ModalState@L41366`, `req.ModalStateMemberDispatch@L41382`
- `def.ModalDeclAst@L41396`, `def.StateBlockAst@L41409`, `def.StateMemberAst@L41421`, `def.ModalRefAst@L41437`, `def.TypeRefModalStateAst@L41449`, `def.ModalStateAccessors@L41461`, `def.ModalStatePayload@L41478`, `def.BuiltinModalSet@L41490`
- `def.ModalPath@L41502`, `def.ModalSelfRef@L41515`, `def.ModalSelfTypes@L41530`, `def.ModalRefAccessors@L41543`, `def.ModalDeclOf@L41560`, `def.ModalRefSubst@L41572`, `def.ModalPayloadSubstitution@L41584`, `def.PayloadMap@L41596`
- `def.ModalPayloadMap@L41610`, `rule.13.WF-Modal-Payload@L41626`, `rule.13.Modal-Payload-DupField@L41642`, `rule.13.WF-ModalState@L41658`, `rule.13.WF-ModalState-ArgCount-Err@L41674`, `def.StateMemberVisOk@L41690`, `rule.13.WF-ModalDecl@L41702`, `rule.13.StateMemberVisOk-Err@L41718`
- `rule.13.Modal-WF@L41734`, `rule.13.Modal-NoStates-Err@L41750`, `rule.13.Modal-DupState-Err@L41766`, `rule.13.Modal-StateName-Err@L41782`, `rule.13.State-Specific-WF@L41798`, `def.ModalPayloadNames@L41814`, `rule.13.T-Modal-State-Intro@L41827`, `rule.13.Record-FileDir-Err@L41843`
- `def.RegionPayload@L41861`, `def.RegionProcSet@L41876`, `def.RegionNewScopedProcSig@L41888`, `def.RegionAllocProcSig@L41900`, `def.RegionResetUncheckedProcSig@L41912`, `def.RegionFreezeProcSig@L41924`, `def.RegionThawProcSig@L41936`, `def.RegionFreeUncheckedProcSig@L41948`
- `def.RegionProvenanceTypeHelpers@L41960`, `def.RegionNonBitcopy@L41974`, `req.RegionAllocProvenance@L41988`, `req.RegionInactiveDereferenceSemantics@L42000`, `req.RegionFreeAtScopeExit@L42012`, `rule.13.Region-Unchecked-Unsafe-Err@L42024`, `def.CancelTokenTypePresence@L42040`, `def.CancelTokenPayload@L42053`
- `def.CancelTokenMembersAndDecl@L42066`, `def.CancelTokenTypeBinding@L42088`, `def.CancelTokenProcSignatures@L42100`, `def.SpawnedTypePresence@L42113`, `def.SpawnedPayload@L42126`, `def.SpawnedMembersAndDecl@L42142`, `def.SpawnedTypeBinding@L42162`, `def.TrackedTypePresence@L42174`
- `def.TrackedPayload@L42187`, `def.TrackedMembersAndDecl@L42203`, `def.TrackedTypeBinding@L42223`, `def.DirIterMembersAndDecl@L42235`, `def.FileMembersAndDecl@L42257`, `def.DirIterAndFileTypeBindings@L42294`, `req.AsyncModalDefinedInChapter21@L42307`, `def.ModalValRuntime@L42321`
- `def.RecordModalStateValueType@L42333`, `def.ModalValValueType@L42345`, `req.ModalRuntimeRepresentation@L42357`, `def.ModalDiscType@L42371`, `def.ModalStateLayoutMetrics@L42383`, `def.ModalSingleFieldPayload@L42396`, `def.ModalEmptyState@L42409`, `def.ModalPayloadState@L42421`
- `def.ModalNicheApplies@L42433`, `def.ModalStateValueBits@L42445`, `def.ModalEmptyStates@L42457`, `def.EmptyRecordVal@L42469`, `def.ModalNicheBits@L42481`, `def.ModalBits@L42493`, `def.ModalAlign@L42505`, `def.ModalSize@L42517`
- `def.ModalPayloadSize@L42529`, `def.ModalPayloadAlign@L42541`, `def.StateRecordBits@L42553`, `def.ModalPayloadBits@L42565`, `def.ModalLayoutJudgementSet@L42577`, `rule.13.Layout-Modal-Niche@L42589`, `rule.13.Layout-Modal-Tagged@L42605`, `rule.13.Size-Modal@L42621`
- `rule.13.Align-Modal@L42637`, `rule.13.Layout-Modal@L42653`, `rule.13.Size-ModalState@L42669`, `rule.13.Align-ModalState@L42685`, `rule.13.Layout-ModalState@L42701`, `def.ModalStateLayoutEquation@L42717`, `def.EmptyModalStateSizeEquation@L42729`, `def.ModalBaseLayoutEquation@L42741`
- `req.ModalTaggedPaddingZero@L42755`, `def.ModalTaggedBits@L42766`, `def.ModalRefValueBits@L42778`, `diag.ModalDeclarations@L42792`, `grammar.StateFields.Syntax@L42808`, `rule.13.Parse-StateMember-Field@L42824`, `def.StateFieldDeclAst@L42842`, `def.PayloadNameHelpers@L42854`
- `def.ModalFieldVisible@L42869`, `rule.13.T-Modal-Field@L42881`, `rule.13.T-Modal-Field-Perm@L42897`, `rule.13.Modal-Field-Missing@L42913`, `rule.13.Modal-Field-General-Err@L42929`, `rule.13.Modal-Field-NotVisible@L42945`, `req.StateFieldDynamic@L42963`, `req.StateFieldLowering@L42977`
- `diag.StateFields@L42991`, `grammar.StateMethods.Syntax@L43007`, `rule.13.Parse-StateMember-Method@L43024`, `req.StateMethodSignatureParser@L43040`, `def.StateMethodDeclAst@L43054`, `def.StateMethodCollections@L43066`, `def.StateMethodSig@L43079`, `def.LookupStateMethod@L43093`
- `def.StateMemberVisible@L43108`, `rule.13.StateMethod-Dup@L43120`, `rule.13.WF-State-Method@L43136`, `rule.13.T-Modal-Method@L43152`, `rule.13.Modal-Method-RecvPerm-Err@L43168`, `rule.13.Modal-Method-NotFound@L43184`, `rule.13.Modal-Method-NotVisible@L43200`, `rule.13.T-Modal-Method-Body@L43216`
- `def.StateMethodTarget@L43234`, `rule.13.ApplyMethodSigma@L43246`, `req.BuiltinStateMethodCalling@L43262`, `req.StateMethodLowering@L43276`, `diag.StateMethods@L43290`, `grammar.Transitions.Syntax@L43306`, `rule.13.Parse-StateMember-Transition@L43322`, `def.TransitionDeclAst@L43340`
- `def.TransitionCollections@L43352`, `def.LookupTransition@L43366`, `def.TransitionSig@L43379`, `rule.13.Transition-Dup@L43398`, `rule.13.StateMember-Name-Conflict@L43414`, `rule.13.WF-Transition@L43430`, `rule.13.Transition-Target-Err@L43446`, `rule.13.T-Modal-Transition@L43462`
- `rule.13.Transition-Source-Err@L43478`, `rule.13.Transition-NotVisible@L43494`, `rule.13.T-Modal-Transition-Body@L43510`, `rule.13.Transition-Body-Err@L43526`, `def.TransitionMethodTarget@L43544`, `req.TransitionRuntimeSemantics@L43556`, `def.IsTransition@L43568`, `def.TransitionTarget@L43580`
- `rule.13.ApplyTransitionSigma@L43592`, `def.ExtractReturnValue@L43610`, `def.ValidateModalState@L43623`, `req.TransitionLowering@L43637`, `diag.Transitions@L43651`, `grammar.ModalWidening.Syntax@L43667`, `rule.13.Parse-Unary-Widen@L43683`, `def.ModalWidening.AST@L43701`
- `def.ModalWideningThreshold@L43715`, `rule.13.T-Modal-Widen@L43727`, `rule.13.T-Modal-Widen-Perm@L43743`, `rule.13.Widen-AlreadyGeneral@L43759`, `rule.13.Widen-NonModal@L43775`, `def.NicheCompatible@L43791`, `rule.13.Chk-Subsumption-Modal-NonNiche@L43803`, `def.WidenWarnCond@L43819`
- `rule.13.Warn-Widen-LargePayload@L43831`, `rule.13.Warn-Widen-Ok@L43847`, `def.ModalWideningDynamic@L43865`, `req.ModalWideningLowering@L43879`, `def.ModalStateSizeBound@L43891`, `diag.ModalWidening@L43905`, `grammar.StringTypes.Syntax@L43921`, `rule.13.Parse-String-Type@L43938`
- `rule.13.Parse-StringState-None@L43956`, `rule.13.Parse-StringState-Managed@L43972`, `rule.13.Parse-StringState-View@L43988`, `def.TypeStringAst@L44006`, `def.StringStateSet@L44018`, `def.StringBuiltinTable@L44030`, `def.StringBuiltinSig@L44051`, `rule.13.WF-String@L44065`
- `rule.13.Sub-String-State@L44081`, `req.StringBuiltinsTyping@L44095`, `def.StringLiteralVal@L44109`, `def.StringBytesStoreDomains@L44121`, `def.ViewBytes@L44137`, `def.ByteSeqOf@L44149`, `def.ByteLen@L44164`, `def.StringValueTypes@L44176`
- `def.StringBytesJudgementSet@L44892`, `req.StringLiteralStorage@L44210`, `rule.13.StringFrom-Ok@L44224`, `rule.13.StringFrom-Err@L44240`, `rule.13.StringAsView-Ok@L44256`, `rule.13.StringToManaged-Ok@L44272`, `rule.13.StringToManaged-Err@L44288`, `rule.13.StringCloneWith-Ok@L44304`
- `rule.13.StringCloneWith-Err@L44320`, `rule.13.StringAppend-Ok@L44336`, `rule.13.StringAppend-Err@L44352`, `rule.13.StringLength@L44368`, `rule.13.StringIsEmpty@L44384`, `def.StringViewOf@L44400`, `def.StringRuntimeLength@L44414`, `def.StringManagedLoweringLayout@L44429`
- `def.StringViewLoweringLayout@L44443`, `rule.13.Size-String-Managed@L44457`, `rule.13.Align-String-Managed@L44473`, `rule.13.Layout-String-Managed@L44489`, `rule.13.Size-String-View@L44505`, `rule.13.Align-String-View@L44521`, `rule.13.Layout-String-View@L44537`, `rule.13.Size-String-Modal@L44553`
- `rule.13.Align-String-Modal@L44569`, `def.StringValueBits@L44585`, `def.DropManagedString@L44597`, `diag.StringTypes@L44611`, `grammar.BytesTypes.Syntax@L44627`, `rule.13.Parse-Bytes-Type@L44644`, `rule.13.Parse-BytesState-None@L44662`, `rule.13.Parse-BytesState-Managed@L44678`
- `rule.13.Parse-BytesState-View@L44694`, `def.TypeBytesAst@L44712`, `def.BytesStateSet@L44724`, `def.BytesBuiltinTable@L44736`, `def.StringBytesBuiltinTable@L44760`, `def.BytesBuiltinSig@L44772`, `def.StringBytesBuiltinSig@L44784`, `rule.13.WF-Bytes@L44799`
- `rule.13.Sub-Bytes-State@L44815`, `req.BytesBuiltinsTyping@L44829`, `def.SliceBytes@L44843`, `def.BytesValueTypes@L44855`, `def.BytesJudgementSet@L44869`, `def.StringBytesJudgementSet@L44892`, `rule.13.BytesWithCapacity-Ok@L44904`, `rule.13.BytesWithCapacity-Err@L44920`
- `rule.13.BytesFromSlice-Ok@L44936`, `rule.13.BytesFromSlice-Err@L44952`, `rule.13.BytesAsView-Ok@L44968`, `rule.13.BytesToManaged-Ok@L44984`, `rule.13.BytesToManaged-Err@L45000`, `rule.13.BytesView-Ok@L45016`, `rule.13.BytesViewString-Ok@L45032`, `rule.13.BytesAsSlice-Ok@L45048`
- `rule.13.BytesAppend-Ok@L45064`, `rule.13.BytesAppend-Err@L45080`, `rule.13.BytesLength@L45096`, `rule.13.BytesIsEmpty@L45112`, `def.BytesViewOf@L45128`, `def.BytesRuntimeLength@L45142`, `def.BytesViewConversions@L45155`, `def.BytesManagedLoweringLayout@L45170`
- `def.BytesViewLoweringLayout@L45184`, `rule.13.Size-Bytes-Managed@L45198`, `rule.13.Align-Bytes-Managed@L45214`, `rule.13.Layout-Bytes-Managed@L45230`, `rule.13.Size-Bytes-View@L45246`, `rule.13.Align-Bytes-View@L45262`, `rule.13.Layout-Bytes-View@L45278`, `rule.13.Size-Bytes-Modal@L45294`
- `rule.13.Align-Bytes-Modal@L45310`, `def.BytesValueBits@L45326`, `def.DropManagedBytes@L45338`, `diag.BytesTypes@L45352`, `grammar.SafePointerTypes.Syntax@L45368`, `rule.13.Parse-Safe-Pointer-Type-ShiftSplit@L45385`, `rule.13.Parse-Safe-Pointer-Type@L45401`, `rule.13.Parse-PtrState-None@L45419`
- `rule.13.Parse-PtrState-Valid@L45435`, `rule.13.Parse-PtrState-Null@L45451`, `rule.13.Parse-PtrState-Expired@L45467`, `def.PtrStateSet@L45485`, `def.SafePointerTypeForms@L45497`, `rule.13.WF-Ptr@L45512`, `def.SafePointerTraits@L45528`, `rule.13.Sub-Ptr-State@L45542`
- `def.SafePointerRuntimeConstructors@L45560`, `def.SafePointerValueType@L45574`, `def.PtrStateImmediate@L45586`, `def.PtrStateValid@L45599`, `def.PtrAddrJudgementSet@L45614`, `rule.13.ReadPtr-Safe@L45626`, `rule.13.WritePtr-Safe@L45642`, `rule.13.ReadPtr-Null@L45658`
- `rule.13.ReadPtr-Expired@L45674`, `rule.13.WritePtr-Null@L45690`, `rule.13.WritePtr-Expired@L45706`, `rule.13.Size-Ptr@L45724`, `rule.13.Align-Ptr@L45740`, `rule.13.Layout-Ptr@L45756`, `def.SafePointerSizeAlignEquations@L45772`, `def.PtrDiagRefs@L45784`
- `def.SafePointerNicheSet@L45796`, `def.SafePointerValidValue@L45809`, `def.SafePointerValueBits@L45824`, `diag.SafePointerTypes@L45841`, `grammar.RawPointerTypes.Syntax@L45857`, `rule.13.Parse-Raw-Pointer-Type@L45873`, `def.RawPointerTypes.AST@L45891`, `rule.13.WF-RawPtr@L45905`
- `rule.13.T-Deref-Raw@L45921`, `rule.13.P-Deref-Raw-Imm@L45937`, `rule.13.P-Deref-Raw-Mut@L45953`, `rule.13.Deref-Raw-Unsafe@L45969`, `def.RawPointerRuntimeValue@L45987`, `rule.13.ReadPtr-Raw@L45999`, `rule.13.WritePtr-Raw@L46015`, `rule.13.ReadPtr-Raw-Invalid@L46031`
- `rule.13.WritePtr-Raw-Imm@L46047`, `rule.13.WritePtr-Raw-Invalid@L46063`, `rule.13.Size-RawPtr@L46081`, `rule.13.Align-RawPtr@L46097`, `rule.13.Layout-RawPtr@L46113`, `def.RawPointerValidValue@L46129`, `def.RawPointerValueBits@L46141`, `req.RawPointerLowering@L46152`
- `diag.RawPointerTypes@L46166`, `grammar.FunctionTypes.Syntax@L46182`, `req.FunctionTypeTrailingComma@L46198`, `rule.13.Parse-Func-Type@L46212`, `rule.13.Parse-ParamType-Move@L46228`, `rule.13.Parse-ParamType-Plain@L46244`, `rule.13.Parse-ParamTypeList-Empty@L46260`, `rule.13.Parse-ParamTypeList-Cons@L46276`
- `rule.13.Parse-ParamTypeListTail-End@L46292`, `rule.13.Parse-ParamTypeListTail-TrailingComma@L46308`, `rule.13.Parse-ParamTypeListTail-Cons@L46324`, `def.FunctionTypes.AST@L46342`, `rule.13.WF-Func@L46356`, `rule.13.T-Equiv-Func@L46372`, `rule.13.Sub-Func@L46388`, `rule.13.T-Proc-As-Value@L46404`
- `diag.FunctionTypeCalls@L46420`, `def.FunctionRuntimeValue@L46434`, `rule.13.EvalSigma-Call-Proc@L46446`, `req.NamedProceduresFirstClass@L46462`, `rule.13.Size-Func@L46476`, `rule.13.Align-Func@L46492`, `rule.13.Layout-Func@L46508`, `req.FunctionTypeCallLowering@L46524`
- `diag.FunctionTypes@L46538`, `grammar.ClosureTypes.Syntax@L46554`, `req.ClosureParamUnionParentheses@L46571`, `rule.13.Parse-Closure-Type@L46585`, `rule.13.Parse-Closure-Type-Empty@L46601`, `rule.13.Parse-ClosureParamType-Grouped@L46617`, `rule.13.Parse-ClosureParamType-Plain@L46633`, `rule.13.Parse-ClosureParamTypeList-Empty@L46649`
- `rule.13.Parse-ClosureParamTypeList-Cons@L46665`, `rule.13.Parse-ClosureParamTypeListTail-End@L46681`, `rule.13.Parse-ClosureParamTypeListTail-TrailingComma@L46697`, `rule.13.Parse-ClosureParamTypeListTail-Comma@L46713`, `rule.13.Parse-ClosureDepsOpt-None@L46729`, `rule.13.Parse-ClosureDepsOpt-Some@L46745`, `rule.13.Parse-SharedDepList-Empty@L46761`, `rule.13.Parse-SharedDepList-Single@L46777`
- `rule.13.Parse-SharedDepList-Cons@L46793`, `rule.13.Parse-SharedDep@L46809`, `def.TypeClosureAst@L46827`, `def.ClosureDepsOpt@L46839`, `req.ClosureTypeOwnershipBoundaries@L46851`, `rule.13.WF-Closure@L46865`, `rule.13.T-Equiv-Closure@L46881`, `rule.13.Sub-Closure@L46897`
- `req.ClosureExpressionOwnership@L46913`, `def.ClosureRuntimeValue@L46927`, `req.ClosureOperationOwnership@L46939`, `def.ClosureLoweringRep@L46953`, `rule.13.Size-Closure@L46965`, `rule.13.Align-Closure@L46981`, `rule.13.Layout-Closure@L46997`, `req.ClosureLoweringOwnership@L47013`
- `diag.ClosureTypes@L47027`, `diagnostics.ModalPointerSupplement@L47039`

#### `spec.abstraction-polymorphism`

Count: 328 total; 327 required; 0 recommended; 0 informative. Ledger line span: L46800-L51908.

- `grammar.GenericParamsAndArgsSyntax@L47087`, `req.GenericArgsTrailingComma@L47105`, `req.GenericParamInlineBoundsClassOnly@L47117`, `rule.14.Parse-GenericArgs@L47131`, `rule.14.Parse-GenericArgsOpt-None@L47147`, `rule.14.Parse-GenericArgsOpt-Yes@L47163`, `rule.14.Parse-GenericParamsOpt-None@L47179`, `rule.14.Parse-GenericParamsOpt-Yes@L47195`
- `rule.14.Parse-GenericParams@L47211`, `rule.14.Parse-TypeParamTail-End@L47227`, `rule.14.Parse-TypeParamTail-Cons@L47243`, `rule.14.Parse-TypeParam@L47259`, `rule.14.Parse-TypeBoundsOpt-None@L47275`, `rule.14.Parse-TypeBoundsOpt-Yes@L47291`, `rule.14.Parse-ClassBoundList-Cons@L47307`, `rule.14.Parse-ClassBoundListTail-End@L47323`
- `rule.14.Parse-ClassBoundListTail-Cons@L47339`, `rule.14.Parse-ClassBound@L47355`, `rule.14.Parse-TypeDefaultOpt-None@L47371`, `rule.14.Parse-TypeDefaultOpt-Yes@L47387`, `rule.14.Parse-PredicateClauseOpt-None@L47403`, `rule.14.Parse-PredicateClauseOpt-Yes@L47419`, `rule.14.Parse-PredicateReqList-Cons@L47435`, `rule.14.Parse-PredicateReqListTail-End@L47451`
- `rule.14.Parse-PredicateReqListTail-TrailingTerminator@L47467`, `rule.14.Parse-PredicateReqListTail-Cons@L47483`, `def.PredicateNameParserSet@L47499`, `rule.14.Parse-PredicateReq-Predicate@L47511`, `rule.14.Parse-PredicateReq-Err@L47527`, `def.VarianceSet@L47545`, `def.GenericParamAst@L47557`, `def.PredicateClauseAst@L47571`
- `def.GenericParamHelpers@L47585`, `def.GenericDefaultWellFormedness@L47604`, `rule.14.WF-Generic-Param@L47618`, `def.DefaultArgs@L47634`, `rule.14.PredicateReq-WF-Predicate@L47651`, `def.PredicateClauseWellFormedness@L47667`, `def.PredOk@L47679`, `rule.14.T-Constraint-Sat@L47694`
- `rule.14.PredicateReq-Predicate@L47710`, `def.PredicateClauseSubstitutionOk@L47726`, `req.GenericBoundsAndPredicatesConjunctive@L47738`, `conformance.GenericParamsNoRuntimeSemantics@L47752`, `conformance.GenericParamsLoweringInputsOnly@L47766`, `diag.GenericParametersAndArguments@L47780`, `grammar.GenericProceduresAndTypesSyntax@L47796`, `req.GenericParamsNominalOwnerChapters@L47812`
- `req.GenericDeclarationParsingDelegated@L47826`, `def.CallTypeArgsStart@L47838`, `rule.14.Postfix-Call-TypeArgs@L47850`, `def.GenericDeclarationAstExtensions@L47868`, `def.GenericApplyAst@L47885`, `def.GenericDeclarationAccessors@L47899`, `rule.14.WF-Generic-Proc@L47914`, `def.GenericCalleeProc@L47930`
- `def.GenericInferenceFreshArgs@L47944`, `def.InferTypeArgs@L47957`, `rule.14.GenericCallInference@L47975`, `rule.14.T-Generic-Call@L48006`, `rule.14.Generic-Call-ArgCount-Err@L48029`, `rule.14.WF-Path-Generic-Err@L48045`, `rule.14.WF-Apply@L48061`, `rule.14.WF-Apply-ArgCount-Err@L48077`
- `req.GenericCallInferenceElaboration@L48093`, `conformance.GenericInstantiationDynamicElaboration@L48107`, `conformance.GenericMonomorphicInstantiationsDistinct@L48119`, `def.MonomorphizationSpecialization@L48133`, `req.GenericProcedureCallLowering@L48147`, `req.GenericInstantiationIndependentLowering@L48159`, `req.GenericInfiniteMonomorphizationRejected@L48171`, `req.GenericInstantiationDepthLimit@L48183`
- `req.GenericNominalSizeAlignSubstitutedBody@L48195`, `diag.GenericProceduresAndTypes@L48209`, `grammar.ClassesSyntax@L48225`, `req.AssociatedTypeSyntaxCanonicalOwner@L48242`, `rule.14.Parse-Class@L48256`, `rule.14.Parse-Superclass-None@L48272`, `rule.14.Parse-Superclass-Yes@L48288`, `rule.14.Parse-SuperclassBounds-Cons@L48304`
- `rule.14.Parse-SuperclassBoundsTail-End@L48320`, `rule.14.Parse-SuperclassBoundsTail-Plus@L48336`, `rule.14.Parse-ClassBody@L48352`, `rule.14.Parse-ClassItemList-End@L48368`, `rule.14.Parse-ClassItemList-Cons@L48384`, `rule.14.Parse-ClassItem-Method@L48400`, `rule.14.Parse-ClassItem-Field@L48416`, `rule.14.Parse-ClassItem-AbstractState@L48432`
- `rule.14.Parse-ClassMethodBody-Concrete@L48448`, `rule.14.Parse-ClassMethodBody-Abstract@L48464`, `def.ClassDeclAst@L48482`, `def.ClassItemAst@L48495`, `def.ClassMethodAbstractConcretePredicates@L48512`, `def.ClassMemberCollections@L48525`, `def.ClassMethodReturnType@L48541`, `def.SelfVar@L48554`
- `def.DistinctDisjoint@L48568`, `rule.14.WF-ClassPath@L48581`, `rule.14.WF-ClassPath-Err@L48597`, `def.SubstSelf@L48613`, `def.ReceiverTypeHelpers@L48639`, `def.Supers@L48668`, `rule.14.T-Superclass@L48680`, `def.ClassLinearizationMergeHelpers@L48696`
- `rule.14.Lin-Base@L48716`, `rule.14.Merge-Empty@L48732`, `rule.14.Merge-Step@L48748`, `rule.14.Merge-Fail@L48764`, `rule.14.Lin-Ok@L48780`, `rule.14.Lin-Fail@L48796`, `rule.14.Superclass-Cycle@L48812`, `def.LinearizeHeadInvariant@L48828`
- `def.EffectiveClassMembers@L48840`, `def.FirstByName@L48853`, `rule.14.EffMethods-Conflict@L48870`, `def.FieldSig@L48886`, `def.FirstFieldByName@L48898`, `rule.14.EffFields-Conflict@L48915`, `def.SelfTypeClass@L48931`, `rule.14.WF-Class-Method@L48943`
- `rule.14.T-Class-Method-Body-Abstract@L48959`, `rule.14.T-Class-Method-Body@L48975`, `rule.14.WF-Class@L48991`, `conformance.ClassDeclarationsNoRuntimeActions@L49009`, `req.ClassMethodLowering@L49023`, `req.ClassDefaultMethodAndVtableOwnership@L49035`, `diag.Classes@L49049`, `grammar.ImplementationsSyntax@L49065`
- `req.NoStandaloneImplementationBlocks@L49080`, `rule.14.Parse-Implements-None@L49094`, `rule.14.Parse-Implements-Yes@L49110`, `rule.14.Parse-ClassList-Cons@L49126`, `rule.14.Parse-ClassListTail-End@L49142`, `rule.14.Parse-ClassListTail-Comma@L49158`, `def.ImplementsAccessor@L49176`, `req.SubtypeOperatorImplementationMeaning@L49188`
- `req.ImplementationsConcreteOwnerOnly@L49203`, `def.ImplementerFields@L49215`, `def.ImplementerMethods@L49228`, `def.MethodByName@L49241`, `def.ClassEffectiveTables@L49254`, `def.ImplementationOrphanRule@L49267`, `def.DefaultMethodPredicates@L49283`, `rule.14.Impl-Abstract-Method@L49296`
- `rule.14.Impl-Missing-Method@L49312`, `rule.14.Impl-AssocType-Missing@L49328`, `rule.14.Impl-Sig-Err@L49344`, `rule.14.Override-Abstract-Err@L49360`, `rule.14.Impl-Concrete-Default@L49376`, `rule.14.Impl-Concrete-Override@L49392`, `rule.14.Override-Missing-Err@L49408`, `rule.14.Impl-Sig-Err-Concrete@L49424`
- `rule.14.Override-NoConcrete@L49440`, `rule.14.Impl-Field@L49456`, `rule.14.Impl-Field-Missing@L49472`, `rule.14.Impl-Field-Type-Err@L49488`, `rule.14.Impl-Coherence-Err@L49504`, `rule.14.Impl-Orphan-Err@L49520`, `rule.14.WF-Impl@L49536`, `rule.14.ImplementationSubtypeRelation@L49552`
- `req.14.ModalClassImplementationRequiresModalType@L49565`, `req.14.DuplicateClassImplementationForbidden@L49578`, `req.14.ImplementationOrphanRequirement@L49591`, `req.14.ImplementationsNoAdditionalRuntimeState@L49606`, `req.14.ImplementationBodyLowering@L49621`, `diag.14.Implementations@L49636`, `grammar.14.AssociatedType@L49653`, `req.14.AssociatedTypeEqualsMeaning@L49668`
- `rule.14.Parse-ClassItem-AssociatedType@L49683`, `rule.14.Parse-AssocTypeOpt-None@L49699`, `rule.14.Parse-AssocTypeOpt-Yes@L49715`, `rule.14.Parse-AssocTypeDefaultOpt@L49731`, `rule.14.Parse-RecordMember-AssociatedType@L49747`, `def.14.AssociatedTypeDeclAst@L49765`, `def.14.AssociatedTypeAstMembership@L49778`, `def.14.AssociatedTypeClassAbstractDefaulted@L49792`
- `def.14.AssocTypeItemsAndNames@L49805`, `def.14.AssocTypeDefault@L49819`, `def.14.ImplAssocType@L49833`, `def.14.AbstractAssociatedTypeNames@L49847`, `def.14.AssocTypeBinding@L49860`, `def.14.AssocTypeBindsPredicate@L49875`, `req.14.GenericParametersAssociatedTypesSupplySites@L49890`, `req.14.AssociatedTypeAbstractAndDefaultBinding@L49903`
- `req.14.ImplementationAssociatedTypeBoundForm@L49916`, `def.14.AssociatedTypeLookupOrder@L49929`, `rule.14.T-Alias-Equiv@L49946`, `req.14.AssociatedTypesNoRuntimeSemantics@L49964`, `req.14.AssociatedTypeErasureLowering@L49979`, `diag.14.AssociatedTypes@L49994`, `grammar.14.DynamicClassObjects@L50011`, `req.14.DynamicMethodCallSurfaceSyntax@L50027`
- `rule.14.Parse-Dynamic-Type@L50042`, `req.14.DynamicCastUsesOrdinaryCastParsing@L50058`, `def.14.TypeDynamicAst@L50073`, `def.14.DynamicClassLayoutFields@L50086`, `def.14.DynamicClassRuntimeValue@L50100`, `def.14.SelfOccurs@L50113`, `def.14.DynamicDispatchEligibility@L50140`, `rule.14.WF-Dynamic@L50158`
- `rule.14.WF-Dynamic-Err@L50174`, `rule.14.T-Equiv-Dynamic@L50190`, `rule.14.T-Dynamic-Form@L50206`, `rule.14.Dynamic-NonDispatchable@L50222`, `def.14.LookupMethod@L50238`, `rule.14.T-Dynamic-MethodCall@L50253`, `rule.14.LookupClassMethod-NotFound@L50269`, `req.14.DynamicDispatchDispatchableClassesOnly@L50285`
- `def.14.DynamicValueType@L50300`, `rule.14.Eval-Dynamic-Form@L50313`, `rule.14.Eval-Dynamic-Form-Ctrl@L50329`, `def.14.DynamicDispatchSelection@L50345`, `def.14.DynamicMethodTarget@L50360`, `rule.14.Layout-DynamicClass@L50375`, `rule.14.Size-DynamicClass@L50390`, `rule.14.Align-DynamicClass@L50406`
- `rule.14.ABI-Dynamic@L50422`, `def.14.DynamicValueBits@L50438`, `def.14.DynamicDispatchLoweringJudgements@L50451`, `rule.14.DispatchSym-Impl@L50465`, `rule.14.DispatchSym-Default-None@L50481`, `rule.14.DispatchSym-Default-Mismatch@L50497`, `rule.14.VTable-Order@L50513`, `rule.14.VSlot-Entry@L50529`
- `rule.14.Lower-Dynamic-Form@L50545`, `rule.14.Lower-DynCall@L50561`, `rule.14.EmitVTable-Decl@L50577`, `diag.14.DynamicClassObjects@L50595`, `grammar.14.OpaqueTypes@L50612`, `req.14.OpaqueTypesComposeAsTypeForms@L50627`, `rule.14.Parse-Opaque-Type@L50642`, `def.14.TypeOpaqueAst@L50660`
- `def.14.TypeOpaqueForm@L50673`, `rule.14.WF-Opaque@L50688`, `rule.14.WF-Opaque-Err@L50704`, `rule.14.T-Equiv-Opaque@L50720`, `rule.14.T-Opaque-Return@L50736`, `rule.14.T-Opaque-Project@L50752`, `req.14.OpaqueEquivalenceAndInterfaceExposure@L50768`, `req.14.OpaqueTypesNoRuntimeWrapper@L50783`
- `req.14.OpaqueTypesLowerAsConcrete@L50798`, `diag.14.OpaqueTypes@L50813`, `grammar.14.RefinementTypes@L50830`, `req.14.RefinementSelfBinding@L50847`, `rule.14.Parse-RefinementOpt-None@L50862`, `rule.14.Parse-RefinementOpt-Yes@L50878`, `rule.14.ParsePredicateExpr@L50894`, `def.14.TypeRefineAst@L50909`
- `def.14.TypeRefineForm@L50922`, `def.14.PredicateEquiv@L50935`, `rule.14.T-Equiv-Refine@L50950`, `rule.14.T-Equiv-Refine-Norm@L50966`, `rule.14.WF-Refine-Type@L50982`, `rule.14.T-Refine-Intro@L50998`, `rule.14.T-Refine-Elim@L51014`, `rule.14.RefinementSubtypeBase@L51030`
- `rule.14.RefinementSubtypeImplication@L51045`, `req.14.RefinementDecidablePredicateFragment@L51060`, `req.14.RefinementStaticDefaultDynamicFallback@L51073`, `req.14.RefinementRuntimeRepresentationAndPanic@L51088`, `rule.14.LLVMTy-Refine@L51103`, `req.14.RefinementRuntimeCheckLowering@L51119`, `diag.14.RefinementTypes@L51134`, `req.14.CapabilityClassSyntaxUsesOrdinaryClassAndDynamicSyntax@L51151`
- `req.14.CapabilityClassNoFeatureSpecificParser@L51166`, `def.14.CapClassSet@L51181`, `def.14.CapType@L51194`, `def.14.FileSystemInterface@L51207`, `def.14.NetworkInterface@L51238`, `def.14.HeapAllocatorInterface@L51254`, `def.14.FileKindDecl@L51272`, `def.14.IoErrorDecl@L51290`
- `def.14.DirEntryDecl@L51311`, `def.14.AllocationErrorDecl@L51329`, `def.14.ContextDecl@L51346`, `def.14.SystemDecl@L51372`, `def.14.ExecutionDomainSupportDecls@L51396`, `def.14.ReactorDecl@L51415`, `def.14.CapMethodSig@L51435`, `def.14.CapRecv@L51452`
- `def.14.CapabilityLoweringSupport@L51468`, `req.14.CapabilityClassesOrdinaryClasses@L51485`, `req.14.CapabilityClassesGenericBounds@L51498`, `req.14.CapabilityClassNamesReserved@L51511`, `req.14.HeapAllocatorRawCallsRequireUnsafe@L51524`, `rule.14.AllocRaw-Unsafe-Err@L51537`, `rule.14.DeallocRaw-Unsafe-Err@L51553`, `def.14.BuiltinTypesFS@L51569`
- `def.14.BuiltinDeclLookup@L51582`, `def.14.BuiltinTypeEnvironment@L51601`, `def.14.BuiltInContext@L51621`, `def.14.ContextBundleFieldType@L51634`, `def.14.ContextBundleType@L51654`, `def.14.ContextBundleFieldValue@L51668`, `def.14.ContextDomainValue@L51688`, `def.14.ContextBundleBuild@L51701`
- `def.14.AllocErrorVal@L51717`, `req.14.CapabilityClassesUseDynamicDispatchModel@L51732`, `req.14.CapabilityBuiltinMethodLowering@L51747`, `diag.14.CapabilityClasses@L51762`, `req.14.FoundationalClassesSyntaxAndReservedNames@L51779`, `req.14.FoundationalClassesNoFeatureSpecificParser@L51794`, `def.14.FoundationalClassName@L51809`, `def.14.FoundationalJudgements@L51822`
- `def.14.HasCloneDropMethod@L51838`, `def.14.CloneDropTypePredicates@L51852`, `def.14.FoundationalImplementationPredicates@L51866`, `req.14.FoundationalBoundsIntrinsicSatisfaction@L51886`, `rule.14.BitcopyDrop-Ok@L51899`, `rule.14.BitcopyDrop-Conflict@L51915`, `def.14.BitcopyType@L51931`, `def.14.BitcopyTypeCore@L51944`
- `def.14.BuiltinBitcopyType@L51967`, `def.14.BuiltinDropCloneType@L51998`, `def.14.BuiltinFoundationalClassSignatures@L52012`, `req.14.EqLaws@L52031`, `req.14.HashRequiresEqAndEqualValuesHashEqual@L52044`, `req.14.IteratorNextContract@L52057`, `req.14.StepPartialInverseContract@L52070`, `req.14.DropCloneDynamicSemantics@L52085`
- `req.14.HasherDynamicSemantics@L52098`, `req.14.IntegerStepDynamicSemantics@L52111`, `req.14.CharStepDynamicSemantics@L52124`, `req.14.FoundationalIntrinsicCallLowering@L52139`, `req.14.FoundationalPredicatesNoSeparateRepresentation@L52152`, `diag.14.FoundationalClasses@L52167`, `diag.14.RefinementPolymorphismDiagnosticsOwnership@L52182`, `diag-table.14.RefinementPolymorphismDiagnostics@L52195`
- `grammar.GenericParamsAndArgsSyntax@L47087`, `req.GenericArgsTrailingComma@L47105`, `req.GenericParamInlineBoundsClassOnly@L47117`, `rule.14.Parse-GenericArgs@L47131`, `rule.14.Parse-GenericArgsOpt-None@L47147`, `rule.14.Parse-GenericArgsOpt-Yes@L47163`, `rule.14.Parse-GenericParamsOpt-None@L47179`, `rule.14.Parse-GenericParamsOpt-Yes@L47195`
- `rule.14.Parse-GenericParams@L47211`, `rule.14.Parse-TypeParamTail-End@L47227`, `rule.14.Parse-TypeParamTail-Cons@L47243`, `rule.14.Parse-TypeParam@L47259`, `rule.14.Parse-TypeBoundsOpt-None@L47275`, `rule.14.Parse-TypeBoundsOpt-Yes@L47291`, `rule.14.Parse-ClassBoundList-Cons@L47307`, `rule.14.Parse-ClassBoundListTail-End@L47323`
- `rule.14.Parse-ClassBoundListTail-Cons@L47339`, `rule.14.Parse-ClassBound@L47355`, `rule.14.Parse-TypeDefaultOpt-None@L47371`, `rule.14.Parse-TypeDefaultOpt-Yes@L47387`, `rule.14.Parse-PredicateClauseOpt-None@L47403`, `rule.14.Parse-PredicateClauseOpt-Yes@L47419`, `rule.14.Parse-PredicateReqList-Cons@L47435`, `rule.14.Parse-PredicateReqListTail-End@L47451`
- `rule.14.Parse-PredicateReqListTail-TrailingTerminator@L47467`, `rule.14.Parse-PredicateReqListTail-Cons@L47483`, `def.PredicateNameParserSet@L47499`, `rule.14.Parse-PredicateReq-Predicate@L47511`, `rule.14.Parse-PredicateReq-Err@L47527`, `def.VarianceSet@L47545`, `def.GenericParamAst@L47557`, `def.PredicateClauseAst@L47571`
- `def.GenericParamHelpers@L47585`, `def.GenericDefaultWellFormedness@L47604`, `rule.14.WF-Generic-Param@L47618`, `def.DefaultArgs@L47634`, `rule.14.PredicateReq-WF-Predicate@L47651`, `def.PredicateClauseWellFormedness@L47667`, `def.PredOk@L47679`, `rule.14.T-Constraint-Sat@L47694`
- `rule.14.PredicateReq-Predicate@L47710`, `def.PredicateClauseSubstitutionOk@L47726`, `req.GenericBoundsAndPredicatesConjunctive@L47738`, `conformance.GenericParamsNoRuntimeSemantics@L47752`, `conformance.GenericParamsLoweringInputsOnly@L47766`, `diag.GenericParametersAndArguments@L47780`, `grammar.GenericProceduresAndTypesSyntax@L47796`, `req.GenericParamsNominalOwnerChapters@L47812`
- `req.GenericDeclarationParsingDelegated@L47826`, `def.CallTypeArgsStart@L47838`, `rule.14.Postfix-Call-TypeArgs@L47850`, `def.GenericDeclarationAstExtensions@L47868`, `def.GenericApplyAst@L47885`, `def.GenericDeclarationAccessors@L47899`, `rule.14.WF-Generic-Proc@L47914`, `def.GenericCalleeProc@L47930`
- `def.GenericInferenceFreshArgs@L47944`, `def.InferTypeArgs@L47957`, `rule.14.GenericCallInference@L47975`, `rule.14.T-Generic-Call@L48006`, `rule.14.Generic-Call-ArgCount-Err@L48029`, `rule.14.WF-Path-Generic-Err@L48045`, `rule.14.WF-Apply@L48061`, `rule.14.WF-Apply-ArgCount-Err@L48077`
- `req.GenericCallInferenceElaboration@L48093`, `conformance.GenericInstantiationDynamicElaboration@L48107`, `conformance.GenericMonomorphicInstantiationsDistinct@L48119`, `def.MonomorphizationSpecialization@L48133`, `req.GenericProcedureCallLowering@L48147`, `req.GenericInstantiationIndependentLowering@L48159`, `req.GenericInfiniteMonomorphizationRejected@L48171`, `req.GenericInstantiationDepthLimit@L48183`
- `req.GenericNominalSizeAlignSubstitutedBody@L48195`, `diag.GenericProceduresAndTypes@L48209`, `grammar.ClassesSyntax@L48225`, `req.AssociatedTypeSyntaxCanonicalOwner@L48242`, `rule.14.Parse-Class@L48256`, `rule.14.Parse-Superclass-None@L48272`, `rule.14.Parse-Superclass-Yes@L48288`, `rule.14.Parse-SuperclassBounds-Cons@L48304`
- `rule.14.Parse-SuperclassBoundsTail-End@L48320`, `rule.14.Parse-SuperclassBoundsTail-Plus@L48336`, `rule.14.Parse-ClassBody@L48352`, `rule.14.Parse-ClassItemList-End@L48368`, `rule.14.Parse-ClassItemList-Cons@L48384`, `rule.14.Parse-ClassItem-Method@L48400`, `rule.14.Parse-ClassItem-Field@L48416`, `rule.14.Parse-ClassItem-AbstractState@L48432`
- `rule.14.Parse-ClassMethodBody-Concrete@L48448`, `rule.14.Parse-ClassMethodBody-Abstract@L48464`, `def.ClassDeclAst@L48482`, `def.ClassItemAst@L48495`, `def.ClassMethodAbstractConcretePredicates@L48512`, `def.ClassMemberCollections@L48525`, `def.ClassMethodReturnType@L48541`, `def.SelfVar@L48554`
- `def.DistinctDisjoint@L48568`, `rule.14.WF-ClassPath@L48581`, `rule.14.WF-ClassPath-Err@L48597`, `def.SubstSelf@L48613`, `def.ReceiverTypeHelpers@L48639`, `def.Supers@L48668`, `rule.14.T-Superclass@L48680`, `def.ClassLinearizationMergeHelpers@L48696`
- `rule.14.Lin-Base@L48716`, `rule.14.Merge-Empty@L48732`, `rule.14.Merge-Step@L48748`, `rule.14.Merge-Fail@L48764`, `rule.14.Lin-Ok@L48780`, `rule.14.Lin-Fail@L48796`, `rule.14.Superclass-Cycle@L48812`, `def.LinearizeHeadInvariant@L48828`
- `def.EffectiveClassMembers@L48840`, `def.FirstByName@L48853`, `rule.14.EffMethods-Conflict@L48870`, `def.FieldSig@L48886`, `def.FirstFieldByName@L48898`, `rule.14.EffFields-Conflict@L48915`, `def.SelfTypeClass@L48931`, `rule.14.WF-Class-Method@L48943`
- `rule.14.T-Class-Method-Body-Abstract@L48959`, `rule.14.T-Class-Method-Body@L48975`, `rule.14.WF-Class@L48991`, `conformance.ClassDeclarationsNoRuntimeActions@L49009`, `req.ClassMethodLowering@L49023`, `req.ClassDefaultMethodAndVtableOwnership@L49035`, `diag.Classes@L49049`, `grammar.ImplementationsSyntax@L49065`
- `req.NoStandaloneImplementationBlocks@L49080`, `rule.14.Parse-Implements-None@L49094`, `rule.14.Parse-Implements-Yes@L49110`, `rule.14.Parse-ClassList-Cons@L49126`, `rule.14.Parse-ClassListTail-End@L49142`, `rule.14.Parse-ClassListTail-Comma@L49158`, `def.ImplementsAccessor@L49176`, `req.SubtypeOperatorImplementationMeaning@L49188`
- `req.ImplementationsConcreteOwnerOnly@L49203`, `def.ImplementerFields@L49215`, `def.ImplementerMethods@L49228`, `def.MethodByName@L49241`, `def.ClassEffectiveTables@L49254`, `def.ImplementationOrphanRule@L49267`, `def.DefaultMethodPredicates@L49283`, `rule.14.Impl-Abstract-Method@L49296`
- `rule.14.Impl-Missing-Method@L49312`, `rule.14.Impl-AssocType-Missing@L49328`, `rule.14.Impl-Sig-Err@L49344`, `rule.14.Override-Abstract-Err@L49360`, `rule.14.Impl-Concrete-Default@L49376`, `rule.14.Impl-Concrete-Override@L49392`, `rule.14.Override-Missing-Err@L49408`, `rule.14.Impl-Sig-Err-Concrete@L49424`
- `rule.14.Override-NoConcrete@L49440`, `rule.14.Impl-Field@L49456`, `rule.14.Impl-Field-Missing@L49472`, `rule.14.Impl-Field-Type-Err@L49488`, `rule.14.Impl-Coherence-Err@L49504`, `rule.14.Impl-Orphan-Err@L49520`, `rule.14.WF-Impl@L49536`, `rule.14.ImplementationSubtypeRelation@L49552`
- `req.14.ModalClassImplementationRequiresModalType@L49565`, `req.14.DuplicateClassImplementationForbidden@L49578`, `req.14.ImplementationOrphanRequirement@L49591`, `req.14.ImplementationsNoAdditionalRuntimeState@L49606`, `req.14.ImplementationBodyLowering@L49621`, `diag.14.Implementations@L49636`, `grammar.14.AssociatedType@L49653`, `req.14.AssociatedTypeEqualsMeaning@L49668`
- `rule.14.Parse-ClassItem-AssociatedType@L49683`, `rule.14.Parse-AssocTypeOpt-None@L49699`, `rule.14.Parse-AssocTypeOpt-Yes@L49715`, `rule.14.Parse-AssocTypeDefaultOpt@L49731`, `rule.14.Parse-RecordMember-AssociatedType@L49747`, `def.14.AssociatedTypeDeclAst@L49765`, `def.14.AssociatedTypeAstMembership@L49778`, `def.14.AssociatedTypeClassAbstractDefaulted@L49792`
- `def.14.AssocTypeItemsAndNames@L49805`, `def.14.AssocTypeDefault@L49819`, `def.14.ImplAssocType@L49833`, `def.14.AbstractAssociatedTypeNames@L49847`, `def.14.AssocTypeBinding@L49860`, `def.14.AssocTypeBindsPredicate@L49875`, `req.14.GenericParametersAssociatedTypesSupplySites@L49890`, `req.14.AssociatedTypeAbstractAndDefaultBinding@L49903`
- `req.14.ImplementationAssociatedTypeBoundForm@L49916`, `def.14.AssociatedTypeLookupOrder@L49929`, `rule.14.T-Alias-Equiv@L49946`, `req.14.AssociatedTypesNoRuntimeSemantics@L49964`, `req.14.AssociatedTypeErasureLowering@L49979`, `diag.14.AssociatedTypes@L49994`, `grammar.14.DynamicClassObjects@L50011`, `req.14.DynamicMethodCallSurfaceSyntax@L50027`
- `rule.14.Parse-Dynamic-Type@L50042`, `req.14.DynamicCastUsesOrdinaryCastParsing@L50058`, `def.14.TypeDynamicAst@L50073`, `def.14.DynamicClassLayoutFields@L50086`, `def.14.DynamicClassRuntimeValue@L50100`, `def.14.SelfOccurs@L50113`, `def.14.DynamicDispatchEligibility@L50140`, `rule.14.WF-Dynamic@L50158`
- `rule.14.WF-Dynamic-Err@L50174`, `rule.14.T-Equiv-Dynamic@L50190`, `rule.14.T-Dynamic-Form@L50206`, `rule.14.Dynamic-NonDispatchable@L50222`, `def.14.LookupMethod@L50238`, `rule.14.T-Dynamic-MethodCall@L50253`, `rule.14.LookupClassMethod-NotFound@L50269`, `req.14.DynamicDispatchDispatchableClassesOnly@L50285`
- `def.14.DynamicValueType@L50300`, `rule.14.Eval-Dynamic-Form@L50313`, `rule.14.Eval-Dynamic-Form-Ctrl@L50329`, `def.14.DynamicDispatchSelection@L50345`, `def.14.DynamicMethodTarget@L50360`, `rule.14.Layout-DynamicClass@L50375`, `rule.14.Size-DynamicClass@L50390`, `rule.14.Align-DynamicClass@L50406`
- `rule.14.ABI-Dynamic@L50422`, `def.14.DynamicValueBits@L50438`, `def.14.DynamicDispatchLoweringJudgements@L50451`, `rule.14.DispatchSym-Impl@L50465`, `rule.14.DispatchSym-Default-None@L50481`, `rule.14.DispatchSym-Default-Mismatch@L50497`, `rule.14.VTable-Order@L50513`, `rule.14.VSlot-Entry@L50529`
- `rule.14.Lower-Dynamic-Form@L50545`, `rule.14.Lower-DynCall@L50561`, `rule.14.EmitVTable-Decl@L50577`, `diag.14.DynamicClassObjects@L50595`, `grammar.14.OpaqueTypes@L50612`, `req.14.OpaqueTypesComposeAsTypeForms@L50627`, `rule.14.Parse-Opaque-Type@L50642`, `def.14.TypeOpaqueAst@L50660`
- `def.14.TypeOpaqueForm@L50673`, `rule.14.WF-Opaque@L50688`, `rule.14.WF-Opaque-Err@L50704`, `rule.14.T-Equiv-Opaque@L50720`, `rule.14.T-Opaque-Return@L50736`, `rule.14.T-Opaque-Project@L50752`, `req.14.OpaqueEquivalenceAndInterfaceExposure@L50768`, `req.14.OpaqueTypesNoRuntimeWrapper@L50783`
- `req.14.OpaqueTypesLowerAsConcrete@L50798`, `diag.14.OpaqueTypes@L50813`, `grammar.14.RefinementTypes@L50830`, `req.14.RefinementSelfBinding@L50847`, `rule.14.Parse-RefinementOpt-None@L50862`, `rule.14.Parse-RefinementOpt-Yes@L50878`, `rule.14.ParsePredicateExpr@L50894`, `def.14.TypeRefineAst@L50909`
- `def.14.TypeRefineForm@L50922`, `def.14.PredicateEquiv@L50935`, `rule.14.T-Equiv-Refine@L50950`, `rule.14.T-Equiv-Refine-Norm@L50966`, `rule.14.WF-Refine-Type@L50982`, `rule.14.T-Refine-Intro@L50998`, `rule.14.T-Refine-Elim@L51014`, `rule.14.RefinementSubtypeBase@L51030`
- `rule.14.RefinementSubtypeImplication@L51045`, `req.14.RefinementDecidablePredicateFragment@L51060`, `req.14.RefinementStaticDefaultDynamicFallback@L51073`, `req.14.RefinementRuntimeRepresentationAndPanic@L51088`, `rule.14.LLVMTy-Refine@L51103`, `req.14.RefinementRuntimeCheckLowering@L51119`, `diag.14.RefinementTypes@L51134`, `req.14.CapabilityClassSyntaxUsesOrdinaryClassAndDynamicSyntax@L51151`
- `req.14.CapabilityClassNoFeatureSpecificParser@L51166`, `def.14.CapClassSet@L51181`, `def.14.CapType@L51194`, `def.14.FileSystemInterface@L51207`, `def.14.NetworkInterface@L51238`, `def.14.HeapAllocatorInterface@L51254`, `def.14.FileKindDecl@L51272`, `def.14.IoErrorDecl@L51290`
- `def.14.DirEntryDecl@L51311`, `def.14.AllocationErrorDecl@L51329`, `def.14.ContextDecl@L51346`, `def.14.SystemDecl@L51372`, `def.14.ExecutionDomainSupportDecls@L51396`, `def.14.ReactorDecl@L51415`, `def.14.CapMethodSig@L51435`, `def.14.CapRecv@L51452`
- `def.14.CapabilityLoweringSupport@L51468`, `req.14.CapabilityClassesOrdinaryClasses@L51485`, `req.14.CapabilityClassesGenericBounds@L51498`, `req.14.CapabilityClassNamesReserved@L51511`, `req.14.HeapAllocatorRawCallsRequireUnsafe@L51524`, `rule.14.AllocRaw-Unsafe-Err@L51537`, `rule.14.DeallocRaw-Unsafe-Err@L51553`, `def.14.BuiltinTypesFS@L51569`
- `def.14.BuiltinDeclLookup@L51582`, `def.14.BuiltinTypeEnvironment@L51601`, `def.14.BuiltInContext@L51621`, `def.14.ContextBundleFieldType@L51634`, `def.14.ContextBundleType@L51654`, `def.14.ContextBundleFieldValue@L51668`, `def.14.ContextDomainValue@L51688`, `def.14.ContextBundleBuild@L51701`
- `def.14.AllocErrorVal@L51717`, `req.14.CapabilityClassesUseDynamicDispatchModel@L51732`, `req.14.CapabilityBuiltinMethodLowering@L51747`, `diag.14.CapabilityClasses@L51762`, `req.14.FoundationalClassesSyntaxAndReservedNames@L51779`, `req.14.FoundationalClassesNoFeatureSpecificParser@L51794`, `def.14.FoundationalClassName@L51809`, `def.14.FoundationalJudgements@L51822`
- `def.14.HasCloneDropMethod@L51838`, `def.14.CloneDropTypePredicates@L51852`, `def.14.FoundationalImplementationPredicates@L51866`, `req.14.FoundationalBoundsIntrinsicSatisfaction@L51886`, `rule.14.BitcopyDrop-Ok@L51899`, `rule.14.BitcopyDrop-Conflict@L51915`, `def.14.BitcopyType@L51931`, `def.14.BitcopyTypeCore@L51944`
- `def.14.BuiltinBitcopyType@L51967`, `def.14.BuiltinDropCloneType@L51998`, `def.14.BuiltinFoundationalClassSignatures@L52012`, `req.14.EqLaws@L52031`, `req.14.HashRequiresEqAndEqualValuesHashEqual@L52044`, `req.14.IteratorNextContract@L52057`, `req.14.StepPartialInverseContract@L52070`, `req.14.DropCloneDynamicSemantics@L52085`
- `req.14.HasherDynamicSemantics@L52098`, `req.14.IntegerStepDynamicSemantics@L52111`, `req.14.CharStepDynamicSemantics@L52124`, `req.14.FoundationalIntrinsicCallLowering@L52139`, `req.14.FoundationalPredicatesNoSeparateRepresentation@L52152`, `diag.14.FoundationalClasses@L52167`, `diag.14.RefinementPolymorphismDiagnosticsOwnership@L52182`, `diag-table.14.RefinementPolymorphismDiagnostics@L52195`

#### `spec.procedures-contracts`

Count: 283 total; 283 required; 0 recommended; 0 informative. Ledger line span: L51972-L56430.

- `grammar.15.ProcedureDeclarations@L52259`, `req.15.ExternProcedureDeclarationsOwnedByFFI@L52277`, `rule.15.Parse-Procedure@L52292`, `rule.15.Parse-Signature@L52308`, `rule.15.Parse-ParamList-Empty@L52324`, `rule.15.Parse-ParamList-Cons@L52340`, `rule.15.Parse-Param@L52356`, `rule.15.Parse-ParamMode-None@L52372`
- `rule.15.Parse-ParamMode-Move@L52388`, `rule.15.Parse-ParamTail-End@L52404`, `rule.15.Parse-ParamTail-TrailingComma@L52420`, `rule.15.Parse-ParamTail-Comma@L52436`, `rule.15.Parse-ReturnOpt-None@L52452`, `rule.15.Parse-ReturnOpt-Arrow@L52468`, `def.15.ProcedureDeclAst@L52486`, `def.15.ParamAst@L52499`
- `def.15.ParamNamesAndBinds@L52512`, `def.15.ProcReturn@L52526`, `def.15.BodyReturnType@L52541`, `def.15.ExplicitReturn@L52556`, `def.15.ReturnAnnOk@L52571`, `rule.15.WF-ProcedureDecl@L52584`, `def.15.DeclTyping@L52600`, `def.15.ProvBindCheck@L52615`
- `def.15.DeclTypingItem@L52628`, `rule.15.ProcedureDeclOkJudgement@L52650`, `rule.15.WF-ProcedureDecl-MissingReturnType@L52663`, `rule.15.WF-ProcBody-ExplicitReturn-Err@L52679`, `req.15.ExportedProcedureForeignCallableObligations@L52695`, `def.15.MainEntryPointDefinitions@L52708`, `rule.15.Main-Ok@L52729`, `rule.15.Main-Bypass-NonExecutable@L52745`
- `rule.15.Main-Multiple@L52761`, `rule.15.Main-Generic-Err@L52777`, `rule.15.Main-Signature-Err@L52793`, `rule.15.Main-Missing@L52809`, `def.15.MainDiagRefs@L52825`, `def.15.FuncValDefined@L52840`, `def.15.BindParams@L52853`, `def.15.ArgumentPassingJudgements@L52866`
- `def.15.CallJudgements@L52880`, `def.15.CallTargets@L52893`, `def.15.BuiltinProcedureParams@L52909`, `def.15.SynthParams@L52923`, `def.15.CalleeProc@L52936`, `def.15.CallParams@L52950`, `def.15.ReturnOut@L52966`, `rule.15.EvalArgsSigma-Empty@L52985`
- `rule.15.EvalArgsSigma-Cons-Move@L53000`, `rule.15.EvalArgsSigma-Cons-Ref@L53016`, `rule.15.EvalArgsSigma-Ctrl-Move@L53032`, `rule.15.EvalArgsSigma-Ctrl-Ref@L53048`, `rule.15.ApplyRegionProc-NewScoped@L53064`, `rule.15.ApplyRegionProc-Alloc@L53080`, `rule.15.ApplyRegionProc-Reset@L53096`, `rule.15.ApplyRegionProc-Freeze@L53112`
- `rule.15.ApplyRegionProc-Thaw@L53128`, `rule.15.ApplyRegionProc-Free@L53144`, `rule.15.ApplyCancelProc-New@L53160`, `rule.15.ApplyProcSigma@L53176`, `rule.15.EvalSigma-Call-Proc@L53192`, `rule.15.CG-Item-Procedure@L53210`, `req.15.MainProgramEntryHandlingOwnedByChapter24@L53226`, `diag.15.ProcedureDeclarations@L53241`
- `grammar.15.MethodsAndReceivers@L53258`, `req.15.ClassAndStateMethodsReuseReceiverForms@L53275`, `rule.15.Parse-MethodDefAfterVis@L53290`, `rule.15.Parse-Override-Yes@L53306`, `rule.15.Parse-Override-No@L53322`, `rule.15.Parse-MethodSignature@L53338`, `rule.15.Parse-StateMethodSignature-Receiver@L53354`, `rule.15.Parse-MethodParams-None@L53370`
- `rule.15.Parse-MethodParams-Comma@L53386`, `rule.15.Parse-Receiver-Short-Const@L53402`, `rule.15.Parse-Receiver-Short-Unique@L53418`, `rule.15.Parse-Receiver-Short-Shared@L53434`, `rule.15.Parse-Receiver-Explicit@L53450`, `def.15.MethodDeclAst@L53468`, `def.15.ReceiverAst@L53481`, `def.15.RecordFieldsMethodsAndSelf@L53496`
- `def.15.SelfType@L53511`, `def.15.RecvType@L53524`, `def.15.RecvMode@L53540`, `def.15.RecvPerm@L53554`, `def.15.MethodSignaturesAndParams@L53569`, `rule.15.Recv-Explicit@L53588`, `rule.15.Record-Method-RecvSelf-Err@L53604`, `rule.15.Recv-Const@L53620`
- `rule.15.Recv-Unique@L53635`, `rule.15.Recv-Shared@L53650`, `rule.15.WF-Record-Method@L53665`, `rule.15.T-Record-Method-Body@L53681`, `rule.15.WF-Record-Methods@L53697`, `rule.15.Record-Method-Dup@L53713`, `def.15.ArgsOkJudg@L53729`, `def.15.RecvBaseType@L53742`
- `rule.15.Args-Empty@L53755`, `rule.15.Args-Cons@L53770`, `rule.15.Args-Cons-Ref@L53786`, `def.15.RecvArgOk@L53802`, `rule.15.T-Record-MethodCall@L53815`, `req.15.OwnerSpecificReceiverRestrictionsReuseCommonForms@L53831`, `def.15.RecvArgMode@L53846`, `def.15.MethodOf@L53860`
- `def.15.RecvBase@L53875`, `def.15.RecvParams@L53888`, `rule.15.EvalRecvSigma-Move@L53903`, `rule.15.EvalRecvSigma-Ref-Dyn@L53919`, `rule.15.EvalRecvSigma-Ref-Dyn-Expired@L53935`, `rule.15.EvalRecvSigma-Ref@L53951`, `rule.15.EvalRecvSigma-Ctrl-Move@L53967`, `rule.15.EvalRecvSigma-Ctrl-Ref@L53983`
- `def.15.BindMethodParams@L53999`, `rule.15.ApplyMethodSigma-Prim@L54012`, `rule.15.ApplyMethodSigma@L54028`, `req.15.MethodsLowerAsProceduresWithReceiverFirst@L54046`, `rule.15.Mangle-Record-Method@L54059`, `rule.15.Mangle-Class-Method@L54075`, `rule.15.Mangle-State-Method@L54091`, `diag.15.MethodsAndReceivers@L54109`
- `req.15.OverloadingNoAdditionalSyntax@L54126`, `req.15.OverloadResolutionNotParserConcern@L54141`, `def.15.ClassDefaults@L54156`, `def.15.LookupMethod@L54169`, `rule.15.LookupMethod-NotFound@L54186`, `rule.15.LookupMethod-Ambig@L54202`, `req.15.FreeProcedureOverloadResolutionBeforeCallTyping@L54218`, `req.15.FreeCallOverloadResolutionAlgorithm@L54231`
- `req.15.DuplicateErasedOverloadSignaturesForbidden@L54253`, `req.15.NoRuntimeOverloadSearch@L54268`, `req.15.OverloadResolutionCompleteBeforeLowering@L54283`, `diag-table.15.Overloading@L54298`, `diag.15.MethodLookupDiagnostics@L54315`, `grammar.15.ContractClauses@L54332`, `req.15.ForeignContractStartDisambiguatesContracts@L54354`, `rule.15.Parse-ContractClauseOpt-None@L54367`
- `rule.15.Parse-ContractClauseOpt-Yes@L54383`, `rule.15.Parse-ContractBody-PostOnly@L54399`, `rule.15.Parse-ContractBody-PrePost@L54415`, `rule.15.Parse-ContractBody-PreOnly@L54431`, `def.15.ContractClauseAst@L54449`, `def.15.ContractOpt@L54462`, `rule.15.WF-Contract@L54477`, `def.15.ContractPurityJudgementIntro@L54494`
- `rule.15.Pure-Literal@L54507`, `rule.15.Pure-Ident@L54523`, `rule.15.Pure-Field@L54539`, `rule.15.Pure-Tuple-Access@L54555`, `rule.15.Pure-Index@L54571`, `rule.15.Pure-Unary@L54587`, `rule.15.Pure-Binary@L54603`, `def.15.PureOps@L54619`
- `rule.15.Pure-Cast@L54632`, `rule.15.Pure-If@L54648`, `rule.15.Pure-If-Is@L54664`, `rule.15.Pure-If-Is-No-Else@L54680`, `rule.15.Pure-If-Case@L54696`, `rule.15.Pure-If-Case-No-Else@L54712`, `rule.15.Pure-Block@L54728`, `rule.15.Pure-Tuple@L54744`
- `rule.15.Pure-Array@L54760`, `rule.15.Pure-Record@L54776`, `rule.15.Pure-Call-Builtin@L54792`, `rule.15.Pure-Call-Procedure@L54808`, `rule.15.Pure-Method-Const@L54824`, `rule.15.Pure-Comptime@L54840`, `def.15.ContractPurityHelperPredicates@L54856`, `req.15.ContractNeverPureForms@L54876`
- `def.15.PreconditionEvaluationContext@L54889`, `def.15.PostconditionEvaluationContext@L54902`, `req.15.ContractClausesNoIndependentRuntimeEffect@L54917`, `req.15.ContractClauseLoweringViaVerificationResults@L54932`, `diag.15.ContractClauses@L54947`, `req.15.PreconditionSyntaxDefinition@L54964`, `req.15.PreconditionsParsedByContractBody@L54979`, `def.15.PreconditionOf@L54994`
- `def.15.PreconditionProofContext@L55011`, `rule.15.Pre-Satisfied@L55027`, `def.15.PreconditionElisionRules@L55043`, `req.15.CallerResponsibleForPrecondition@L55063`, `req.15.PreconditionRuntimeEvaluationOrder@L55078`, `req.15.PreconditionCheckInsertionOwnedByVerificationLogic@L55093`, `diag.15.Preconditions@L55108`, `grammar.15.Postconditions@L55125`
- `rule.15.Parse-Contract-Result@L55143`, `rule.15.Parse-Contract-Entry@L55159`, `def.15.ContractIntrinsicAst@L55177`, `def.15.PostconditionOf@L55190`, `def.15.PostconditionProofContext@L55207`, `rule.15.Post-Valid@L55222`, `def.15.PostconditionElisionRules@L55238`, `req.15.ContractResultProperties@L55258`
- `rule.15.Result-Union-Type@L55275`, `rule.15.Result-Is-Predicate@L55291`, `rule.15.Result-Narrowing@L55307`, `rule.15.Propagate-Postcondition@L55323`, `rule.15.Result-Modal@L55339`, `rule.15.Result-Generic@L55355`, `rule.15.Result-Generic-Constraint@L55371`, `req.15.ContractEntryConstraints@L55387`
- `rule.15.Entry-Type@L55405`, `req.15.PostconditionResultRuntimeBinding@L55423`, `req.15.ContractEntryRuntimeCapture@L55436`, `def.15.EntryCaptureTiming@L55453`, `rule.15.EntryCapturePhase@L55473`, `def.15.EntryCaptureValue@L55490`, `req.15.PostconditionLoweringRepresentation@L55505`, `diag.15.Postconditions@L55520`
- `grammar.15.Invariants@L55537`, `rule.15.Parse-InvariantOpt-None@L55555`, `rule.15.Parse-InvariantOpt-Yes@L55571`, `rule.15.ParseLoopInvariantOpt@L55587`, `def.15.InvariantAst@L55602`, `def.15.TypeInvariantAstExtensions@L55616`, `def.15.LoopInvariantAstPreservation@L55631`, `def.15.TypeInvariantContext@L55646`
- `def.15.TypeInvariantEnforcementPoints@L55663`, `req.15.TypeInvariantsForbidPublicMutableFields@L55680`, `req.15.PrivateProceduresExemptFromTypeInvariantPreCall@L55693`, `def.15.LoopInvariantEnforcementPoints@L55706`, `req.15.LoopInvariantExitFact@L55723`, `req.15.InvariantVerificationModeRules@L55736`, `req.15.InvariantRuntimeChecks@L55751`, `req.15.InvariantLoweringViaVerificationLogic@L55766`
- `diag.15.Invariants@L55781`, `req.15.VerificationLogicNoSurfaceSyntax@L55798`, `req.15.VerificationLogicNotParserOwned@L55813`, `def.15.ContractKind@L55828`, `def.15.VerificationFact@L55841`, `def.15.CheckState@L55854`, `def.15.ContractCheck@L55867`, `def.15.DynamicScopeAndContext@L55882`
- `rule.15.Contract-Static-OK@L55903`, `rule.15.Contract-Static-Fail@L55919`, `rule.15.Contract-Dynamic-Elide@L55935`, `rule.15.Contract-Dynamic-Check@L55951`, `req.15.MandatoryProofTechniques@L55967`, `def.15.ProofContextAt@L55987`, `def.15.DecidablePredicates@L56008`, `rule.15.Ent-True@L56028`
- `rule.15.Ent-Fact@L56044`, `rule.15.Ent-And@L56060`, `rule.15.Ent-Or-L@L56076`, `rule.15.Ent-Or-R@L56092`, `rule.15.Ent-Linear@L56108`, `def.15.LinearIntegerEntailment@L56124`, `req.15.LinearEntailmentSoundAndComplete@L56147`, `def.15.StaticProofAt@L56160`
- `def.15.NegFact@L56173`, `req.15.VerificationFactsNoRuntimeRepresentation@L56195`, `rule.15.Fact-Dominate@L56212`, `req.15.FactGeneration@L56228`, `req.15.TypeNarrowingFromFacts@L56251`, `def.15.ContractEnvironments@L56266`, `rule.15.Check-True@L56283`, `rule.15.Check-False@L56299`
- `rule.15.Check-Panic@L56315`, `rule.15.Check-Ok@L56331`, `rule.15.Check-Fail@L56347`, `req.15.DynamicChecksInjectFacts@L56363`, `def.15.RuntimeCheckInsertionPointsIntro@L56378`, `rule.15.Insert-Precondition-Check@L56391`, `rule.15.Insert-Postcondition-Check@L56407`, `rule.15.Insert-TypeInv-Construction-Check@L56423`
- `rule.15.Insert-TypeInv-PreCall-Check@L56439`, `rule.15.Insert-TypeInv-PostCall-Check@L56455`, `rule.15.Insert-LoopInv-Init-Check@L56471`, `rule.15.Insert-LoopInv-Maintenance-Check@L56487`, `rule.15.Insert-Refinement-Check@L56503`, `diag.15.VerificationLogic@L56521`, `req.15.BehavioralSubtypingNoSurfaceSyntax@L56538`, `req.15.BehavioralSubtypingNotParserOwned@L56553`
- `def.15.BehavioralSubtypingRelationship@L56568`, `req.15.BehavioralSubtypingLiskovRequirement@L56583`, `req.15.BehavioralSubtypingPreconditionRule@L56596`, `req.15.BehavioralSubtypingPostconditionRule@L56612`, `req.15.BehavioralSubtypingVerificationStrategy@L56628`, `req.15.BehavioralSubtypingNoRuntimeChecks@L56644`, `req.15.BehavioralSubtypingNoAdditionalRuntimeSemantics@L56659`, `req.15.BehavioralSubtypingLoweringNoExtraChecks@L56674`
- `diag.15.BehavioralSubtyping@L56689`, `diag.15.ProcedureContractEntryDiagnosticsOwnership@L56704`, `diag-table.15.ProcedureContractEntryDiagnostics@L56717`
- `grammar.15.ProcedureDeclarations@L52259`, `req.15.ExternProcedureDeclarationsOwnedByFFI@L52277`, `rule.15.Parse-Procedure@L52292`, `rule.15.Parse-Signature@L52308`, `rule.15.Parse-ParamList-Empty@L52324`, `rule.15.Parse-ParamList-Cons@L52340`, `rule.15.Parse-Param@L52356`, `rule.15.Parse-ParamMode-None@L52372`
- `rule.15.Parse-ParamMode-Move@L52388`, `rule.15.Parse-ParamTail-End@L52404`, `rule.15.Parse-ParamTail-TrailingComma@L52420`, `rule.15.Parse-ParamTail-Comma@L52436`, `rule.15.Parse-ReturnOpt-None@L52452`, `rule.15.Parse-ReturnOpt-Arrow@L52468`, `def.15.ProcedureDeclAst@L52486`, `def.15.ParamAst@L52499`
- `def.15.ParamNamesAndBinds@L52512`, `def.15.ProcReturn@L52526`, `def.15.BodyReturnType@L52541`, `def.15.ExplicitReturn@L52556`, `def.15.ReturnAnnOk@L52571`, `rule.15.WF-ProcedureDecl@L52584`, `def.15.DeclTyping@L52600`, `def.15.ProvBindCheck@L52615`
- `def.15.DeclTypingItem@L52628`, `rule.15.ProcedureDeclOkJudgement@L52650`, `rule.15.WF-ProcedureDecl-MissingReturnType@L52663`, `rule.15.WF-ProcBody-ExplicitReturn-Err@L52679`, `req.15.ExportedProcedureForeignCallableObligations@L52695`, `def.15.MainEntryPointDefinitions@L52708`, `rule.15.Main-Ok@L52729`, `rule.15.Main-Bypass-NonExecutable@L52745`
- `rule.15.Main-Multiple@L52761`, `rule.15.Main-Generic-Err@L52777`, `rule.15.Main-Signature-Err@L52793`, `rule.15.Main-Missing@L52809`, `def.15.MainDiagRefs@L52825`, `def.15.FuncValDefined@L52840`, `def.15.BindParams@L52853`, `def.15.ArgumentPassingJudgements@L52866`
- `def.15.CallJudgements@L52880`, `def.15.CallTargets@L52893`, `def.15.BuiltinProcedureParams@L52909`, `def.15.SynthParams@L52923`, `def.15.CalleeProc@L52936`, `def.15.CallParams@L52950`, `def.15.ReturnOut@L52966`, `rule.15.EvalArgsSigma-Empty@L52985`
- `rule.15.EvalArgsSigma-Cons-Move@L53000`, `rule.15.EvalArgsSigma-Cons-Ref@L53016`, `rule.15.EvalArgsSigma-Ctrl-Move@L53032`, `rule.15.EvalArgsSigma-Ctrl-Ref@L53048`, `rule.15.ApplyRegionProc-NewScoped@L53064`, `rule.15.ApplyRegionProc-Alloc@L53080`, `rule.15.ApplyRegionProc-Reset@L53096`, `rule.15.ApplyRegionProc-Freeze@L53112`
- `rule.15.ApplyRegionProc-Thaw@L53128`, `rule.15.ApplyRegionProc-Free@L53144`, `rule.15.ApplyCancelProc-New@L53160`, `rule.15.ApplyProcSigma@L53176`, `rule.15.EvalSigma-Call-Proc@L53192`, `rule.15.CG-Item-Procedure@L53210`, `req.15.MainProgramEntryHandlingOwnedByChapter24@L53226`, `diag.15.ProcedureDeclarations@L53241`
- `grammar.15.MethodsAndReceivers@L53258`, `req.15.ClassAndStateMethodsReuseReceiverForms@L53275`, `rule.15.Parse-MethodDefAfterVis@L53290`, `rule.15.Parse-Override-Yes@L53306`, `rule.15.Parse-Override-No@L53322`, `rule.15.Parse-MethodSignature@L53338`, `rule.15.Parse-StateMethodSignature-Receiver@L53354`, `rule.15.Parse-MethodParams-None@L53370`
- `rule.15.Parse-MethodParams-Comma@L53386`, `rule.15.Parse-Receiver-Short-Const@L53402`, `rule.15.Parse-Receiver-Short-Unique@L53418`, `rule.15.Parse-Receiver-Short-Shared@L53434`, `rule.15.Parse-Receiver-Explicit@L53450`, `def.15.MethodDeclAst@L53468`, `def.15.ReceiverAst@L53481`, `def.15.RecordFieldsMethodsAndSelf@L53496`
- `def.15.SelfType@L53511`, `def.15.RecvType@L53524`, `def.15.RecvMode@L53540`, `def.15.RecvPerm@L53554`, `def.15.MethodSignaturesAndParams@L53569`, `rule.15.Recv-Explicit@L53588`, `rule.15.Record-Method-RecvSelf-Err@L53604`, `rule.15.Recv-Const@L53620`
- `rule.15.Recv-Unique@L53635`, `rule.15.Recv-Shared@L53650`, `rule.15.WF-Record-Method@L53665`, `rule.15.T-Record-Method-Body@L53681`, `rule.15.WF-Record-Methods@L53697`, `rule.15.Record-Method-Dup@L53713`, `def.15.ArgsOkJudg@L53729`, `def.15.RecvBaseType@L53742`
- `rule.15.Args-Empty@L53755`, `rule.15.Args-Cons@L53770`, `rule.15.Args-Cons-Ref@L53786`, `def.15.RecvArgOk@L53802`, `rule.15.T-Record-MethodCall@L53815`, `req.15.OwnerSpecificReceiverRestrictionsReuseCommonForms@L53831`, `def.15.RecvArgMode@L53846`, `def.15.MethodOf@L53860`
- `def.15.RecvBase@L53875`, `def.15.RecvParams@L53888`, `rule.15.EvalRecvSigma-Move@L53903`, `rule.15.EvalRecvSigma-Ref-Dyn@L53919`, `rule.15.EvalRecvSigma-Ref-Dyn-Expired@L53935`, `rule.15.EvalRecvSigma-Ref@L53951`, `rule.15.EvalRecvSigma-Ctrl-Move@L53967`, `rule.15.EvalRecvSigma-Ctrl-Ref@L53983`
- `def.15.BindMethodParams@L53999`, `rule.15.ApplyMethodSigma-Prim@L54012`, `rule.15.ApplyMethodSigma@L54028`, `req.15.MethodsLowerAsProceduresWithReceiverFirst@L54046`, `rule.15.Mangle-Record-Method@L54059`, `rule.15.Mangle-Class-Method@L54075`, `rule.15.Mangle-State-Method@L54091`, `diag.15.MethodsAndReceivers@L54109`
- `req.15.OverloadingNoAdditionalSyntax@L54126`, `req.15.OverloadResolutionNotParserConcern@L54141`, `def.15.ClassDefaults@L54156`, `def.15.LookupMethod@L54169`, `rule.15.LookupMethod-NotFound@L54186`, `rule.15.LookupMethod-Ambig@L54202`, `req.15.FreeProcedureOverloadResolutionBeforeCallTyping@L54218`, `req.15.FreeCallOverloadResolutionAlgorithm@L54231`
- `req.15.DuplicateErasedOverloadSignaturesForbidden@L54253`, `req.15.NoRuntimeOverloadSearch@L54268`, `req.15.OverloadResolutionCompleteBeforeLowering@L54283`, `diag-table.15.Overloading@L54298`, `diag.15.MethodLookupDiagnostics@L54315`, `grammar.15.ContractClauses@L54332`, `req.15.ForeignContractStartDisambiguatesContracts@L54354`, `rule.15.Parse-ContractClauseOpt-None@L54367`
- `rule.15.Parse-ContractClauseOpt-Yes@L54383`, `rule.15.Parse-ContractBody-PostOnly@L54399`, `rule.15.Parse-ContractBody-PrePost@L54415`, `rule.15.Parse-ContractBody-PreOnly@L54431`, `def.15.ContractClauseAst@L54449`, `def.15.ContractOpt@L54462`, `rule.15.WF-Contract@L54477`, `def.15.ContractPurityJudgementIntro@L54494`
- `rule.15.Pure-Literal@L54507`, `rule.15.Pure-Ident@L54523`, `rule.15.Pure-Field@L54539`, `rule.15.Pure-Tuple-Access@L54555`, `rule.15.Pure-Index@L54571`, `rule.15.Pure-Unary@L54587`, `rule.15.Pure-Binary@L54603`, `def.15.PureOps@L54619`
- `rule.15.Pure-Cast@L54632`, `rule.15.Pure-If@L54648`, `rule.15.Pure-If-Is@L54664`, `rule.15.Pure-If-Is-No-Else@L54680`, `rule.15.Pure-If-Case@L54696`, `rule.15.Pure-If-Case-No-Else@L54712`, `rule.15.Pure-Block@L54728`, `rule.15.Pure-Tuple@L54744`
- `rule.15.Pure-Array@L54760`, `rule.15.Pure-Record@L54776`, `rule.15.Pure-Call-Builtin@L54792`, `rule.15.Pure-Call-Procedure@L54808`, `rule.15.Pure-Method-Const@L54824`, `rule.15.Pure-Comptime@L54840`, `def.15.ContractPurityHelperPredicates@L54856`, `req.15.ContractNeverPureForms@L54876`
- `def.15.PreconditionEvaluationContext@L54889`, `def.15.PostconditionEvaluationContext@L54902`, `req.15.ContractClausesNoIndependentRuntimeEffect@L54917`, `req.15.ContractClauseLoweringViaVerificationResults@L54932`, `diag.15.ContractClauses@L54947`, `req.15.PreconditionSyntaxDefinition@L54964`, `req.15.PreconditionsParsedByContractBody@L54979`, `def.15.PreconditionOf@L54994`
- `def.15.PreconditionProofContext@L55011`, `rule.15.Pre-Satisfied@L55027`, `def.15.PreconditionElisionRules@L55043`, `req.15.CallerResponsibleForPrecondition@L55063`, `req.15.PreconditionRuntimeEvaluationOrder@L55078`, `req.15.PreconditionCheckInsertionOwnedByVerificationLogic@L55093`, `diag.15.Preconditions@L55108`, `grammar.15.Postconditions@L55125`
- `rule.15.Parse-Contract-Result@L55143`, `rule.15.Parse-Contract-Entry@L55159`, `def.15.ContractIntrinsicAst@L55177`, `def.15.PostconditionOf@L55190`, `def.15.PostconditionProofContext@L55207`, `rule.15.Post-Valid@L55222`, `def.15.PostconditionElisionRules@L55238`, `req.15.ContractResultProperties@L55258`
- `rule.15.Result-Union-Type@L55275`, `rule.15.Result-Is-Predicate@L55291`, `rule.15.Result-Narrowing@L55307`, `rule.15.Propagate-Postcondition@L55323`, `rule.15.Result-Modal@L55339`, `rule.15.Result-Generic@L55355`, `rule.15.Result-Generic-Constraint@L55371`, `req.15.ContractEntryConstraints@L55387`
- `rule.15.Entry-Type@L55405`, `req.15.PostconditionResultRuntimeBinding@L55423`, `req.15.ContractEntryRuntimeCapture@L55436`, `def.15.EntryCaptureTiming@L55453`, `rule.15.EntryCapturePhase@L55473`, `def.15.EntryCaptureValue@L55490`, `req.15.PostconditionLoweringRepresentation@L55505`, `diag.15.Postconditions@L55520`
- `grammar.15.Invariants@L55537`, `rule.15.Parse-InvariantOpt-None@L55555`, `rule.15.Parse-InvariantOpt-Yes@L55571`, `rule.15.ParseLoopInvariantOpt@L55587`, `def.15.InvariantAst@L55602`, `def.15.TypeInvariantAstExtensions@L55616`, `def.15.LoopInvariantAstPreservation@L55631`, `def.15.TypeInvariantContext@L55646`
- `def.15.TypeInvariantEnforcementPoints@L55663`, `req.15.TypeInvariantsForbidPublicMutableFields@L55680`, `req.15.PrivateProceduresExemptFromTypeInvariantPreCall@L55693`, `def.15.LoopInvariantEnforcementPoints@L55706`, `req.15.LoopInvariantExitFact@L55723`, `req.15.InvariantVerificationModeRules@L55736`, `req.15.InvariantRuntimeChecks@L55751`, `req.15.InvariantLoweringViaVerificationLogic@L55766`
- `diag.15.Invariants@L55781`, `req.15.VerificationLogicNoSurfaceSyntax@L55798`, `req.15.VerificationLogicNotParserOwned@L55813`, `def.15.ContractKind@L55828`, `def.15.VerificationFact@L55841`, `def.15.CheckState@L55854`, `def.15.ContractCheck@L55867`, `def.15.DynamicScopeAndContext@L55882`
- `rule.15.Contract-Static-OK@L55903`, `rule.15.Contract-Static-Fail@L55919`, `rule.15.Contract-Dynamic-Elide@L55935`, `rule.15.Contract-Dynamic-Check@L55951`, `req.15.MandatoryProofTechniques@L55967`, `def.15.ProofContextAt@L55987`, `def.15.DecidablePredicates@L56008`, `rule.15.Ent-True@L56028`
- `rule.15.Ent-Fact@L56044`, `rule.15.Ent-And@L56060`, `rule.15.Ent-Or-L@L56076`, `rule.15.Ent-Or-R@L56092`, `rule.15.Ent-Linear@L56108`, `def.15.LinearIntegerEntailment@L56124`, `req.15.LinearEntailmentSoundAndComplete@L56147`, `def.15.StaticProofAt@L56160`
- `def.15.NegFact@L56173`, `req.15.VerificationFactsNoRuntimeRepresentation@L56195`, `rule.15.Fact-Dominate@L56212`, `req.15.FactGeneration@L56228`, `req.15.TypeNarrowingFromFacts@L56251`, `def.15.ContractEnvironments@L56266`, `rule.15.Check-True@L56283`, `rule.15.Check-False@L56299`
- `rule.15.Check-Panic@L56315`, `rule.15.Check-Ok@L56331`, `rule.15.Check-Fail@L56347`, `req.15.DynamicChecksInjectFacts@L56363`, `def.15.RuntimeCheckInsertionPointsIntro@L56378`, `rule.15.Insert-Precondition-Check@L56391`, `rule.15.Insert-Postcondition-Check@L56407`, `rule.15.Insert-TypeInv-Construction-Check@L56423`
- `rule.15.Insert-TypeInv-PreCall-Check@L56439`, `rule.15.Insert-TypeInv-PostCall-Check@L56455`, `rule.15.Insert-LoopInv-Init-Check@L56471`, `rule.15.Insert-LoopInv-Maintenance-Check@L56487`, `rule.15.Insert-Refinement-Check@L56503`, `diag.15.VerificationLogic@L56521`, `req.15.BehavioralSubtypingNoSurfaceSyntax@L56538`, `req.15.BehavioralSubtypingNotParserOwned@L56553`
- `def.15.BehavioralSubtypingRelationship@L56568`, `req.15.BehavioralSubtypingLiskovRequirement@L56583`, `req.15.BehavioralSubtypingPreconditionRule@L56596`, `req.15.BehavioralSubtypingPostconditionRule@L56612`, `req.15.BehavioralSubtypingVerificationStrategy@L56628`, `req.15.BehavioralSubtypingNoRuntimeChecks@L56644`, `req.15.BehavioralSubtypingNoAdditionalRuntimeSemantics@L56659`, `req.15.BehavioralSubtypingLoweringNoExtraChecks@L56674`
- `diag.15.BehavioralSubtyping@L56689`, `diag.15.ProcedureContractEntryDiagnosticsOwnership@L56704`, `diag-table.15.ProcedureContractEntryDiagnostics@L56717`

### Language Constructs, Dynamic Semantics, And Feature Semantics

#### `spec.expressions`

Count: 478 total; 475 required; 0 recommended; 0 informative. Ledger line span: L56475-L64152.

- `grammar.16.LiteralAndNameExpressions@L56762`, `req.16.QualifiedApplicationOwnership@L56780`, `rule.16.Parse-Literal-Expr@L56795`, `rule.16.Parse-Null-Ptr@L56811`, `rule.16.Parse-Identifier-Expr@L56827`, `rule.16.Parse-Qualified-Name@L56843`, `def.16.LiteralKindAndToken@L56861`, `def.16.LiteralNameExprAst@L56875`
- `def.16.QualifiedNameResolution@L56889`, `def.16.ValuePathType@L56907`, `def.16.NumericLiteralTypeSets@L56930`, `def.16.NumericLiteralParsingHelpers@L56947`, `rule.16.T-Int-Literal-Suffix@L56974`, `rule.16.T-Int-Literal-Default@L56990`, `rule.16.T-Float-Literal-Explicit@L57006`, `rule.16.T-Float-Literal-Infer@L57022`
- `rule.16.T-Bool-Literal@L57038`, `rule.16.T-Char-Literal@L57054`, `rule.16.T-String-Literal@L57070`, `rule.16.Syn-Literal@L57086`, `def.16.NullLiteralExpected@L57102`, `rule.16.Chk-Int-Literal@L57115`, `rule.16.Chk-Float-Literal-Explicit@L57131`, `rule.16.Chk-Float-Literal-Infer@L57147`
- `rule.16.Chk-Null-Literal@L57163`, `def.16.PtrNullExpected@L57179`, `rule.16.Chk-Null-Ptr@L57192`, `rule.16.Syn-PtrNull-Err@L57208`, `rule.16.Chk-PtrNull-Err@L57224`, `rule.16.T-Ident@L57240`, `rule.16.T-Path-Value@L57256`, `rule.16.Expr-Unresolved-Err@L57272`
- `req.16.QualifiedNameEliminatedBeforeTyping@L57288`, `def.16.EvaluationJudgements@L57303`, `def.16.LiteralRuntimeValues@L57318`, `rule.16.EvalSigma-Literal@L57339`, `rule.16.EvalSigma-PtrNull@L57355`, `rule.16.EvalSigma-Ident@L57370`, `rule.16.EvalSigma-Path@L57386`, `rule.16.EvalSigma-ErrorExpr@L57402`
- `req.16.NamePathEvaluationMayPanicForPoisonedModules@L57417`, `rule.16.Lower-Expr-Literal@L57432`, `rule.16.Lower-Expr-PtrNull@L57448`, `rule.16.Lower-Expr-Ident-Local@L57463`, `rule.16.Lower-Expr-Ident-Path@L57479`, `rule.16.Lower-Expr-Path@L57495`, `rule.16.Lower-Expr-Error@L57510`, `diag.16.LiteralAndNameExpressions@L57527`
- `grammar.16.AccessAndPlaceExpressions@L57544`, `req.16.AccessPostfixOwnership@L57560`, `rule.16.Postfix-Field@L57575`, `rule.16.Postfix-TupleIndex@L57591`, `rule.16.Postfix-Index@L57607`, `def.16.IsPlace@L57623`, `rule.16.Parse-Place-Deref@L57636`, `rule.16.Parse-Place-Postfix@L57652`
- `rule.16.Parse-Place-Err@L57668`, `def.16.AccessPlaceAst@L57686`, `def.16.PlaceForms0@L57699`, `def.16.FieldVisibility@L57712`, `def.16.IndexClassification@L57726`, `rule.16.T-Field-Record@L57743`, `rule.16.T-Field-Record-Perm@L57759`, `rule.16.P-Field-Record@L57775`
- `rule.16.P-Field-Record-Perm@L57791`, `rule.16.T-Tuple-Index@L57807`, `rule.16.T-Tuple-Index-Perm@L57823`, `rule.16.P-Tuple-Index@L57839`, `rule.16.P-Tuple-Index-Perm@L57855`, `rule.16.T-Index-Array@L57871`, `rule.16.T-Index-Array-Dynamic@L57887`, `rule.16.T-Index-Array-Perm@L57903`
- `rule.16.T-Index-Array-Perm-Dynamic@L57919`, `rule.16.T-Index-Slice@L57935`, `rule.16.T-Index-Slice-Perm@L57951`, `rule.16.T-Slice-From-Array@L57967`, `rule.16.T-Slice-From-Array-Perm@L57983`, `rule.16.T-Slice-From-Slice@L57999`, `rule.16.T-Slice-From-Slice-Perm@L58015`, `rule.16.PlaceIndexAndSliceCounterparts@L58031`
- `rule.16.Coerce-Array-Slice@L58044`, `rule.16.Union-DirectAccess-Err@L58060`, `rule.16.ValueUse-NonBitcopyPlace@L58076`, `rule.16.EvalSigma-FieldAccess@L58094`, `rule.16.EvalSigma-TupleAccess@L58110`, `rule.16.EvalSigma-Index@L58126`, `rule.16.EvalSigma-Index-Range@L58142`, `req.16.IndexAccessRuntimeFailuresAndControlPropagation@L58158`
- `rule.16.Lower-Expr-FieldAccess@L58173`, `rule.16.Lower-Expr-TupleAccess@L58189`, `rule.16.Lower-Expr-IndexFamily@L58205`, `rule.16.Lower-Place-Ident@L58218`, `rule.16.Lower-Place-Field@L58233`, `rule.16.Lower-Place-Tuple@L58249`, `rule.16.Lower-Place-Index@L58265`, `rule.16.Lower-Place-Deref@L58281`
- `req.16.PlaceReadWriteLoweringPreservesAccessBehavior@L58297`, `diag.16.AccessAndPlaceExpressions@L58312`, `req.16.ArraySliceIndexDiagnosticsAndPanicBehavior@L58325`, `grammar.16.CallExpressions@L58342`, `req.16.QualifiedApplyParenPreResolution@L58361`, `rule.16.Postfix-Call@L58376`, `rule.16.Postfix-Call-TypeArgs@L58392`, `rule.16.Postfix-MethodCall@L58408`
- `rule.16.Parse-Qualified-Apply-Paren@L58424`, `rule.16.ArgumentListParsingFamily@L58440`, `def.16.ArgAst@L58455`, `def.16.CallExprAst@L58468`, `def.16.ArgAccessors@L58481`, `def.16.MovedArg@L58495`, `req.16.QualifiedParenthesizedApplicationResolution@L58510`, `def.16.CallStaticJudgementsAndArgumentTyping@L58529`
- `rule.16.ArgsT-Empty@L58557`, `rule.16.ArgsT-Cons@L58572`, `rule.16.ArgsT-Cons-Ref@L58588`, `rule.16.T-Call-Generic-Infer@L58604`, `rule.16.T-Call@L58620`, `rule.16.Call-Callee-NotFunc@L58636`, `rule.16.Call-ArgCount-Err@L58652`, `rule.16.Call-ArgType-Err@L58668`
- `rule.16.Call-Move-Missing@L58684`, `rule.16.Call-Move-Unexpected@L58700`, `rule.16.Call-Arg-Packed-Unsafe-Err@L58716`, `rule.16.Call-Arg-NotPlace@L58732`, `rule.16.Chk-Call-Generic-Infer@L58748`, `req.16.CallTypeArgsStaticOwnership@L58764`, `req.16.MethodRecordClosureCallStaticOwnership@L58777`, `req.16.ExternProcedureCallsRequireUnsafe@L58790`
- `rule.16.EvalSigma-Call-Closure@L58805`, `rule.16.EvalSigma-Call-RegionProc@L58821`, `rule.16.EvalSigma-Call-RegionProc-Ctrl-Args@L58837`, `rule.16.EvalSigma-Call-CancelProc@L58853`, `rule.16.EvalSigma-Call-CancelProc-Ctrl-Args@L58869`, `rule.16.EvalSigma-Call-Proc@L58885`, `rule.16.EvalSigma-Call-Record@L58901`, `rule.16.EvalSigma-MethodCall@L58917`
- `req.16.CallControlPropagation@L58933`, `req.16.MethodCallControlPropagation@L58946`, `req.16.CallTypeArgsEvaluationElaboration@L58959`, `rule.16.Lower-Args-Empty@L58974`, `rule.16.Lower-Args-Cons-Move@L58989`, `rule.16.Lower-Args-Cons-Ref@L59005`, `rule.16.Lower-Expr-Call-Closure@L59021`, `rule.16.Lower-Expr-CallFamily@L59037`
- `rule.16.Lower-MethodCallFamily@L59050`, `req.16.CallTypeArgsLoweringElaboration@L59063`, `diag.16.CallExpressions@L59078`, `grammar.16.OperatorExpressions@L59095`, `req.16.OperatorPrefixSyntaxOwnership@L59126`, `rule.16.ParseRangeFamily@L59141`, `rule.16.ParseLeftChainFamily@L59154`, `rule.16.ParsePowerFamily@L59167`
- `rule.16.Parse-Unary-Prefix@L59180`, `def.16.RangeAndOperatorExprAst@L59198`, `def.16.OperatorSets@L59212`, `def.16.OperatorStaticTypes@L59230`, `rule.16.T-Range-Lift@L59245`, `rule.16.RangeTypingFamily@L59261`, `rule.16.T-Not-Bool@L59274`, `rule.16.T-Not-Int@L59290`
- `rule.16.T-Neg@L59306`, `rule.16.T-Arith@L59322`, `rule.16.T-Bitwise@L59338`, `rule.16.T-Shift@L59354`, `rule.16.T-Compare-Eq@L59370`, `rule.16.T-Compare-Ord@L59386`, `rule.16.T-Logical@L59402`, `def.16.OperatorRuntimeJudgementsAndValuePredicates@L59420`
- `def.16.OperatorComparisonRuntime@L59442`, `def.16.OperatorBitShiftArithmeticRuntime@L59462`, `def.16.UnaryOperatorRuntime@L59482`, `req.16.FloatUnaryNegationTotality@L59500`, `def.16.BinaryOperatorRuntime@L59513`, `rule.16.EvalSigma-Range@L59534`, `rule.16.EvalSigma-Unary@L59550`, `rule.16.EvalSigma-Bin-And-False@L59566`
- `rule.16.EvalSigma-Bin-And-True@L59582`, `rule.16.EvalSigma-Bin-Or-True@L59598`, `rule.16.EvalSigma-Bin-Or-False@L59614`, `rule.16.EvalSigma-Binary@L59630`, `req.16.OperatorUndefinedAndNaNBehavior@L59646`, `rule.16.Lower-Expr-Unary@L59661`, `rule.16.Lower-Expr-Bin-And@L59677`, `rule.16.Lower-Expr-Bin-Or@L59693`
- `rule.16.Lower-Expr-Binary@L59709`, `rule.16.Lower-Expr-Range@L59725`, `def.16.UnaryOperatorLoweringPanicCheck@L59741`, `rule.16.Lower-UnOp-Ok@L59755`, `rule.16.Lower-UnOp-Panic@L59771`, `req.16.UnaryNegationLoweringOverflowChecks@L59787`, `rule.16.LowerBinaryAndRangeRemainderFamily@L59800`, `diag.16.OperatorExpressions@L59815`
- `grammar.16.CastAndTransmuteExpressions@L59832`, `req.16.WidenPrefixOwnershipForCastTransmute@L59848`, `rule.16.Parse-Cast@L59863`, `rule.16.Parse-CastTail-None@L59879`, `rule.16.Parse-CastTail-As@L59895`, `rule.16.ParseTransmuteExprFamily@L59911`, `req.16.WidenParsingOwnershipForCastTransmute@L59924`, `def.16.CastTransmuteExprAst@L59939`
- `req.16.WidenAstOwnershipForCastTransmute@L59952`, `def.16.CastValidity@L59967`, `rule.16.T-Cast@L59982`, `rule.16.T-Cast-Invalid@L59998`, `rule.16.T-Transmute-SizeEq@L60014`, `rule.16.T-Transmute-AlignEq@L60030`, `rule.16.T-Transmute@L60046`, `rule.16.Transmute-Unsafe-Err@L60062`
- `def.16.ValidTransmuteTarget@L60078`, `req.16.WidenTypingDiagnosticsOwnershipForCastTransmute@L60095`, `def.16.CastDynamicContext@L60110`, `def.16.CastRuntimeConversionHelpers@L60124`, `rule.16.CastVal-Id@L60158`, `rule.16.CastVal-Int-Int-Signed@L60174`, `rule.16.CastVal-Int-Int-Unsigned@L60190`, `rule.16.CastVal-Int-Float@L60206`
- `req.16.IntToFloatLoweringPreservesSignedness@L60222`, `rule.16.CastVal-Float-Float@L60235`, `rule.16.CastVal-Float-Int@L60251`, `rule.16.CastVal-Bool-Int@L60267`, `rule.16.CastVal-Int-Bool@L60285`, `rule.16.CastVal-Char-U32@L60303`, `rule.16.CastVal-U32-Char@L60319`, `rule.16.EvalSigma-Cast@L60335`
- `rule.16.EvalSigma-Cast-Panic@L60351`, `def.16.TransmuteVal@L60367`, `rule.16.EvalSigma-Transmute@L60380`, `rule.16.EvalSigma-Transmute-Ctrl@L60396`, `req.16.WidenDynamicOwnershipForCastTransmute@L60412`, `rule.16.Lower-Expr-Cast@L60427`, `rule.16.Lower-Expr-Transmute@L60443`, `rule.16.LowerCastTransmuteFamily@L60459`
- `diag.16.CastAndTransmuteExpressions@L60474`, `grammar.16.ConstructionExpressions@L60491`, `req.16.EnumConstructorAndRecordDefaultSyntaxResolution@L60513`, `rule.16.Parse-Tuple-Literal@L60528`, `rule.16.Parse-Array-Segment-Elem@L60544`, `rule.16.Parse-Array-Segment-Repeat@L60560`, `rule.16.Parse-Array-Segment-List-Empty@L60576`, `rule.16.Parse-Array-Segment-List-Single@L60591`
- `rule.16.Parse-Array-Segment-List-Comma@L60607`, `rule.16.Parse-Array-Literal@L60623`, `rule.16.Parse-Record-Literal-ModalState@L60639`, `rule.16.Parse-Record-Literal@L60655`, `rule.16.Parse-Qualified-Apply-Brace@L60671`, `rule.16.ConstructionListAndShorthandParsingFamily@L60687`, `def.16.FieldInitAst@L60702`, `def.16.ConstructionExprAst@L60715`
- `def.16.FieldInitNamesAndSet@L60728`, `req.16.QualifiedBraceApplicationResolution@L60742`, `req.16.QualifiedParenApplicationConstructionResolution@L60758`, `rule.16.T-Unit-Literal@L60773`, `rule.16.T-Tuple-Literal@L60788`, `def.16.ArraySegmentLength@L60804`, `rule.16.T-Array-Literal-Segments@L60818`, `def.16.RecordFieldNameSet@L60843`
- `rule.16.T-Record-Literal@L60857`, `rule.16.Record-FieldInit-Dup@L60873`, `rule.16.Record-FieldInit-Missing@L60889`, `rule.16.RecordFieldUnknownNotVisibleFamily@L60905`, `rule.16.Record-Field-NonBitcopy-Move@L60918`, `rule.16.EnumLiteralTypingFamily@L60934`, `def.16.RecordDefaultConstructionEligibility@L60947`, `rule.16.T-Record-Default@L60961`
- `rule.16.Record-Default-Init-Err@L60977`, `rule.16.EvalSigmaTupleConstructionFamily@L60995`, `rule.16.EvalSigmaArrayConstructionFamily@L61008`, `rule.16.EvalSigmaRecordConstructionFamily@L61021`, `rule.16.EvalSigmaEnumConstructionFamily@L61034`, `req.16.RecordDefaultConstructionRuntimeUsesCallRecord@L61047`, `rule.16.Lower-Expr-Tuple@L61062`, `rule.16.Lower-Expr-Array@L61078`
- `rule.16.Lower-Expr-Record@L61094`, `rule.16.LowerEnumConstructionFamily@L61110`, `rule.16.Lower-CallIR-RecordCtor@L61123`, `diag.16.ConstructionExpressions@L61141`, `grammar.16.ControlExpressions@L61158`, `req.16.ControlExpressionOwnership@L61182`, `rule.16.Parse-If-Expr@L61198`, `rule.16.Parse-If-Is-Single@L61214`
- `rule.16.Parse-If-Is-CaseList@L61230`, `rule.16.Parse-Loop-Expr@L61246`, `rule.16.Parse-Block-Expr@L61262`, `rule.16.ControlExpressionParsingRemainderFamily@L61278`, `def.16.ControlExprAst@L61293`, `def.16.ControlAstHelpers@L61306`, `def.16.LoopTypeInference@L61320`, `req.16.BlockTypingOwnershipForControlExpressions@L61344`
- `rule.16.T-If@L61363`, `rule.16.T-If-No-Else@L61379`, `rule.16.CheckIfFamily@L61395`, `req.16.PatternTypingOwnershipForControlExpressions@L61408`, `rule.16.T-If-Is@L61424`, `rule.16.T-If-Is-No-Else@L61440`, `rule.16.IfCaseTypingFamily@L61456`, `rule.16.CheckIfIsAndIfCaseFamily@L61469`
- `req.16.LoopInvariantTypingOwnership@L61483`, `rule.16.T-Loop-Infinite@L61496`, `rule.16.T-Loop-Conditional@L61512`, `rule.16.T-Loop-Iter@L61528`, `rule.16.AsyncIteratorLoopTypingFamily@L61544`, `rule.16.EvalSigma-If-True@L61559`, `rule.16.EvalSigma-If-False-None@L61575`, `rule.16.EvalSigma-If-False-Some@L61591`
- `rule.16.EvalSigma-If-Ctrl@L61607`, `rule.16.EvalSigma-If-Is@L61623`, `rule.16.EvalSigma-If-Is-Ctrl@L61639`, `rule.16.EvalSigma-If-Cases@L61655`, `rule.16.EvalSigma-If-Cases-Ctrl@L61671`, `rule.16.EvalIfCasesFamily@L61687`, `rule.16.EvalSigma-Block@L61700`, `def.16.LoopIterableTypePredicates@L61716`
- `def.16.LoopIteratorRuntime@L61734`, `def.16.LoopIterJudgement@L61767`, `rule.16.EvalSigma-Loop-Infinite-Step@L61780`, `rule.16.EvalSigma-Loop-Infinite-Continue@L61796`, `rule.16.EvalSigma-Loop-Infinite-Break@L61812`, `rule.16.EvalSigma-Loop-Infinite-Ctrl@L61828`, `rule.16.EvalSigma-Loop-Cond-False@L61844`, `rule.16.EvalSigma-Loop-Cond-True-Step@L61860`
- `rule.16.EvalSigma-Loop-Cond-Continue@L61876`, `rule.16.EvalSigma-Loop-Cond-Break@L61892`, `rule.16.EvalSigma-Loop-Cond-Ctrl@L61908`, `rule.16.EvalSigma-Loop-Cond-Body-Ctrl@L61924`, `rule.16.EvalSigma-Loop-Iter@L61940`, `rule.16.EvalSigma-Loop-Iter-Ctrl@L61956`, `rule.16.LoopIter-Done@L61972`, `rule.16.LoopIter-Step-Val@L61988`
- `rule.16.LoopIter-Step-Continue@L62004`, `rule.16.LoopIter-Step-Break@L62020`, `rule.16.LoopIter-Step-Ctrl@L62036`, `rule.16.Lower-Expr-If@L62054`, `rule.16.Lower-Expr-If-Is@L62070`, `rule.16.Lower-Expr-If-Cases@L62086`, `rule.16.LowerLoopExpressionFamily@L62102`, `rule.16.Lower-Expr-Block@L62115`
- `req.16.ControlExpressionLoweringOwnership@L62131`, `diag.16.ControlExpressions@L62146`, `req.16.ControlExpressionDiagnosticOwnership@L62159`, `grammar.16.EffectfulCoreExpressions@L62177`, `req.16.RegionAliasAllocRewrite@L62197`, `rule.16.Parse-Unary-Deref@L62212`, `rule.16.Parse-Unary-AddressOf@L62228`, `rule.16.Parse-Unary-Move@L62244`
- `rule.16.Postfix-Propagate@L62260`, `rule.16.Parse-Alloc-Implicit@L62276`, `rule.16.Parse-Unsafe-Expr@L62292`, `def.16.EffectfulCoreExprAst@L62310`, `rule.16.ResolveExpr-Alloc-Explicit-ByAlias@L62323`, `def.16.AddressOfStaticHelpers@L62342`, `rule.16.T-Unsafe-Expr@L62357`, `rule.16.Chk-Unsafe-Expr@L62373`
- `rule.16.T-AddrOf@L62389`, `rule.16.T-Deref-Ptr@L62405`, `rule.16.T-Deref-Raw@L62421`, `rule.16.DerefPlaceTypingFamily@L62437`, `rule.16.T-Move@L62450`, `rule.16.T-Alloc-Explicit@L62466`, `rule.16.T-Alloc-Implicit@L62482`, `def.16.SuccessMember@L62498`
- `rule.16.T-Propagate@L62511`, `def.16.SuccessMemberAsync@L62527`, `rule.16.T-Async-Try@L62540`, `rule.16.Async-Try-Infallible-Err@L62556`, `rule.16.EvalSigma-UnsafeBlock@L62574`, `rule.16.EvalSigma-AddressOf@L62590`, `rule.16.EvalSigma-Deref@L62606`, `rule.16.EvalSigma-Move@L62622`
- `rule.16.EvalSigma-Alloc-Implicit@L62638`, `rule.16.EvalSigma-Alloc-Implicit-Ctrl@L62654`, `rule.16.EvalSigma-Alloc-Explicit@L62670`, `rule.16.EvalSigma-Alloc-Explicit-Ctrl@L62686`, `rule.16.EvalSigma-Propagate-Success@L62702`, `rule.16.EvalSigma-Propagate-Success-Async@L62718`, `rule.16.EvalSigma-Propagate-Error@L62734`, `rule.16.EvalSigma-Propagate-Error-Async@L62750`
- `rule.16.EvalSigma-Propagate-Ctrl@L62767`, `def.16.ExprStateAndTerminalExpr@L62783`, `rule.16.StepSigma-Pure@L62798`, `rule.16.StepSigma-Alloc-Implicit@L62814`, `rule.16.StepSigma-Alloc-Implicit-Ctrl@L62830`, `rule.16.StepSigma-Alloc-Explicit@L62846`, `rule.16.StepSigma-Alloc-Explicit-Ctrl@L62862`, `rule.16.StepSigma-Block@L62878`
- `rule.16.StepSigma-UnsafeBlock@L62894`, `rule.16.StepSigma-Loop@L62910`, `rule.16.StepSigma-Stateful-Other@L62926`, `rule.16.Lower-Expr-UnsafeBlock@L62944`, `rule.16.Lower-Expr-Move@L62960`, `rule.16.Lower-Expr-AddressOf@L62976`, `rule.16.Lower-Expr-Deref@L62992`, `rule.16.Lower-Expr-Alloc@L63008`
- `rule.16.Lower-Expr-Propagate-Success@L63024`, `rule.16.Lower-Expr-Propagate-Return@L63040`, `req.16.EffectfulCoreLoweringMechanics@L63056`, `diag.16.EffectfulCoreExpressions@L63071`, `grammar.16.ClosureAndPipelineExpressions@L63088`, `req.16.ClosureParamTrailingComma@L63107`, `req.16.ClosureUnionParamParentheses@L63120`, `req.16.ClosureInvocationOrdinaryCallSyntax@L63133`
- `rule.16.Parse-Pipeline@L63148`, `rule.16.Parse-PipelineTail-Stop@L63164`, `rule.16.Parse-PipelineTail-Cons@L63180`, `rule.16.Parse-Closure-Expr@L63196`, `rule.16.Parse-Closure-Expr-Empty@L63212`, `rule.16.Parse-ClosureParams-Single@L63228`, `rule.16.Parse-ClosureParams-Cons@L63244`, `rule.16.Parse-ClosureParamType-Grouped@L63260`
- `rule.16.Parse-ClosureParamType-Plain@L63276`, `rule.16.Parse-ClosureParam-MoveTyped@L63292`, `rule.16.Parse-ClosureParam-MoveUntyped@L63308`, `rule.16.Parse-ClosureParam-Typed@L63324`, `rule.16.Parse-ClosureParam-Untyped@L63340`, `rule.16.Parse-ClosureRetOpt-Some@L63356`, `rule.16.Parse-ClosureRetOpt-None@L63372`, `rule.16.Parse-ClosureBody-Block@L63388`
- `rule.16.Parse-ClosureBody-Expr@L63404`, `def.16.ClosurePipelineAstForms@L63422`, `def.16.ClosureCaptureSets@L63441`, `def.16.ClosureEscapeClassification@L63461`, `def.16.ClosureParameterAccessors@L63476`, `rule.16.T-Closure-NonCapturing@L63490`, `rule.16.T-Closure-Capturing@L63508`, `rule.16.T-Closure-Escaping@L63527`
- `rule.16.K-Closure-Escape-Type@L63547`, `rule.16.Capture-Const@L63563`, `rule.16.Capture-Shared@L63579`, `rule.16.Capture-Unique-Err@L63595`, `rule.16.T-ClosureCall@L63611`, `rule.16.Infer-Closure-Params@L63627`, `rule.16.Infer-Closure-Params-Err@L63643`, `rule.16.Infer-Closure-Return@L63659`
- `req.16.ClosureSharedDependencyInference@L63675`, `def.16.ClosureCaptureBindingAccessors@L63688`, `rule.16.B-Closure-NonCapturing@L63712`, `rule.16.B-Closure-Capturing@L63728`, `rule.16.B-Closure-MoveCapture-Moved-Err@L63747`, `rule.16.B-Closure-MoveCapture-Immovable-Err@L63764`, `rule.16.B-Closure-RefCapture-Moved-Err@L63781`, `rule.16.T-Pipeline@L63798`
- `rule.16.T-Pipeline-NotCallable-Err@L63816`, `rule.16.T-Pipeline-TypeMismatch-Err@L63833`, `rule.16.T-Pipeline-ArgCount-Err@L63851`, `rule.16.B-Pipeline@L63868`, `req.16.ClosureParamInferenceFailure@L63884`, `req.16.ClosureSharedDependencyInferenceRestated@L63897`, `def.16.ClosureEnvironmentRuntimeModel@L63912`, `rule.16.EvalSigma-Closure-NonCapturing@L63948`
- `rule.16.EvalSigma-Closure-Capturing@L63964`, `def.16.MarkMoved@L63982`, `rule.16.EvalSigma-ClosureCall@L63996`, `def.16.ClosureCallRuntimeHelpers@L64014`, `rule.16.EvalSigma-ClosureCall-Ctrl@L64036`, `rule.16.EvalSigma-ClosureCall-Ctrl-Args@L64052`, `req.16.ClosureCallResolvedInternalFormRuntime@L64069`, `req.16.PipelineDesugaring@L64082`
- `rule.16.EvalSigma-Pipeline-Func@L64095`, `rule.16.EvalSigma-Pipeline-Closure@L64112`, `rule.16.EvalSigma-Pipeline-Ctrl-Left@L64129`, `rule.16.EvalSigma-Pipeline-Ctrl-Right@L64145`, `def.16.ClosureLoweringCaptureTypes@L64163`, `rule.16.Layout-ClosureEnv@L64179`, `rule.16.Layout-ClosureEnv-Empty@L64195`, `rule.16.Lower-Expr-Closure-NonCapturing@L64211`
- `rule.16.Lower-Expr-Closure-Capturing@L64227`, `def.16.LowerCaptureEnv@L64245`, `def.16.CapturedIdentifierLoweringHelpers@L64265`, `rule.16.Lower-CapturedIdent-Ref@L64280`, `req.16.LowerCapturedIdentRefTemporaries@L64297`, `rule.16.Lower-CapturedIdent-Move@L64310`, `def.16.ClosureEnvParam@L64327`, `def.16.ClosureCodeSig@L64340`
- `rule.16.Lower-Closure-Call@L64357`, `req.16.LowerClosureCallResolvedInternalForm@L64375`, `rule.16.Lower-Expr-Pipeline@L64388`, `def.16.LowerPipelineCallablePredicates@L64406`, `diag.16.ClosureAndPipelineExpressions@L64424`, `diag.16.ExpressionDiagnosticsSupplement@L64439`
- `grammar.16.LiteralAndNameExpressions@L56762`, `req.16.QualifiedApplicationOwnership@L56780`, `rule.16.Parse-Literal-Expr@L56795`, `rule.16.Parse-Null-Ptr@L56811`, `rule.16.Parse-Identifier-Expr@L56827`, `rule.16.Parse-Qualified-Name@L56843`, `def.16.LiteralKindAndToken@L56861`, `def.16.LiteralNameExprAst@L56875`
- `def.16.QualifiedNameResolution@L56889`, `def.16.ValuePathType@L56907`, `def.16.NumericLiteralTypeSets@L56930`, `def.16.NumericLiteralParsingHelpers@L56947`, `rule.16.T-Int-Literal-Suffix@L56974`, `rule.16.T-Int-Literal-Default@L56990`, `rule.16.T-Float-Literal-Explicit@L57006`, `rule.16.T-Float-Literal-Infer@L57022`
- `rule.16.T-Bool-Literal@L57038`, `rule.16.T-Char-Literal@L57054`, `rule.16.T-String-Literal@L57070`, `rule.16.Syn-Literal@L57086`, `def.16.NullLiteralExpected@L57102`, `rule.16.Chk-Int-Literal@L57115`, `rule.16.Chk-Float-Literal-Explicit@L57131`, `rule.16.Chk-Float-Literal-Infer@L57147`
- `rule.16.Chk-Null-Literal@L57163`, `def.16.PtrNullExpected@L57179`, `rule.16.Chk-Null-Ptr@L57192`, `rule.16.Syn-PtrNull-Err@L57208`, `rule.16.Chk-PtrNull-Err@L57224`, `rule.16.T-Ident@L57240`, `rule.16.T-Path-Value@L57256`, `rule.16.Expr-Unresolved-Err@L57272`
- `req.16.QualifiedNameEliminatedBeforeTyping@L57288`, `def.16.EvaluationJudgements@L57303`, `def.16.LiteralRuntimeValues@L57318`, `rule.16.EvalSigma-Literal@L57339`, `rule.16.EvalSigma-PtrNull@L57355`, `rule.16.EvalSigma-Ident@L57370`, `rule.16.EvalSigma-Path@L57386`, `rule.16.EvalSigma-ErrorExpr@L57402`
- `req.16.NamePathEvaluationMayPanicForPoisonedModules@L57417`, `rule.16.Lower-Expr-Literal@L57432`, `rule.16.Lower-Expr-PtrNull@L57448`, `rule.16.Lower-Expr-Ident-Local@L57463`, `rule.16.Lower-Expr-Ident-Path@L57479`, `rule.16.Lower-Expr-Path@L57495`, `rule.16.Lower-Expr-Error@L57510`, `diag.16.LiteralAndNameExpressions@L57527`
- `grammar.16.AccessAndPlaceExpressions@L57544`, `req.16.AccessPostfixOwnership@L57560`, `rule.16.Postfix-Field@L57575`, `rule.16.Postfix-TupleIndex@L57591`, `rule.16.Postfix-Index@L57607`, `def.16.IsPlace@L57623`, `rule.16.Parse-Place-Deref@L57636`, `rule.16.Parse-Place-Postfix@L57652`
- `rule.16.Parse-Place-Err@L57668`, `def.16.AccessPlaceAst@L57686`, `def.16.PlaceForms0@L57699`, `def.16.FieldVisibility@L57712`, `def.16.IndexClassification@L57726`, `rule.16.T-Field-Record@L57743`, `rule.16.T-Field-Record-Perm@L57759`, `rule.16.P-Field-Record@L57775`
- `rule.16.P-Field-Record-Perm@L57791`, `rule.16.T-Tuple-Index@L57807`, `rule.16.T-Tuple-Index-Perm@L57823`, `rule.16.P-Tuple-Index@L57839`, `rule.16.P-Tuple-Index-Perm@L57855`, `rule.16.T-Index-Array@L57871`, `rule.16.T-Index-Array-Dynamic@L57887`, `rule.16.T-Index-Array-Perm@L57903`
- `rule.16.T-Index-Array-Perm-Dynamic@L57919`, `rule.16.T-Index-Slice@L57935`, `rule.16.T-Index-Slice-Perm@L57951`, `rule.16.T-Slice-From-Array@L57967`, `rule.16.T-Slice-From-Array-Perm@L57983`, `rule.16.T-Slice-From-Slice@L57999`, `rule.16.T-Slice-From-Slice-Perm@L58015`, `rule.16.PlaceIndexAndSliceCounterparts@L58031`
- `rule.16.Coerce-Array-Slice@L58044`, `rule.16.Union-DirectAccess-Err@L58060`, `rule.16.ValueUse-NonBitcopyPlace@L58076`, `rule.16.EvalSigma-FieldAccess@L58094`, `rule.16.EvalSigma-TupleAccess@L58110`, `rule.16.EvalSigma-Index@L58126`, `rule.16.EvalSigma-Index-Range@L58142`, `req.16.IndexAccessRuntimeFailuresAndControlPropagation@L58158`
- `rule.16.Lower-Expr-FieldAccess@L58173`, `rule.16.Lower-Expr-TupleAccess@L58189`, `rule.16.Lower-Expr-IndexFamily@L58205`, `rule.16.Lower-Place-Ident@L58218`, `rule.16.Lower-Place-Field@L58233`, `rule.16.Lower-Place-Tuple@L58249`, `rule.16.Lower-Place-Index@L58265`, `rule.16.Lower-Place-Deref@L58281`
- `req.16.PlaceReadWriteLoweringPreservesAccessBehavior@L58297`, `diag.16.AccessAndPlaceExpressions@L58312`, `req.16.ArraySliceIndexDiagnosticsAndPanicBehavior@L58325`, `grammar.16.CallExpressions@L58342`, `req.16.QualifiedApplyParenPreResolution@L58361`, `rule.16.Postfix-Call@L58376`, `rule.16.Postfix-Call-TypeArgs@L58392`, `rule.16.Postfix-MethodCall@L58408`
- `rule.16.Parse-Qualified-Apply-Paren@L58424`, `rule.16.ArgumentListParsingFamily@L58440`, `def.16.ArgAst@L58455`, `def.16.CallExprAst@L58468`, `def.16.ArgAccessors@L58481`, `def.16.MovedArg@L58495`, `req.16.QualifiedParenthesizedApplicationResolution@L58510`, `def.16.CallStaticJudgementsAndArgumentTyping@L58529`
- `rule.16.ArgsT-Empty@L58557`, `rule.16.ArgsT-Cons@L58572`, `rule.16.ArgsT-Cons-Ref@L58588`, `rule.16.T-Call-Generic-Infer@L58604`, `rule.16.T-Call@L58620`, `rule.16.Call-Callee-NotFunc@L58636`, `rule.16.Call-ArgCount-Err@L58652`, `rule.16.Call-ArgType-Err@L58668`
- `rule.16.Call-Move-Missing@L58684`, `rule.16.Call-Move-Unexpected@L58700`, `rule.16.Call-Arg-Packed-Unsafe-Err@L58716`, `rule.16.Call-Arg-NotPlace@L58732`, `rule.16.Chk-Call-Generic-Infer@L58748`, `req.16.CallTypeArgsStaticOwnership@L58764`, `req.16.MethodRecordClosureCallStaticOwnership@L58777`, `req.16.ExternProcedureCallsRequireUnsafe@L58790`
- `rule.16.EvalSigma-Call-Closure@L58805`, `rule.16.EvalSigma-Call-RegionProc@L58821`, `rule.16.EvalSigma-Call-RegionProc-Ctrl-Args@L58837`, `rule.16.EvalSigma-Call-CancelProc@L58853`, `rule.16.EvalSigma-Call-CancelProc-Ctrl-Args@L58869`, `rule.16.EvalSigma-Call-Proc@L58885`, `rule.16.EvalSigma-Call-Record@L58901`, `rule.16.EvalSigma-MethodCall@L58917`
- `req.16.CallControlPropagation@L58933`, `req.16.MethodCallControlPropagation@L58946`, `req.16.CallTypeArgsEvaluationElaboration@L58959`, `rule.16.Lower-Args-Empty@L58974`, `rule.16.Lower-Args-Cons-Move@L58989`, `rule.16.Lower-Args-Cons-Ref@L59005`, `rule.16.Lower-Expr-Call-Closure@L59021`, `rule.16.Lower-Expr-CallFamily@L59037`
- `rule.16.Lower-MethodCallFamily@L59050`, `req.16.CallTypeArgsLoweringElaboration@L59063`, `diag.16.CallExpressions@L59078`, `grammar.16.OperatorExpressions@L59095`, `req.16.OperatorPrefixSyntaxOwnership@L59126`, `rule.16.ParseRangeFamily@L59141`, `rule.16.ParseLeftChainFamily@L59154`, `rule.16.ParsePowerFamily@L59167`
- `rule.16.Parse-Unary-Prefix@L59180`, `def.16.RangeAndOperatorExprAst@L59198`, `def.16.OperatorSets@L59212`, `def.16.OperatorStaticTypes@L59230`, `rule.16.T-Range-Lift@L59245`, `rule.16.RangeTypingFamily@L59261`, `rule.16.T-Not-Bool@L59274`, `rule.16.T-Not-Int@L59290`
- `rule.16.T-Neg@L59306`, `rule.16.T-Arith@L59322`, `rule.16.T-Bitwise@L59338`, `rule.16.T-Shift@L59354`, `rule.16.T-Compare-Eq@L59370`, `rule.16.T-Compare-Ord@L59386`, `rule.16.T-Logical@L59402`, `def.16.OperatorRuntimeJudgementsAndValuePredicates@L59420`
- `def.16.OperatorComparisonRuntime@L59442`, `def.16.OperatorBitShiftArithmeticRuntime@L59462`, `def.16.UnaryOperatorRuntime@L59482`, `req.16.FloatUnaryNegationTotality@L59500`, `def.16.BinaryOperatorRuntime@L59513`, `rule.16.EvalSigma-Range@L59534`, `rule.16.EvalSigma-Unary@L59550`, `rule.16.EvalSigma-Bin-And-False@L59566`
- `rule.16.EvalSigma-Bin-And-True@L59582`, `rule.16.EvalSigma-Bin-Or-True@L59598`, `rule.16.EvalSigma-Bin-Or-False@L59614`, `rule.16.EvalSigma-Binary@L59630`, `req.16.OperatorUndefinedAndNaNBehavior@L59646`, `rule.16.Lower-Expr-Unary@L59661`, `rule.16.Lower-Expr-Bin-And@L59677`, `rule.16.Lower-Expr-Bin-Or@L59693`
- `rule.16.Lower-Expr-Binary@L59709`, `rule.16.Lower-Expr-Range@L59725`, `def.16.UnaryOperatorLoweringPanicCheck@L59741`, `rule.16.Lower-UnOp-Ok@L59755`, `rule.16.Lower-UnOp-Panic@L59771`, `req.16.UnaryNegationLoweringOverflowChecks@L59787`, `rule.16.LowerBinaryAndRangeRemainderFamily@L59800`, `diag.16.OperatorExpressions@L59815`
- `grammar.16.CastAndTransmuteExpressions@L59832`, `req.16.WidenPrefixOwnershipForCastTransmute@L59848`, `rule.16.Parse-Cast@L59863`, `rule.16.Parse-CastTail-None@L59879`, `rule.16.Parse-CastTail-As@L59895`, `rule.16.ParseTransmuteExprFamily@L59911`, `req.16.WidenParsingOwnershipForCastTransmute@L59924`, `def.16.CastTransmuteExprAst@L59939`
- `req.16.WidenAstOwnershipForCastTransmute@L59952`, `def.16.CastValidity@L59967`, `rule.16.T-Cast@L59982`, `rule.16.T-Cast-Invalid@L59998`, `rule.16.T-Transmute-SizeEq@L60014`, `rule.16.T-Transmute-AlignEq@L60030`, `rule.16.T-Transmute@L60046`, `rule.16.Transmute-Unsafe-Err@L60062`
- `def.16.ValidTransmuteTarget@L60078`, `req.16.WidenTypingDiagnosticsOwnershipForCastTransmute@L60095`, `def.16.CastDynamicContext@L60110`, `def.16.CastRuntimeConversionHelpers@L60124`, `rule.16.CastVal-Id@L60158`, `rule.16.CastVal-Int-Int-Signed@L60174`, `rule.16.CastVal-Int-Int-Unsigned@L60190`, `rule.16.CastVal-Int-Float@L60206`
- `req.16.IntToFloatLoweringPreservesSignedness@L60222`, `rule.16.CastVal-Float-Float@L60235`, `rule.16.CastVal-Float-Int@L60251`, `rule.16.CastVal-Bool-Int@L60267`, `rule.16.CastVal-Int-Bool@L60285`, `rule.16.CastVal-Char-U32@L60303`, `rule.16.CastVal-U32-Char@L60319`, `rule.16.EvalSigma-Cast@L60335`
- `rule.16.EvalSigma-Cast-Panic@L60351`, `def.16.TransmuteVal@L60367`, `rule.16.EvalSigma-Transmute@L60380`, `rule.16.EvalSigma-Transmute-Ctrl@L60396`, `req.16.WidenDynamicOwnershipForCastTransmute@L60412`, `rule.16.Lower-Expr-Cast@L60427`, `rule.16.Lower-Expr-Transmute@L60443`, `rule.16.LowerCastTransmuteFamily@L60459`
- `diag.16.CastAndTransmuteExpressions@L60474`, `grammar.16.ConstructionExpressions@L60491`, `req.16.EnumConstructorAndRecordDefaultSyntaxResolution@L60513`, `rule.16.Parse-Tuple-Literal@L60528`, `rule.16.Parse-Array-Segment-Elem@L60544`, `rule.16.Parse-Array-Segment-Repeat@L60560`, `rule.16.Parse-Array-Segment-List-Empty@L60576`, `rule.16.Parse-Array-Segment-List-Single@L60591`
- `rule.16.Parse-Array-Segment-List-Comma@L60607`, `rule.16.Parse-Array-Literal@L60623`, `rule.16.Parse-Record-Literal-ModalState@L60639`, `rule.16.Parse-Record-Literal@L60655`, `rule.16.Parse-Qualified-Apply-Brace@L60671`, `rule.16.ConstructionListAndShorthandParsingFamily@L60687`, `def.16.FieldInitAst@L60702`, `def.16.ConstructionExprAst@L60715`
- `def.16.FieldInitNamesAndSet@L60728`, `req.16.QualifiedBraceApplicationResolution@L60742`, `req.16.QualifiedParenApplicationConstructionResolution@L60758`, `rule.16.T-Unit-Literal@L60773`, `rule.16.T-Tuple-Literal@L60788`, `def.16.ArraySegmentLength@L60804`, `rule.16.T-Array-Literal-Segments@L60818`, `def.16.RecordFieldNameSet@L60843`
- `rule.16.T-Record-Literal@L60857`, `rule.16.Record-FieldInit-Dup@L60873`, `rule.16.Record-FieldInit-Missing@L60889`, `rule.16.RecordFieldUnknownNotVisibleFamily@L60905`, `rule.16.Record-Field-NonBitcopy-Move@L60918`, `rule.16.EnumLiteralTypingFamily@L60934`, `def.16.RecordDefaultConstructionEligibility@L60947`, `rule.16.T-Record-Default@L60961`
- `rule.16.Record-Default-Init-Err@L60977`, `rule.16.EvalSigmaTupleConstructionFamily@L60995`, `rule.16.EvalSigmaArrayConstructionFamily@L61008`, `rule.16.EvalSigmaRecordConstructionFamily@L61021`, `rule.16.EvalSigmaEnumConstructionFamily@L61034`, `req.16.RecordDefaultConstructionRuntimeUsesCallRecord@L61047`, `rule.16.Lower-Expr-Tuple@L61062`, `rule.16.Lower-Expr-Array@L61078`
- `rule.16.Lower-Expr-Record@L61094`, `rule.16.LowerEnumConstructionFamily@L61110`, `rule.16.Lower-CallIR-RecordCtor@L61123`, `diag.16.ConstructionExpressions@L61141`, `grammar.16.ControlExpressions@L61158`, `req.16.ControlExpressionOwnership@L61182`, `rule.16.Parse-If-Expr@L61198`, `rule.16.Parse-If-Is-Single@L61214`
- `rule.16.Parse-If-Is-CaseList@L61230`, `rule.16.Parse-Loop-Expr@L61246`, `rule.16.Parse-Block-Expr@L61262`, `rule.16.ControlExpressionParsingRemainderFamily@L61278`, `def.16.ControlExprAst@L61293`, `def.16.ControlAstHelpers@L61306`, `def.16.LoopTypeInference@L61320`, `req.16.BlockTypingOwnershipForControlExpressions@L61344`
- `rule.16.T-If@L61363`, `rule.16.T-If-No-Else@L61379`, `rule.16.CheckIfFamily@L61395`, `req.16.PatternTypingOwnershipForControlExpressions@L61408`, `rule.16.T-If-Is@L61424`, `rule.16.T-If-Is-No-Else@L61440`, `rule.16.IfCaseTypingFamily@L61456`, `rule.16.CheckIfIsAndIfCaseFamily@L61469`
- `req.16.LoopInvariantTypingOwnership@L61483`, `rule.16.T-Loop-Infinite@L61496`, `rule.16.T-Loop-Conditional@L61512`, `rule.16.T-Loop-Iter@L61528`, `rule.16.AsyncIteratorLoopTypingFamily@L61544`, `rule.16.EvalSigma-If-True@L61559`, `rule.16.EvalSigma-If-False-None@L61575`, `rule.16.EvalSigma-If-False-Some@L61591`
- `rule.16.EvalSigma-If-Ctrl@L61607`, `rule.16.EvalSigma-If-Is@L61623`, `rule.16.EvalSigma-If-Is-Ctrl@L61639`, `rule.16.EvalSigma-If-Cases@L61655`, `rule.16.EvalSigma-If-Cases-Ctrl@L61671`, `rule.16.EvalIfCasesFamily@L61687`, `rule.16.EvalSigma-Block@L61700`, `def.16.LoopIterableTypePredicates@L61716`
- `def.16.LoopIteratorRuntime@L61734`, `def.16.LoopIterJudgement@L61767`, `rule.16.EvalSigma-Loop-Infinite-Step@L61780`, `rule.16.EvalSigma-Loop-Infinite-Continue@L61796`, `rule.16.EvalSigma-Loop-Infinite-Break@L61812`, `rule.16.EvalSigma-Loop-Infinite-Ctrl@L61828`, `rule.16.EvalSigma-Loop-Cond-False@L61844`, `rule.16.EvalSigma-Loop-Cond-True-Step@L61860`
- `rule.16.EvalSigma-Loop-Cond-Continue@L61876`, `rule.16.EvalSigma-Loop-Cond-Break@L61892`, `rule.16.EvalSigma-Loop-Cond-Ctrl@L61908`, `rule.16.EvalSigma-Loop-Cond-Body-Ctrl@L61924`, `rule.16.EvalSigma-Loop-Iter@L61940`, `rule.16.EvalSigma-Loop-Iter-Ctrl@L61956`, `rule.16.LoopIter-Done@L61972`, `rule.16.LoopIter-Step-Val@L61988`
- `rule.16.LoopIter-Step-Continue@L62004`, `rule.16.LoopIter-Step-Break@L62020`, `rule.16.LoopIter-Step-Ctrl@L62036`, `rule.16.Lower-Expr-If@L62054`, `rule.16.Lower-Expr-If-Is@L62070`, `rule.16.Lower-Expr-If-Cases@L62086`, `rule.16.LowerLoopExpressionFamily@L62102`, `rule.16.Lower-Expr-Block@L62115`
- `req.16.ControlExpressionLoweringOwnership@L62131`, `diag.16.ControlExpressions@L62146`, `req.16.ControlExpressionDiagnosticOwnership@L62159`, `grammar.16.EffectfulCoreExpressions@L62177`, `req.16.RegionAliasAllocRewrite@L62197`, `rule.16.Parse-Unary-Deref@L62212`, `rule.16.Parse-Unary-AddressOf@L62228`, `rule.16.Parse-Unary-Move@L62244`
- `rule.16.Postfix-Propagate@L62260`, `rule.16.Parse-Alloc-Implicit@L62276`, `rule.16.Parse-Unsafe-Expr@L62292`, `def.16.EffectfulCoreExprAst@L62310`, `rule.16.ResolveExpr-Alloc-Explicit-ByAlias@L62323`, `def.16.AddressOfStaticHelpers@L62342`, `rule.16.T-Unsafe-Expr@L62357`, `rule.16.Chk-Unsafe-Expr@L62373`
- `rule.16.T-AddrOf@L62389`, `rule.16.T-Deref-Ptr@L62405`, `rule.16.T-Deref-Raw@L62421`, `rule.16.DerefPlaceTypingFamily@L62437`, `rule.16.T-Move@L62450`, `rule.16.T-Alloc-Explicit@L62466`, `rule.16.T-Alloc-Implicit@L62482`, `def.16.SuccessMember@L62498`
- `rule.16.T-Propagate@L62511`, `def.16.SuccessMemberAsync@L62527`, `rule.16.T-Async-Try@L62540`, `rule.16.Async-Try-Infallible-Err@L62556`, `rule.16.EvalSigma-UnsafeBlock@L62574`, `rule.16.EvalSigma-AddressOf@L62590`, `rule.16.EvalSigma-Deref@L62606`, `rule.16.EvalSigma-Move@L62622`
- `rule.16.EvalSigma-Alloc-Implicit@L62638`, `rule.16.EvalSigma-Alloc-Implicit-Ctrl@L62654`, `rule.16.EvalSigma-Alloc-Explicit@L62670`, `rule.16.EvalSigma-Alloc-Explicit-Ctrl@L62686`, `rule.16.EvalSigma-Propagate-Success@L62702`, `rule.16.EvalSigma-Propagate-Success-Async@L62718`, `rule.16.EvalSigma-Propagate-Error@L62734`, `rule.16.EvalSigma-Propagate-Error-Async@L62750`
- `rule.16.EvalSigma-Propagate-Ctrl@L62767`, `def.16.ExprStateAndTerminalExpr@L62783`, `rule.16.StepSigma-Pure@L62798`, `rule.16.StepSigma-Alloc-Implicit@L62814`, `rule.16.StepSigma-Alloc-Implicit-Ctrl@L62830`, `rule.16.StepSigma-Alloc-Explicit@L62846`, `rule.16.StepSigma-Alloc-Explicit-Ctrl@L62862`, `rule.16.StepSigma-Block@L62878`
- `rule.16.StepSigma-UnsafeBlock@L62894`, `rule.16.StepSigma-Loop@L62910`, `rule.16.StepSigma-Stateful-Other@L62926`, `rule.16.Lower-Expr-UnsafeBlock@L62944`, `rule.16.Lower-Expr-Move@L62960`, `rule.16.Lower-Expr-AddressOf@L62976`, `rule.16.Lower-Expr-Deref@L62992`, `rule.16.Lower-Expr-Alloc@L63008`
- `rule.16.Lower-Expr-Propagate-Success@L63024`, `rule.16.Lower-Expr-Propagate-Return@L63040`, `req.16.EffectfulCoreLoweringMechanics@L63056`, `diag.16.EffectfulCoreExpressions@L63071`, `grammar.16.ClosureAndPipelineExpressions@L63088`, `req.16.ClosureParamTrailingComma@L63107`, `req.16.ClosureUnionParamParentheses@L63120`, `req.16.ClosureInvocationOrdinaryCallSyntax@L63133`
- `rule.16.Parse-Pipeline@L63148`, `rule.16.Parse-PipelineTail-Stop@L63164`, `rule.16.Parse-PipelineTail-Cons@L63180`, `rule.16.Parse-Closure-Expr@L63196`, `rule.16.Parse-Closure-Expr-Empty@L63212`, `rule.16.Parse-ClosureParams-Single@L63228`, `rule.16.Parse-ClosureParams-Cons@L63244`, `rule.16.Parse-ClosureParamType-Grouped@L63260`
- `rule.16.Parse-ClosureParamType-Plain@L63276`, `rule.16.Parse-ClosureParam-MoveTyped@L63292`, `rule.16.Parse-ClosureParam-MoveUntyped@L63308`, `rule.16.Parse-ClosureParam-Typed@L63324`, `rule.16.Parse-ClosureParam-Untyped@L63340`, `rule.16.Parse-ClosureRetOpt-Some@L63356`, `rule.16.Parse-ClosureRetOpt-None@L63372`, `rule.16.Parse-ClosureBody-Block@L63388`
- `rule.16.Parse-ClosureBody-Expr@L63404`, `def.16.ClosurePipelineAstForms@L63422`, `def.16.ClosureCaptureSets@L63441`, `def.16.ClosureEscapeClassification@L63461`, `def.16.ClosureParameterAccessors@L63476`, `rule.16.T-Closure-NonCapturing@L63490`, `rule.16.T-Closure-Capturing@L63508`, `rule.16.T-Closure-Escaping@L63527`
- `rule.16.K-Closure-Escape-Type@L63547`, `rule.16.Capture-Const@L63563`, `rule.16.Capture-Shared@L63579`, `rule.16.Capture-Unique-Err@L63595`, `rule.16.T-ClosureCall@L63611`, `rule.16.Infer-Closure-Params@L63627`, `rule.16.Infer-Closure-Params-Err@L63643`, `rule.16.Infer-Closure-Return@L63659`
- `req.16.ClosureSharedDependencyInference@L63675`, `def.16.ClosureCaptureBindingAccessors@L63688`, `rule.16.B-Closure-NonCapturing@L63712`, `rule.16.B-Closure-Capturing@L63728`, `rule.16.B-Closure-MoveCapture-Moved-Err@L63747`, `rule.16.B-Closure-MoveCapture-Immovable-Err@L63764`, `rule.16.B-Closure-RefCapture-Moved-Err@L63781`, `rule.16.T-Pipeline@L63798`
- `rule.16.T-Pipeline-NotCallable-Err@L63816`, `rule.16.T-Pipeline-TypeMismatch-Err@L63833`, `rule.16.T-Pipeline-ArgCount-Err@L63851`, `rule.16.B-Pipeline@L63868`, `req.16.ClosureParamInferenceFailure@L63884`, `req.16.ClosureSharedDependencyInferenceRestated@L63897`, `def.16.ClosureEnvironmentRuntimeModel@L63912`, `rule.16.EvalSigma-Closure-NonCapturing@L63948`
- `rule.16.EvalSigma-Closure-Capturing@L63964`, `def.16.MarkMoved@L63982`, `rule.16.EvalSigma-ClosureCall@L63996`, `def.16.ClosureCallRuntimeHelpers@L64014`, `rule.16.EvalSigma-ClosureCall-Ctrl@L64036`, `rule.16.EvalSigma-ClosureCall-Ctrl-Args@L64052`, `req.16.ClosureCallResolvedInternalFormRuntime@L64069`, `req.16.PipelineDesugaring@L64082`
- `rule.16.EvalSigma-Pipeline-Func@L64095`, `rule.16.EvalSigma-Pipeline-Closure@L64112`, `rule.16.EvalSigma-Pipeline-Ctrl-Left@L64129`, `rule.16.EvalSigma-Pipeline-Ctrl-Right@L64145`, `def.16.ClosureLoweringCaptureTypes@L64163`, `rule.16.Layout-ClosureEnv@L64179`, `rule.16.Layout-ClosureEnv-Empty@L64195`, `rule.16.Lower-Expr-Closure-NonCapturing@L64211`
- `rule.16.Lower-Expr-Closure-Capturing@L64227`, `def.16.LowerCaptureEnv@L64245`, `def.16.CapturedIdentifierLoweringHelpers@L64265`, `rule.16.Lower-CapturedIdent-Ref@L64280`, `req.16.LowerCapturedIdentRefTemporaries@L64297`, `rule.16.Lower-CapturedIdent-Move@L64310`, `def.16.ClosureEnvParam@L64327`, `def.16.ClosureCodeSig@L64340`
- `rule.16.Lower-Closure-Call@L64357`, `req.16.LowerClosureCallResolvedInternalForm@L64375`, `rule.16.Lower-Expr-Pipeline@L64388`, `def.16.LowerPipelineCallablePredicates@L64406`, `diag.16.ClosureAndPipelineExpressions@L64424`, `diag.16.ExpressionDiagnosticsSupplement@L64439`

#### `spec.patterns`

Count: 161 total; 161 required; 0 recommended; 0 informative. Ledger line span: L64193-L66741.

- `grammar.17.BasicPatterns@L64480`, `rule.17.Parse-Pattern-Literal@L64497`, `rule.17.Parse-Pattern-Wildcard@L64513`, `rule.17.Parse-Pattern-Identifier@L64529`, `def.17.PatternAstForms@L64547`, `def.17.PatternJudgements@L64563`, `def.17.PermWrap@L64576`, `rule.17.Pat-StripPerm@L64591`
- `def.17.PatternNameExtractionJudgement@L64607`, `rule.17.Pat-Ident-Names@L64620`, `rule.17.Pat-Wild@L64634`, `rule.17.Pat-Lit@L64649`, `rule.17.Pat-Dup-R-Err@L64664`, `rule.17.Pat-Wildcard-R@L64682`, `rule.17.Pat-Ident-R@L64697`, `rule.17.Pat-Literal-R@L64712`
- `def.17.PatternBindingEnvironment@L64730`, `def.17.PatternMatchingJudgementAndLiteralTypes@L64745`, `rule.17.Match-Wildcard@L64767`, `rule.17.Match-Ident@L64782`, `rule.17.Match-Literal@L64797`, `req.17.BasicPatternLoweringShared@L64815`, `diag.17.BasicPatterns@L64830`, `grammar.17.TupleRecordPatterns@L64847`
- `req.17.TuplePatternSingleElementSemicolon@L64866`, `rule.17.Parse-Pattern-Tuple@L64881`, `rule.17.Parse-Pattern-Record@L64897`, `rule.17.Parse-TuplePatternElems-Empty@L64913`, `rule.17.Parse-TuplePatternElems-Single@L64929`, `rule.17.Parse-TuplePatternElems-Many@L64945`, `rule.17.Parse-FieldPatternList-Empty@L64961`, `rule.17.Parse-FieldPatternList-Cons@L64977`
- `rule.17.Parse-FieldPattern@L64993`, `rule.17.Parse-FieldPatternTailOpt-None@L65009`, `rule.17.Parse-FieldPatternTailOpt-Yes@L65025`, `rule.17.Parse-FieldPatternTail-End@L65041`, `rule.17.Parse-FieldPatternTail-TrailingComma@L65057`, `rule.17.Parse-FieldPatternTail-Comma@L65073`, `def.17.FieldPatternAstAndAccessors@L65091`, `rule.17.PatNames-TuplePattern@L65108`
- `rule.17.Pat-Record-Field-Explicit@L65123`, `rule.17.Pat-Record-Field-Implicit@L65139`, `rule.17.PatNames-RecordPattern@L65154`, `rule.17.Pat-Tuple-R-Arity-Err@L65171`, `rule.17.Pat-Tuple-R@L65187`, `rule.17.Pat-Record-R@L65203`, `rule.17.RecordPattern-UnknownField@L65219`, `def.17.MatchRecordJudgement@L65237`
- `rule.17.MatchRecord-Empty@L65251`, `rule.17.MatchRecord-Cons-Implicit@L65266`, `rule.17.MatchRecord-Cons-Explicit@L65282`, `rule.17.Match-Tuple@L65298`, `rule.17.Match-Record@L65314`, `req.17.TupleRecordPatternLoweringShared@L65332`, `diag.17.TupleRecordPatterns@L65347`, `grammar.17.EnumModalPatterns@L65364`
- `req.17.EnumPayloadSingleElementTuple@L65382`, `rule.17.Parse-Pattern-Enum@L65397`, `rule.17.Parse-Pattern-Modal@L65413`, `rule.17.Parse-EnumPatternPayloadOpt-None@L65429`, `rule.17.Parse-EnumPayloadPatternElems-Empty@L65445`, `rule.17.Parse-EnumPayloadPatternElems-One@L65461`, `rule.17.Parse-EnumPayloadPatternElems-TrailingComma@L65477`, `rule.17.Parse-EnumPayloadPatternElems-Many@L65493`
- `rule.17.Parse-EnumPatternPayloadOpt-Tuple@L65509`, `rule.17.Parse-EnumPatternPayloadOpt-Record@L65525`, `rule.17.Parse-ModalPatternPayloadOpt-None@L65541`, `rule.17.Parse-ModalPatternPayloadOpt-Record@L65557`, `def.17.EnumModalPayloadPatterns@L65575`, `rule.17.Pat-Enum-None@L65589`, `rule.17.Pat-Enum-Tuple@L65604`, `rule.17.Pat-Enum-Record@L65620`
- `rule.17.Pat-Modal-None@L65636`, `rule.17.Pat-Modal-Record@L65651`, `rule.17.Pat-Enum-Unit-R@L65669`, `rule.17.Pat-Enum-Tuple-R@L65685`, `rule.17.Pat-Enum-Record-R@L65701`, `rule.17.Pat-Modal-R@L65717`, `rule.17.Pat-Modal-State-R@L65733`, `def.17.MatchModalJudgement@L65751`
- `rule.17.Match-Modal-Empty@L65764`, `rule.17.Match-Modal-Record@L65779`, `rule.17.Match-Enum-Unit@L65795`, `rule.17.Match-Enum-Tuple@L65811`, `rule.17.Match-Enum-Record@L65827`, `rule.17.Match-Modal-General@L65843`, `rule.17.Match-Modal-State@L65859`, `req.17.EnumModalPatternLoweringShared@L65877`
- `diag.17.EnumModalPatterns@L65892`, `grammar.17.RangePatterns@L65909`, `rule.17.Parse-Pattern@L65926`, `rule.17.Parse-Pattern-Err@L65942`, `rule.17.Parse-Pattern-Range-None@L65958`, `rule.17.Parse-Pattern-Range@L65974`, `def.17.RangePatternAst@L65992`, `rule.17.Pat-Range-R@L66008`
- `rule.17.RangePattern-NonConst@L66024`, `rule.17.RangePattern-Empty@L66040`, `def.17.ConstPat@L66058`, `rule.17.Match-Range-Inclusive@L66071`, `rule.17.Match-Range-Exclusive@L66087`, `req.17.RangePatternLoweringShared@L66104`, `diag.17.RangePatterns@L66119`, `grammar.17.CaseClauses@L66136`
- `def.17.CaseClauseParsingGroup@L66154`, `rule.17.Parse-IfCases-Cons@L66167`, `rule.17.Parse-IfCase@L66183`, `rule.17.Parse-IfCasesTail-End@L66199`, `rule.17.Parse-IfCasesTail-Else@L66215`, `rule.17.Parse-IfCasesTail-Cons@L66231`, `def.17.IfCaseAst@L66249`, `def.17.BindOrder@L66262`
- `req.17.CaseBodyTypingScope@L66277`, `def.17.IfCaseEvaluationJudgements@L66292`, `rule.17.EvalIfCase-Fail@L66306`, `rule.17.EvalIfCase-Hit@L66322`, `rule.17.EvalIfCases-Head@L66338`, `rule.17.EvalIfCases-Tail@L66354`, `rule.17.EvalIfCases-Else@L66370`, `rule.17.EvalIfCases-None@L66386`
- `def.17.PatternLoweringJudgements@L66404`, `rule.17.Lower-Pat-Correctness@L66418`, `def.17.IfCaseValueCorrect@L66434`, `rule.17.Lower-IfCases-Correctness@L66447`, `def.17.PatternTagHelpers@L66463`, `rule.17.TagOf-Enum@L66479`, `rule.17.TagOf-Modal@L66495`, `rule.17.Lower-BindList-Empty@L66511`
- `rule.17.Lower-BindList-Cons@L66526`, `rule.17.Lower-Pat-General@L66542`, `rule.17.Lower-Pat-Err@L66558`, `rule.17.Lower-IfCases@L66574`, `diag.17.CaseClauses@L66592`, `req.17.ExhaustivenessNoSyntax@L66609`, `req.17.ExhaustivenessNotParserOwned@L66624`, `def.17.ExhaustivenessIrrefutabilityHelpers@L66639`
- `def.17.EnumCaseCoverageHelpers@L66661`, `def.17.ModalCaseCoverageHelpers@L66675`, `def.17.UnionCaseCoverageHelpers@L66688`, `def.17.EnumCaseAnalysisGroup@L66705`, `rule.17.T-IfCase-Enum@L66718`, `def.17.ModalCaseAnalysisGroup@L66734`, `rule.17.T-IfCase-Modal@L66747`, `rule.17.IfCase-Modal-NonExhaustive@L66763`
- `def.17.UnionCaseAnalysisGroup@L66779`, `rule.17.T-IfCase-Union@L66792`, `rule.17.IfCase-Union-NonExhaustive@L66808`, `rule.17.Chk-IfCase-Union@L66824`, `def.17.OtherCaseAnalysisGroup@L66840`, `rule.17.T-IfCase-Other@L66853`, `rule.17.Chk-IfCase-Enum@L66869`, `rule.17.IfCase-Enum-NonExhaustive@L66885`
- `rule.17.Chk-IfCase-Modal@L66901`, `rule.17.Chk-IfCase-Other@L66917`, `rule.17.Chk-IfIs@L66933`, `rule.17.Chk-IfIs-No-Else@L66949`, `rule.17.IfCase-Unreachable@L66965`, `req.17.ExhaustivenessNoAdditionalDynamicSemantics@L66983`, `req.17.ExhaustivenessNoAdditionalLowering@L66998`, `diag.17.ExhaustivenessAndReachability@L67013`
- `diag.17.PatternDiagnosticsSupplement@L67028`
- `grammar.17.BasicPatterns@L64480`, `rule.17.Parse-Pattern-Literal@L64497`, `rule.17.Parse-Pattern-Wildcard@L64513`, `rule.17.Parse-Pattern-Identifier@L64529`, `def.17.PatternAstForms@L64547`, `def.17.PatternJudgements@L64563`, `def.17.PermWrap@L64576`, `rule.17.Pat-StripPerm@L64591`
- `def.17.PatternNameExtractionJudgement@L64607`, `rule.17.Pat-Ident-Names@L64620`, `rule.17.Pat-Wild@L64634`, `rule.17.Pat-Lit@L64649`, `rule.17.Pat-Dup-R-Err@L64664`, `rule.17.Pat-Wildcard-R@L64682`, `rule.17.Pat-Ident-R@L64697`, `rule.17.Pat-Literal-R@L64712`
- `def.17.PatternBindingEnvironment@L64730`, `def.17.PatternMatchingJudgementAndLiteralTypes@L64745`, `rule.17.Match-Wildcard@L64767`, `rule.17.Match-Ident@L64782`, `rule.17.Match-Literal@L64797`, `req.17.BasicPatternLoweringShared@L64815`, `diag.17.BasicPatterns@L64830`, `grammar.17.TupleRecordPatterns@L64847`
- `req.17.TuplePatternSingleElementSemicolon@L64866`, `rule.17.Parse-Pattern-Tuple@L64881`, `rule.17.Parse-Pattern-Record@L64897`, `rule.17.Parse-TuplePatternElems-Empty@L64913`, `rule.17.Parse-TuplePatternElems-Single@L64929`, `rule.17.Parse-TuplePatternElems-Many@L64945`, `rule.17.Parse-FieldPatternList-Empty@L64961`, `rule.17.Parse-FieldPatternList-Cons@L64977`
- `rule.17.Parse-FieldPattern@L64993`, `rule.17.Parse-FieldPatternTailOpt-None@L65009`, `rule.17.Parse-FieldPatternTailOpt-Yes@L65025`, `rule.17.Parse-FieldPatternTail-End@L65041`, `rule.17.Parse-FieldPatternTail-TrailingComma@L65057`, `rule.17.Parse-FieldPatternTail-Comma@L65073`, `def.17.FieldPatternAstAndAccessors@L65091`, `rule.17.PatNames-TuplePattern@L65108`
- `rule.17.Pat-Record-Field-Explicit@L65123`, `rule.17.Pat-Record-Field-Implicit@L65139`, `rule.17.PatNames-RecordPattern@L65154`, `rule.17.Pat-Tuple-R-Arity-Err@L65171`, `rule.17.Pat-Tuple-R@L65187`, `rule.17.Pat-Record-R@L65203`, `rule.17.RecordPattern-UnknownField@L65219`, `def.17.MatchRecordJudgement@L65237`
- `rule.17.MatchRecord-Empty@L65251`, `rule.17.MatchRecord-Cons-Implicit@L65266`, `rule.17.MatchRecord-Cons-Explicit@L65282`, `rule.17.Match-Tuple@L65298`, `rule.17.Match-Record@L65314`, `req.17.TupleRecordPatternLoweringShared@L65332`, `diag.17.TupleRecordPatterns@L65347`, `grammar.17.EnumModalPatterns@L65364`
- `req.17.EnumPayloadSingleElementTuple@L65382`, `rule.17.Parse-Pattern-Enum@L65397`, `rule.17.Parse-Pattern-Modal@L65413`, `rule.17.Parse-EnumPatternPayloadOpt-None@L65429`, `rule.17.Parse-EnumPayloadPatternElems-Empty@L65445`, `rule.17.Parse-EnumPayloadPatternElems-One@L65461`, `rule.17.Parse-EnumPayloadPatternElems-TrailingComma@L65477`, `rule.17.Parse-EnumPayloadPatternElems-Many@L65493`
- `rule.17.Parse-EnumPatternPayloadOpt-Tuple@L65509`, `rule.17.Parse-EnumPatternPayloadOpt-Record@L65525`, `rule.17.Parse-ModalPatternPayloadOpt-None@L65541`, `rule.17.Parse-ModalPatternPayloadOpt-Record@L65557`, `def.17.EnumModalPayloadPatterns@L65575`, `rule.17.Pat-Enum-None@L65589`, `rule.17.Pat-Enum-Tuple@L65604`, `rule.17.Pat-Enum-Record@L65620`
- `rule.17.Pat-Modal-None@L65636`, `rule.17.Pat-Modal-Record@L65651`, `rule.17.Pat-Enum-Unit-R@L65669`, `rule.17.Pat-Enum-Tuple-R@L65685`, `rule.17.Pat-Enum-Record-R@L65701`, `rule.17.Pat-Modal-R@L65717`, `rule.17.Pat-Modal-State-R@L65733`, `def.17.MatchModalJudgement@L65751`
- `rule.17.Match-Modal-Empty@L65764`, `rule.17.Match-Modal-Record@L65779`, `rule.17.Match-Enum-Unit@L65795`, `rule.17.Match-Enum-Tuple@L65811`, `rule.17.Match-Enum-Record@L65827`, `rule.17.Match-Modal-General@L65843`, `rule.17.Match-Modal-State@L65859`, `req.17.EnumModalPatternLoweringShared@L65877`
- `diag.17.EnumModalPatterns@L65892`, `grammar.17.RangePatterns@L65909`, `rule.17.Parse-Pattern@L65926`, `rule.17.Parse-Pattern-Err@L65942`, `rule.17.Parse-Pattern-Range-None@L65958`, `rule.17.Parse-Pattern-Range@L65974`, `def.17.RangePatternAst@L65992`, `rule.17.Pat-Range-R@L66008`
- `rule.17.RangePattern-NonConst@L66024`, `rule.17.RangePattern-Empty@L66040`, `def.17.ConstPat@L66058`, `rule.17.Match-Range-Inclusive@L66071`, `rule.17.Match-Range-Exclusive@L66087`, `req.17.RangePatternLoweringShared@L66104`, `diag.17.RangePatterns@L66119`, `grammar.17.CaseClauses@L66136`
- `def.17.CaseClauseParsingGroup@L66154`, `rule.17.Parse-IfCases-Cons@L66167`, `rule.17.Parse-IfCase@L66183`, `rule.17.Parse-IfCasesTail-End@L66199`, `rule.17.Parse-IfCasesTail-Else@L66215`, `rule.17.Parse-IfCasesTail-Cons@L66231`, `def.17.IfCaseAst@L66249`, `def.17.BindOrder@L66262`
- `req.17.CaseBodyTypingScope@L66277`, `def.17.IfCaseEvaluationJudgements@L66292`, `rule.17.EvalIfCase-Fail@L66306`, `rule.17.EvalIfCase-Hit@L66322`, `rule.17.EvalIfCases-Head@L66338`, `rule.17.EvalIfCases-Tail@L66354`, `rule.17.EvalIfCases-Else@L66370`, `rule.17.EvalIfCases-None@L66386`
- `def.17.PatternLoweringJudgements@L66404`, `rule.17.Lower-Pat-Correctness@L66418`, `def.17.IfCaseValueCorrect@L66434`, `rule.17.Lower-IfCases-Correctness@L66447`, `def.17.PatternTagHelpers@L66463`, `rule.17.TagOf-Enum@L66479`, `rule.17.TagOf-Modal@L66495`, `rule.17.Lower-BindList-Empty@L66511`
- `rule.17.Lower-BindList-Cons@L66526`, `rule.17.Lower-Pat-General@L66542`, `rule.17.Lower-Pat-Err@L66558`, `rule.17.Lower-IfCases@L66574`, `diag.17.CaseClauses@L66592`, `req.17.ExhaustivenessNoSyntax@L66609`, `req.17.ExhaustivenessNotParserOwned@L66624`, `def.17.ExhaustivenessIrrefutabilityHelpers@L66639`
- `def.17.EnumCaseCoverageHelpers@L66661`, `def.17.ModalCaseCoverageHelpers@L66675`, `def.17.UnionCaseCoverageHelpers@L66688`, `def.17.EnumCaseAnalysisGroup@L66705`, `rule.17.T-IfCase-Enum@L66718`, `def.17.ModalCaseAnalysisGroup@L66734`, `rule.17.T-IfCase-Modal@L66747`, `rule.17.IfCase-Modal-NonExhaustive@L66763`
- `def.17.UnionCaseAnalysisGroup@L66779`, `rule.17.T-IfCase-Union@L66792`, `rule.17.IfCase-Union-NonExhaustive@L66808`, `rule.17.Chk-IfCase-Union@L66824`, `def.17.OtherCaseAnalysisGroup@L66840`, `rule.17.T-IfCase-Other@L66853`, `rule.17.Chk-IfCase-Enum@L66869`, `rule.17.IfCase-Enum-NonExhaustive@L66885`
- `rule.17.Chk-IfCase-Modal@L66901`, `rule.17.Chk-IfCase-Other@L66917`, `rule.17.Chk-IfIs@L66933`, `rule.17.Chk-IfIs-No-Else@L66949`, `rule.17.IfCase-Unreachable@L66965`, `req.17.ExhaustivenessNoAdditionalDynamicSemantics@L66983`, `req.17.ExhaustivenessNoAdditionalLowering@L66998`, `diag.17.ExhaustivenessAndReachability@L67013`
- `diag.17.PatternDiagnosticsSupplement@L67028`

#### `spec.statements`

Count: 260 total; 260 required; 0 recommended; 0 informative. Ledger line span: L66771-L70869.

- `grammar.18.Blocks@L67058`, `req.18.BlockStatementExternalDefinitions@L67088`, `def.18.StatementTerminators@L67104`, `def.18.AttachStmtAttrs@L67119`, `rule.18.Parse-Statement@L67132`, `rule.18.Parse-Statement-Err@L67148`, `rule.18.Parse-Block@L67164`, `def.18.RequiredStatementTerminators@L67180`
- `rule.18.ConsumeTerminatorOpt-Req-Yes@L67193`, `rule.18.ConsumeTerminatorOpt-Req-No@L67209`, `rule.18.ConsumeTerminatorOpt-Opt-Yes@L67225`, `rule.18.ConsumeTerminatorOpt-Opt-No@L67241`, `def.18.SkipNL@L67257`, `rule.18.ParseStmtSeq-End@L67271`, `rule.18.ParseStmtSeq-TailExpr@L67287`, `rule.18.ParseStmtSeq-Cons@L67303`
- `def.18.SyncStmt@L67319`, `def.18.StatementAstForms@L67334`, `def.18.LastStmtAndResultType@L67347`, `def.18.BindingEnvironmentHelpers@L67366`, `def.18.StatementTypingJudgements@L67383`, `def.18.LoopFlag@L67396`, `def.18.ScopeStackTypeHelpers@L67409`, `rule.18.T-ErrorStmt@L67423`
- `rule.18.BlockInfo-Res@L67438`, `rule.18.BlockInfo-Res-Err@L67454`, `rule.18.BlockInfo-Tail@L67470`, `rule.18.BlockInfo-ReturnTail@L67486`, `rule.18.BlockInfo-Unit@L67502`, `rule.18.T-Block@L67518`, `req.18.BlockCheckingModeValidation@L67534`, `req.18.BlockExprExpressionFormOwnership@L67547`
- `def.18.StatementExecutionJudgements@L67562`, `def.18.ControlAndStatementOutcomes@L67575`, `def.18.BlockExitOutcome@L67594`, `def.18.BlockExit@L67610`, `def.18.EvalBlockBodySigma@L67623`, `def.18.EvalBlockSigma@L67641`, `def.18.EvalBlockBindSigma@L67654`, `def.18.EvalInScopeSigma@L67667`
- `def.18.PlaceEvaluationHelpersGroup@L67680`, `def.18.PlaceJudgements@L67693`, `rule.18.ExecSeq-Empty@L67707`, `rule.18.ExecSeq-Cons-Ok@L67722`, `rule.18.ExecSeq-Cons-Ctrl@L67738`, `rule.18.ExecSigma-Error@L67754`, `def.18.ExecState@L67769`, `rule.18.Step-Exec-Other-Ok@L67782`
- `rule.18.Step-Exec-Other-Ctrl@L67798`, `rule.18.Step-ExecSeq-Ok@L67814`, `rule.18.Step-ExecSeq-Ctrl@L67830`, `rule.18.Step-Exec-Defer@L67846`, `req.18.BlockExprEvalDelegatesToBlock@L67862`, `def.18.LowerStatementJudgements@L67877`, `rule.18.Lower-Stmt-Correctness@L67890`, `rule.18.Lower-Block-Correctness@L67906`
- `def.18.StatementLoweringTotality@L67922`, `rule.18.Lower-StmtList-Empty@L67936`, `rule.18.Lower-StmtList-Cons@L67951`, `rule.18.Lower-Block-Tail@L67967`, `rule.18.Lower-Block-Unit@L67983`, `rule.18.Lower-Stmt-Error@L67999`, `req.18.TemporaryCleanupLowering@L68014`, `def.18.BlockLoopLoweringTotality@L68046`
- `rule.18.Lower-Loop-Infinite@L68062`, `rule.18.Lower-Loop-Cond@L68078`, `rule.18.Lower-Loop-Iter@L68094`, `diag.18.Blocks@L68112`, `grammar.18.BindingStatements@L68129`, `rule.18.Parse-Binding-Stmt@L68147`, `rule.18.Parse-BindingAfterLetVar@L68163`, `rule.18.LetOrVarStmt-Let@L68179`
- `rule.18.LetOrVarStmt-Var@L68195`, `def.18.LetOrVarStmtAst@L68213`, `def.18.BindingAstAndAccessors@L68226`, `def.18.IntroEnt@L68249`, `rule.18.IntroAll-Empty@L68262`, `rule.18.IntroAll-Cons@L68277`, `rule.18.IntroAllVar-Empty@L68293`, `rule.18.IntroAllVar-Cons@L68308`
- `rule.18.T-LetStmt-Ann@L68324`, `rule.18.T-LetStmt-Ann-Mismatch@L68340`, `rule.18.T-LetStmt-Infer@L68356`, `rule.18.T-LetStmt-Infer-Err@L68372`, `req.18.VarStmtTypingMirrorsLet@L68388`, `rule.18.Let-Refutable-Pattern-Err@L68401`, `rule.18.B-LetVar-UniqueNonMove-Err@L68417`, `def.18.SuspendUniqueBind@L68433`
- `rule.18.B-LetVar@L68448`, `rule.18.Prov-LetVar-Ordinary@L68464`, `rule.18.Prov-LetVar-Region-Alias@L68480`, `rule.18.Prov-LetVar-Region-Fresh@L68496`, `def.18.BindVal@L68514`, `def.18.BindPatternRuntimeHelpers@L68527`, `rule.18.BindList-Empty@L68541`, `rule.18.BindList-Cons@L68556`
- `def.18.BindPattern@L68572`, `rule.18.ExecSigma-Let@L68585`, `rule.18.ExecSigma-Let-Ctrl@L68601`, `req.18.VarExecutionMirrorsLet@L68617`, `rule.18.Lower-Stmt-Let@L68632`, `rule.18.Lower-Stmt-Var@L68648`, `diag.18.BindingStatements@L68666`, `grammar.18.LocalUsingStatements@L68683`
- `rule.18.Parse-UsingLocal-Stmt@L68700`, `def.18.UsingLocalStmtAst@L68718`, `req.18.UsingLocalUsesUsingAlias@L68733`, `rule.18.T-UsingLocalStmt@L68746`, `rule.18.T-UsingLocalStmt-Err@L68762`, `req.18.UsingLocalAliasIdentity@L68778`, `rule.18.ExecSigma-UsingLocal@L68793`, `req.18.UsingLocalNoRuntimeEffect@L68808`
- `rule.18.Lower-Stmt-UsingLocal@L68823`, `req.18.UsingLocalNoRuntimeIR@L68838`, `diag.18.LocalUsingStatements@L68853`, `grammar.18.AssignmentStatements@L68870`, `rule.18.Parse-Assign-Stmt@L68888`, `rule.18.AssignOrCompound-Assign@L68904`, `rule.18.AssignOrCompound-Compound@L68920`, `def.18.AssignmentAstForms@L68938`
- `def.18.PlaceRoot@L68952`, `rule.18.T-Assign@L68971`, `rule.18.T-CompoundAssign@L68987`, `rule.18.Assign-NotPlace@L69003`, `rule.18.Assign-Immutable-Err@L69019`, `rule.18.Assign-Type-Err@L69035`, `rule.18.Assign-Const-Err@L69051`, `req.18.AssignmentBindingStateRules@L69067`
- `req.18.AssignmentProvenanceRules@L69080`, `req.18.AssignmentProvenanceEscapeFailures@L69093`, `def.18.AssignmentRootBinding@L69108`, `def.18.DropOnAssign@L69123`, `def.18.DropSubvalueJudgement@L69140`, `rule.18.DropSubvalue-Do@L69153`, `rule.18.DropSubvalue-Skip@L69169`, `rule.18.ExecSigma-Assign@L69185`
- `rule.18.ExecSigma-Assign-Ctrl@L69201`, `rule.18.ExecSigma-CompoundAssign@L69217`, `req.18.CompoundAssignControlPropagation@L69233`, `rule.18.Lower-Stmt-Assign@L69248`, `rule.18.Lower-Stmt-CompoundAssign@L69264`, `diag.18.AssignmentStatements@L69282`, `grammar.18.ExpressionStatements@L69299`, `rule.18.Parse-Expr-Stmt@L69316`
- `def.18.ExprStmtAst@L69334`, `rule.18.T-ExprStmt@L69349`, `req.18.ExprStmtStateAndProvenanceRules@L69365`, `rule.18.ExecSigma-ExprStmt@L69380`, `rule.18.Lower-Stmt-Expr@L69398`, `diag.18.ExpressionStatements@L69416`, `grammar.18.Defer@L69433`, `rule.18.Parse-Defer-Stmt@L69450`
- `def.18.DeferStmtAst@L69468`, `rule.18.T-DeferStmt@L69483`, `rule.18.Defer-NonUnit-Err@L69499`, `rule.18.Defer-NonLocal-Err@L69515`, `rule.18.HasNonLocalCtrl-Return@L69531`, `rule.18.HasNonLocalCtrl-Break@L69546`, `rule.18.HasNonLocalCtrl-Continue@L69562`, `req.18.HasNonLocalCtrlPropagation@L69578`
- `def.18.DeferSafe@L69591`, `req.18.DeferStateAndProvenancePreservation@L69604`, `rule.18.ExecSigma-Defer@L69619`, `req.18.DeferCleanupSmallStep@L69635`, `req.18.DeferCleanupBigStep@L69648`, `rule.18.Lower-Stmt-Defer@L69663`, `diag.18.Defer@L69680`, `grammar.18.Region@L69697`
- `rule.18.Parse-Region-Opts-None@L69716`, `rule.18.Parse-Region-Opts-Some@L69732`, `rule.18.Parse-Region-Alias-None@L69748`, `rule.18.Parse-Region-Alias-Some@L69764`, `rule.18.Parse-Region-Stmt@L69780`, `def.18.RegionStmtAst@L69798`, `def.18.RegionTypeAndFreshNameHelpers@L69811`, `def.18.RegionOptsExpr@L69825`
- `def.18.RegionBind@L69841`, `rule.18.T-RegionStmt@L69856`, `req.18.AnonymousRegionSyntheticBinding@L69872`, `req.18.RegionBindingState@L69885`, `req.18.RegionProvenance@L69898`, `def.18.BindRegionAlias@L69913`, `rule.18.ExecSigma-Region@L69927`, `rule.18.ExecSigma-Region-Ctrl@L69943`
- `def.18.RegionRelease@L69959`, `rule.18.Step-Exec-Region-Enter@L69972`, `rule.18.Step-Exec-Region-Enter-Ctrl@L69988`, `rule.18.Step-Exec-Region-Body@L70004`, `rule.18.Step-Exec-Region-Exit-Ok@L70020`, `rule.18.Step-Exec-Region-Exit-Ctrl@L70036`, `rule.18.Lower-Stmt-Region@L70054`, `diag.18.Region@L70072`
- `grammar.18.Frame@L70089`, `rule.18.Parse-Frame-Stmt@L70106`, `rule.18.Parse-Frame-Explicit@L70122`, `def.18.FrameStmtAst@L70140`, `def.18.InnermostActiveRegion@L70153`, `def.18.FrameBind@L70169`, `rule.18.T-FrameStmt-Implicit@L70186`, `rule.18.T-FrameStmt-Explicit@L70202`
- `rule.18.Frame-NoActiveRegion-Err@L70218`, `rule.18.Frame-Target-NotActive-Err@L70234`, `req.18.FrameSyntheticRegionBinding@L70250`, `req.18.FrameBindingState@L70263`, `req.18.FrameProvenance@L70276`, `def.18.FrameTargetResolution@L70291`, `def.18.FrameEnter@L70305`, `rule.18.ExecSigma-Frame-Implicit@L70318`
- `rule.18.ExecSigma-Frame-Explicit@L70334`, `def.18.FrameReset@L70350`, `rule.18.Step-Exec-Frame-Enter-Implicit@L70363`, `rule.18.Step-Exec-Frame-Enter-Explicit@L70379`, `rule.18.Step-Exec-Frame-Body@L70395`, `rule.18.Step-Exec-Frame-Exit-Ok@L70411`, `rule.18.Step-Exec-Frame-Exit-Ctrl@L70427`, `rule.18.Lower-Stmt-Frame-Implicit@L70445`
- `rule.18.Lower-Stmt-Frame-Explicit@L70461`, `diag.18.Frame@L70479`, `grammar.18.ControlTransferStatements@L70496`, `rule.18.Parse-Return-Stmt@L70515`, `rule.18.Parse-Break-Stmt@L70531`, `rule.18.Parse-Continue-Stmt@L70547`, `def.18.ControlTransferAstForms@L70565`, `rule.18.T-Return-Value@L70582`
- `rule.18.T-Return-Unit@L70598`, `rule.18.Return-Async-Type-Err@L70614`, `rule.18.Return-Async-Unit-Err@L70630`, `rule.18.Return-Type-Err@L70646`, `rule.18.Return-Unit-Err@L70662`, `rule.18.T-Break-Value@L70678`, `rule.18.T-Break-Unit@L70694`, `rule.18.Break-Outside-Loop@L70710`
- `rule.18.T-Continue@L70726`, `rule.18.Continue-Outside-Loop@L70742`, `req.18.ControlTransferBindingState@L70758`, `req.18.ControlTransferProvenance@L70771`, `rule.18.ExecSigma-Return@L70786`, `rule.18.ExecSigma-Return-Unit@L70802`, `rule.18.ExecSigma-Return-Ctrl@L70817`, `rule.18.ExecSigma-Break@L70833`
- `rule.18.ExecSigma-Break-Unit@L70849`, `rule.18.ExecSigma-Break-Ctrl@L70864`, `rule.18.ExecSigma-Continue@L70880`, `rule.18.Lower-Stmt-Return@L70897`, `rule.18.Lower-Stmt-Return-Unit@L70913`, `rule.18.Lower-Stmt-Break@L70928`, `rule.18.Lower-Stmt-Break-Unit@L70944`, `rule.18.Lower-Stmt-Continue@L70959`
- `req.18.ControlTransferTemporaryCleanupLowering@L70974`, `diag.18.ControlTransferStatements@L70994`, `grammar.18.UnsafeStatements@L71011`, `rule.18.Parse-Unsafe-Block@L71028`, `def.18.UnsafeBlockStmtAst@L71046`, `rule.18.T-UnsafeStmt@L71061`, `req.18.UnsafeStatementStateAndProvenance@L71077`, `diag.18.UnsafeRequiredOperationOwnership@L71090`
- `rule.18.ExecSigma-UnsafeStmt@L71105`, `rule.18.Lower-Stmt-UnsafeBlock@L71123`, `diag.18.UnsafeStatements@L71141`, `diag.18.StatementDiagnosticsSupplement@L71156`
- `grammar.18.Blocks@L67058`, `req.18.BlockStatementExternalDefinitions@L67088`, `def.18.StatementTerminators@L67104`, `def.18.AttachStmtAttrs@L67119`, `rule.18.Parse-Statement@L67132`, `rule.18.Parse-Statement-Err@L67148`, `rule.18.Parse-Block@L67164`, `def.18.RequiredStatementTerminators@L67180`
- `rule.18.ConsumeTerminatorOpt-Req-Yes@L67193`, `rule.18.ConsumeTerminatorOpt-Req-No@L67209`, `rule.18.ConsumeTerminatorOpt-Opt-Yes@L67225`, `rule.18.ConsumeTerminatorOpt-Opt-No@L67241`, `def.18.SkipNL@L67257`, `rule.18.ParseStmtSeq-End@L67271`, `rule.18.ParseStmtSeq-TailExpr@L67287`, `rule.18.ParseStmtSeq-Cons@L67303`
- `def.18.SyncStmt@L67319`, `def.18.StatementAstForms@L67334`, `def.18.LastStmtAndResultType@L67347`, `def.18.BindingEnvironmentHelpers@L67366`, `def.18.StatementTypingJudgements@L67383`, `def.18.LoopFlag@L67396`, `def.18.ScopeStackTypeHelpers@L67409`, `rule.18.T-ErrorStmt@L67423`
- `rule.18.BlockInfo-Res@L67438`, `rule.18.BlockInfo-Res-Err@L67454`, `rule.18.BlockInfo-Tail@L67470`, `rule.18.BlockInfo-ReturnTail@L67486`, `rule.18.BlockInfo-Unit@L67502`, `rule.18.T-Block@L67518`, `req.18.BlockCheckingModeValidation@L67534`, `req.18.BlockExprExpressionFormOwnership@L67547`
- `def.18.StatementExecutionJudgements@L67562`, `def.18.ControlAndStatementOutcomes@L67575`, `def.18.BlockExitOutcome@L67594`, `def.18.BlockExit@L67610`, `def.18.EvalBlockBodySigma@L67623`, `def.18.EvalBlockSigma@L67641`, `def.18.EvalBlockBindSigma@L67654`, `def.18.EvalInScopeSigma@L67667`
- `def.18.PlaceEvaluationHelpersGroup@L67680`, `def.18.PlaceJudgements@L67693`, `rule.18.ExecSeq-Empty@L67707`, `rule.18.ExecSeq-Cons-Ok@L67722`, `rule.18.ExecSeq-Cons-Ctrl@L67738`, `rule.18.ExecSigma-Error@L67754`, `def.18.ExecState@L67769`, `rule.18.Step-Exec-Other-Ok@L67782`
- `rule.18.Step-Exec-Other-Ctrl@L67798`, `rule.18.Step-ExecSeq-Ok@L67814`, `rule.18.Step-ExecSeq-Ctrl@L67830`, `rule.18.Step-Exec-Defer@L67846`, `req.18.BlockExprEvalDelegatesToBlock@L67862`, `def.18.LowerStatementJudgements@L67877`, `rule.18.Lower-Stmt-Correctness@L67890`, `rule.18.Lower-Block-Correctness@L67906`
- `def.18.StatementLoweringTotality@L67922`, `rule.18.Lower-StmtList-Empty@L67936`, `rule.18.Lower-StmtList-Cons@L67951`, `rule.18.Lower-Block-Tail@L67967`, `rule.18.Lower-Block-Unit@L67983`, `rule.18.Lower-Stmt-Error@L67999`, `req.18.TemporaryCleanupLowering@L68014`, `def.18.BlockLoopLoweringTotality@L68046`
- `rule.18.Lower-Loop-Infinite@L68062`, `rule.18.Lower-Loop-Cond@L68078`, `rule.18.Lower-Loop-Iter@L68094`, `diag.18.Blocks@L68112`, `grammar.18.BindingStatements@L68129`, `rule.18.Parse-Binding-Stmt@L68147`, `rule.18.Parse-BindingAfterLetVar@L68163`, `rule.18.LetOrVarStmt-Let@L68179`
- `rule.18.LetOrVarStmt-Var@L68195`, `def.18.LetOrVarStmtAst@L68213`, `def.18.BindingAstAndAccessors@L68226`, `def.18.IntroEnt@L68249`, `rule.18.IntroAll-Empty@L68262`, `rule.18.IntroAll-Cons@L68277`, `rule.18.IntroAllVar-Empty@L68293`, `rule.18.IntroAllVar-Cons@L68308`
- `rule.18.T-LetStmt-Ann@L68324`, `rule.18.T-LetStmt-Ann-Mismatch@L68340`, `rule.18.T-LetStmt-Infer@L68356`, `rule.18.T-LetStmt-Infer-Err@L68372`, `req.18.VarStmtTypingMirrorsLet@L68388`, `rule.18.Let-Refutable-Pattern-Err@L68401`, `rule.18.B-LetVar-UniqueNonMove-Err@L68417`, `def.18.SuspendUniqueBind@L68433`
- `rule.18.B-LetVar@L68448`, `rule.18.Prov-LetVar-Ordinary@L68464`, `rule.18.Prov-LetVar-Region-Alias@L68480`, `rule.18.Prov-LetVar-Region-Fresh@L68496`, `def.18.BindVal@L68514`, `def.18.BindPatternRuntimeHelpers@L68527`, `rule.18.BindList-Empty@L68541`, `rule.18.BindList-Cons@L68556`
- `def.18.BindPattern@L68572`, `rule.18.ExecSigma-Let@L68585`, `rule.18.ExecSigma-Let-Ctrl@L68601`, `req.18.VarExecutionMirrorsLet@L68617`, `rule.18.Lower-Stmt-Let@L68632`, `rule.18.Lower-Stmt-Var@L68648`, `diag.18.BindingStatements@L68666`, `grammar.18.LocalUsingStatements@L68683`
- `rule.18.Parse-UsingLocal-Stmt@L68700`, `def.18.UsingLocalStmtAst@L68718`, `req.18.UsingLocalUsesUsingAlias@L68733`, `rule.18.T-UsingLocalStmt@L68746`, `rule.18.T-UsingLocalStmt-Err@L68762`, `req.18.UsingLocalAliasIdentity@L68778`, `rule.18.ExecSigma-UsingLocal@L68793`, `req.18.UsingLocalNoRuntimeEffect@L68808`
- `rule.18.Lower-Stmt-UsingLocal@L68823`, `req.18.UsingLocalNoRuntimeIR@L68838`, `diag.18.LocalUsingStatements@L68853`, `grammar.18.AssignmentStatements@L68870`, `rule.18.Parse-Assign-Stmt@L68888`, `rule.18.AssignOrCompound-Assign@L68904`, `rule.18.AssignOrCompound-Compound@L68920`, `def.18.AssignmentAstForms@L68938`
- `def.18.PlaceRoot@L68952`, `rule.18.T-Assign@L68971`, `rule.18.T-CompoundAssign@L68987`, `rule.18.Assign-NotPlace@L69003`, `rule.18.Assign-Immutable-Err@L69019`, `rule.18.Assign-Type-Err@L69035`, `rule.18.Assign-Const-Err@L69051`, `req.18.AssignmentBindingStateRules@L69067`
- `req.18.AssignmentProvenanceRules@L69080`, `req.18.AssignmentProvenanceEscapeFailures@L69093`, `def.18.AssignmentRootBinding@L69108`, `def.18.DropOnAssign@L69123`, `def.18.DropSubvalueJudgement@L69140`, `rule.18.DropSubvalue-Do@L69153`, `rule.18.DropSubvalue-Skip@L69169`, `rule.18.ExecSigma-Assign@L69185`
- `rule.18.ExecSigma-Assign-Ctrl@L69201`, `rule.18.ExecSigma-CompoundAssign@L69217`, `req.18.CompoundAssignControlPropagation@L69233`, `rule.18.Lower-Stmt-Assign@L69248`, `rule.18.Lower-Stmt-CompoundAssign@L69264`, `diag.18.AssignmentStatements@L69282`, `grammar.18.ExpressionStatements@L69299`, `rule.18.Parse-Expr-Stmt@L69316`
- `def.18.ExprStmtAst@L69334`, `rule.18.T-ExprStmt@L69349`, `req.18.ExprStmtStateAndProvenanceRules@L69365`, `rule.18.ExecSigma-ExprStmt@L69380`, `rule.18.Lower-Stmt-Expr@L69398`, `diag.18.ExpressionStatements@L69416`, `grammar.18.Defer@L69433`, `rule.18.Parse-Defer-Stmt@L69450`
- `def.18.DeferStmtAst@L69468`, `rule.18.T-DeferStmt@L69483`, `rule.18.Defer-NonUnit-Err@L69499`, `rule.18.Defer-NonLocal-Err@L69515`, `rule.18.HasNonLocalCtrl-Return@L69531`, `rule.18.HasNonLocalCtrl-Break@L69546`, `rule.18.HasNonLocalCtrl-Continue@L69562`, `req.18.HasNonLocalCtrlPropagation@L69578`
- `def.18.DeferSafe@L69591`, `req.18.DeferStateAndProvenancePreservation@L69604`, `rule.18.ExecSigma-Defer@L69619`, `req.18.DeferCleanupSmallStep@L69635`, `req.18.DeferCleanupBigStep@L69648`, `rule.18.Lower-Stmt-Defer@L69663`, `diag.18.Defer@L69680`, `grammar.18.Region@L69697`
- `rule.18.Parse-Region-Opts-None@L69716`, `rule.18.Parse-Region-Opts-Some@L69732`, `rule.18.Parse-Region-Alias-None@L69748`, `rule.18.Parse-Region-Alias-Some@L69764`, `rule.18.Parse-Region-Stmt@L69780`, `def.18.RegionStmtAst@L69798`, `def.18.RegionTypeAndFreshNameHelpers@L69811`, `def.18.RegionOptsExpr@L69825`
- `def.18.RegionBind@L69841`, `rule.18.T-RegionStmt@L69856`, `req.18.AnonymousRegionSyntheticBinding@L69872`, `req.18.RegionBindingState@L69885`, `req.18.RegionProvenance@L69898`, `def.18.BindRegionAlias@L69913`, `rule.18.ExecSigma-Region@L69927`, `rule.18.ExecSigma-Region-Ctrl@L69943`
- `def.18.RegionRelease@L69959`, `rule.18.Step-Exec-Region-Enter@L69972`, `rule.18.Step-Exec-Region-Enter-Ctrl@L69988`, `rule.18.Step-Exec-Region-Body@L70004`, `rule.18.Step-Exec-Region-Exit-Ok@L70020`, `rule.18.Step-Exec-Region-Exit-Ctrl@L70036`, `rule.18.Lower-Stmt-Region@L70054`, `diag.18.Region@L70072`
- `grammar.18.Frame@L70089`, `rule.18.Parse-Frame-Stmt@L70106`, `rule.18.Parse-Frame-Explicit@L70122`, `def.18.FrameStmtAst@L70140`, `def.18.InnermostActiveRegion@L70153`, `def.18.FrameBind@L70169`, `rule.18.T-FrameStmt-Implicit@L70186`, `rule.18.T-FrameStmt-Explicit@L70202`
- `rule.18.Frame-NoActiveRegion-Err@L70218`, `rule.18.Frame-Target-NotActive-Err@L70234`, `req.18.FrameSyntheticRegionBinding@L70250`, `req.18.FrameBindingState@L70263`, `req.18.FrameProvenance@L70276`, `def.18.FrameTargetResolution@L70291`, `def.18.FrameEnter@L70305`, `rule.18.ExecSigma-Frame-Implicit@L70318`
- `rule.18.ExecSigma-Frame-Explicit@L70334`, `def.18.FrameReset@L70350`, `rule.18.Step-Exec-Frame-Enter-Implicit@L70363`, `rule.18.Step-Exec-Frame-Enter-Explicit@L70379`, `rule.18.Step-Exec-Frame-Body@L70395`, `rule.18.Step-Exec-Frame-Exit-Ok@L70411`, `rule.18.Step-Exec-Frame-Exit-Ctrl@L70427`, `rule.18.Lower-Stmt-Frame-Implicit@L70445`
- `rule.18.Lower-Stmt-Frame-Explicit@L70461`, `diag.18.Frame@L70479`, `grammar.18.ControlTransferStatements@L70496`, `rule.18.Parse-Return-Stmt@L70515`, `rule.18.Parse-Break-Stmt@L70531`, `rule.18.Parse-Continue-Stmt@L70547`, `def.18.ControlTransferAstForms@L70565`, `rule.18.T-Return-Value@L70582`
- `rule.18.T-Return-Unit@L70598`, `rule.18.Return-Async-Type-Err@L70614`, `rule.18.Return-Async-Unit-Err@L70630`, `rule.18.Return-Type-Err@L70646`, `rule.18.Return-Unit-Err@L70662`, `rule.18.T-Break-Value@L70678`, `rule.18.T-Break-Unit@L70694`, `rule.18.Break-Outside-Loop@L70710`
- `rule.18.T-Continue@L70726`, `rule.18.Continue-Outside-Loop@L70742`, `req.18.ControlTransferBindingState@L70758`, `req.18.ControlTransferProvenance@L70771`, `rule.18.ExecSigma-Return@L70786`, `rule.18.ExecSigma-Return-Unit@L70802`, `rule.18.ExecSigma-Return-Ctrl@L70817`, `rule.18.ExecSigma-Break@L70833`
- `rule.18.ExecSigma-Break-Unit@L70849`, `rule.18.ExecSigma-Break-Ctrl@L70864`, `rule.18.ExecSigma-Continue@L70880`, `rule.18.Lower-Stmt-Return@L70897`, `rule.18.Lower-Stmt-Return-Unit@L70913`, `rule.18.Lower-Stmt-Break@L70928`, `rule.18.Lower-Stmt-Break-Unit@L70944`, `rule.18.Lower-Stmt-Continue@L70959`
- `req.18.ControlTransferTemporaryCleanupLowering@L70974`, `diag.18.ControlTransferStatements@L70994`, `grammar.18.UnsafeStatements@L71011`, `rule.18.Parse-Unsafe-Block@L71028`, `def.18.UnsafeBlockStmtAst@L71046`, `rule.18.T-UnsafeStmt@L71061`, `req.18.UnsafeStatementStateAndProvenance@L71077`, `diag.18.UnsafeRequiredOperationOwnership@L71090`
- `rule.18.ExecSigma-UnsafeStmt@L71105`, `rule.18.Lower-Stmt-UnsafeBlock@L71123`, `diag.18.UnsafeStatements@L71141`, `diag.18.StatementDiagnosticsSupplement@L71156`

#### `spec.key-system`

Count: 185 total; 175 required; 0 recommended; 0 informative. Ledger line span: L70903-L74064.

- `grammar.19.KeyPaths@L71190`, `parse.19.KeyPathRules@L71212`, `ast.19.KeyPathForms@L71238`, `requirement.19.KeyPathWellFormedness@L71263`, `requirement.19.KeyAnalysisSharedOnly@L71276`, `def.19.RootExtraction@L71291`, `def.19.ObjectIdentity@L71315`, `def.19.KeyPathFormation@L71336`
- `requirement.19.PointerDereferenceKeyAccess@L71352`, `requirement.19.SharedDynamicClassObjects@L71370`, `def.19.DynMethods@L71383`, `rule.19.K-Witness-Shared-WF@L71396`, `requirement.19.SharedDynamicClassRejectsMutatingReceivers@L71412`, `requirement.19.RuntimeKeyRootIdentityConstraints@L71427`, `def.19.SharedDynamicMethodCallKeyPath@L71440`, `def.19.KeyLoweringForms@L71457`
- `rule.19.Lower-KeyPath@L71472`, `rule.19.Lower-KeyAccess-Uncovered@L71488`, `rule.19.Lower-KeyAccess-Covered@L71504`, `diagnostics.19.KeyPaths@L71522`, `grammar.19.KeyAcquisitionBlocks@L71546`, `requirement.19.OrderedKeyBlockModifier@L71566`, `parse.19.KeyBlockRules@L71581`, `ast.19.KeyBlockForms@L71614`
- `def.19.KeyTriple@L71647`, `rule.19.K-Mode-Read@L71673`, `rule.19.K-Mode-Write@L71689`, `requirement.19.RestrictiveContextApplies@L71705`, `def.19.ReadContexts@L71718`, `def.19.WriteContexts@L71740`, `def.19.KeyStateContext@L71763`, `def.19.Covered@L71786`
- `requirement.19.ValidKeyContext@L71801`, `rule.19.K-Acquire-New@L71820`, `rule.19.K-Acquire-Covered@L71836`, `requirement.19.KeyAcquisitionEvaluationOrder@L71852`, `rule.19.K-Block-Acquire@L71867`, `rule.19.K-Read-Block-No-Write@L71883`, `requirement.19.KeyCoarseningInlineMarker@L71901`, `rule.19.K-Coarsen-Inline@L71914`
- `requirement.19.FieldKeyBoundary@L71930`, `requirement.19.ClosureDependencySetConsumption@L71945`, `def.19.SharedCaptures@L71958`, `def.19.LocalClosureKeyPath@L71971`, `rule.19.K-Closure-Escape-Keys@L71988`, `requirement.19.EscapingClosureSharedLifetime@L72004`, `requirement.19.EscapingClosureRuntimeIdentityCoverage@L72017`, `requirement.19.KeyBlockCanonicalOrderReferences@L72036`
- `def.19.KeyBlockRuntimeJudgments@L72049`, `def.19.AcquireKeysSigma@L72062`, `def.19.ReleaseKeysSigma@L72079`, `def.19.ModeOf@L72095`, `rule.19.ExecSigma-KeyBlock@L72112`, `rule.19.ExecSigma-KeyBlock-Ctrl@L72128`, `rule.19.Step-Exec-KeyBlock-Enter@L72144`, `rule.19.Step-Exec-KeyBlock-Body@L72160`
- `rule.19.Step-Exec-KeyBlock-Exit-Ok@L72176`, `rule.19.Step-Exec-KeyBlock-Exit-Ctrl@L72192`, `requirement.19.ScopeExitKeyRelease@L72208`, `requirement.19.LocalClosureInvocationSharedCaptures@L72223`, `requirement.19.EscapingClosureInvocationSharedCaptures@L72241`, `def.19.LowerKeyPathsEmpty@L72261`, `def.19.LowerKeyPathsCons@L72274`, `rule.19.Lower-Stmt-KeyBlock@L72287`
- `requirement.19.KeyScopeBound@L72308`, `requirement.19.KeyEscapeRestrictions@L72323`, `requirement.19.FineGrainedKeyLoopWarning@L72338`, `requirement.19.KeyEscapeDiagnosticPrecedence@L72351`, `diagnostics.19.KeyAcquisitionBlocks@L72364`, `requirement.19.ConflictDetectionNoAdditionalSyntax@L72394`, `requirement.19.ConflictDetectionNoAdditionalParsingRules@L72409`, `def.19.PrefixAndDisjoint@L72424`
- `def.19.KeyPathOrdering@L72439`, `def.19.KeyCompatibility@L72474`, `def.19.IndexEquivalence@L72505`, `requirement.19.IndexEquivalenceConservativeSubset@L72528`, `rule.19.K-Disjoint-Safe@L72541`, `rule.19.K-Prefix-Coverage@L72557`, `def.19.DynamicIndexDisjointness@L72575`, `requirement.19.DynamicIndexDisjointnessConservativeSubset@L72598`
- `rule.19.K-Dynamic-Index-Conflict@L72611`, `def.19.ReadThenWrite@L72629`, `requirement.19.ReadThenWriteDiagnosticSurface@L72646`, `requirement.19.ReadThenWriteOtherWriteForms@L72659`, `rule.19.K-Read-Write-Reject@L72672`, `rule.19.K-RMW-Permitted@L72688`, `rule.19.K-RMW-Explicit-Warn@L72704`, `rule.19.K-RMW-Contention-Warn@L72720`
- `def.19.OrderedComparablePaths@L72736`, `rule.19.K-Ordered-Ok@L72752`, `rule.19.K-Ordered-Base-Err@L72768`, `rule.19.K-Ordered-Redundant-Warn@L72784`, `requirement.19.CanonicalOrderDynamicUse@L72802`, `requirement.19.KeyConflictRuntimeCompatibility@L72815`, `def.19.LowerConflictChecks@L72830`, `rule.19.Lower-Key-ConflictChecks@L72847`
- `diagnostics.19.ConflictDetection@L72865`, `requirement.19.NestedReleaseNoAdditionalSyntax@L72890`, `requirement.19.NestedReleaseNoAdditionalParsingRules@L72905`, `ast.19.NestedReleaseForm@L72920`, `rule.19.K-Nested-Same-Path@L72935`, `def.19.SharedParam@L72956`, `def.19.DirectCalleeAccesses@L72970`, `def.19.CalleeAccessSummary@L72983`
- `def.19.CalleeAccessInstantiation@L72996`, `rule.19.K-Reentrant@L73011`, `requirement.19.UnknownCalleeAccessWarning@L73026`, `rule.19.CallSharedArgumentNoKeyAcquisition@L73041`, `requirement.19.StaleOkSuppressesReleaseWarning@L73056`, `rule.19.K-Release-SameMode-Err@L73069`, `requirement.19.NestedReleaseExecutionSequence@L73087`, `rule.19.K-Release-Sequence@L73106`
- `requirement.19.NestedReleaseInterleavingWindow@L73126`, `def.19.HeldKeyAccessors@L73139`, `def.19.ReleasedKeyState@L73155`, `rule.19.ExecSigma-KeyBlock-Release@L73175`, `rule.19.Lower-Stmt-KeyBlock-Release@L73193`, `diagnostics.19.NestedRelease@L73216`, `grammar.19.SpeculativeExecution@L73239`, `parse.19.SpeculativeBlocks@L73256`
- `ast.19.SpeculativeBlockForm@L73271`, `def.19.SpeculativeSetsAndStates@L73284`, `rule.19.K-Spec-Write-Required@L73312`, `rule.19.K-Spec-Pure-Body@L73328`, `requirement.19.SpeculativePermittedOperations@L73344`, `requirement.19.SpeculativeProhibitedOperations@L73362`, `def.19.IsCallLike@L73382`, `rule.19.K-Spec-No-Nested-Key@L73395`
- `rule.19.K-Spec-No-Impure-Call@L73411`, `rule.19.K-Spec-No-Memory-Ordering@L73427`, `rule.19.K-Spec-No-Wait@L73443`, `rule.19.K-Spec-No-Defer@L73459`, `rule.19.K-Spec-No-Release@L73475`, `rule.19.ExecSigma-KeyBlock-Speculative@L73495`, `def.19.SpecLoop@L73511`, `rule.19.Spec-Start@L73532`
- `rule.19.Spec-Snapshot@L73547`, `rule.19.Spec-Exec-Ok@L73563`, `rule.19.Spec-Exec-Panic@L73579`, `rule.19.Spec-Commit-Success@L73595`, `rule.19.Spec-Commit-Fail-Retry@L73611`, `rule.19.Spec-Commit-Fail-Fallback@L73627`, `rule.19.Spec-Retry@L73643`, `rule.19.Spec-Fallback@L73658`
- `rule.19.SpecBlock-Ok@L73674`, `rule.19.SpecBlock-Panic@L73690`, `def.19.SpeculativeRuntimeHelpers@L73706`, `requirement.19.SpeculativePanicDiscardsWrites@L73727`, `requirement.19.SpeculativeAtomicity@L73740`, `requirement.19.SpeculativeAbstractSemanticsAndFallback@L73753`, `def.19.SpeculativeIR@L73770`, `rule.19.Lower-Stmt-KeyBlock-Speculative@L73783`
- `diagnostics.19.SpeculativeExecution@L73803`, `requirement.19.DynamicKeyVerificationNoAdditionalSyntax@L73831`, `requirement.19.DynamicKeyVerificationNoAdditionalParsingRules@L73846`, `def.19.StaticallySafeConditions@L73861`, `requirement.19.StaticallySafeSoundProofRequired@L73883`, `rule.19.K-Static-Safe@L73902`, `requirement.19.NoRuntimeSyncMeaning@L73918`, `rule.19.K-Static-Required@L73933`
- `requirement.19.RuntimeSynchronizationRequirements@L73951`, `requirement.19.DynamicIndexRuntimeOrdering@L73969`, `requirement.19.DynamicIndexedPathCoarsening@L73988`, `requirement.19.CanonicalOrderDeadlockFreedom@L74003`, `requirement.19.StaticAndRuntimeKeySafetyEquivalence@L74018`, `rule.19.K-Dynamic-Permitted@L74033`, `requirement.19.DynamicContextStaticSafeLowering@L74049`, `diagnostics.19.DynamicKeyVerification@L74064`
- `grammar.19.MemoryOrdering@L74085`, `parse.19.MemoryOrdering@L74105`, `ast.19.MemoryOrderingForms@L74122`, `requirement.19.MemoryOrderingDefaultsAndKeySemantics@L74145`, `def.19.MemoryOrderingLevels@L74160`, `requirement.19.MemoryOrderAttributeAttachment@L74181`, `requirement.19.ExpressionMemoryOrderWellFormedness@L74199`, `requirement.19.MemoryOrderDoesNotAlterKeySemantics@L74212`
- `requirement.19.MemoryOrderNotInsideSpeculativeBlocks@L74225`, `rule.19.T-Fence@L74238`, `requirement.19.FenceContextAndHeldKeys@L74254`, `requirement.19.FenceEvaluation@L74269`, `requirement.19.FenceOrderingConstraints@L74286`, `requirement.19.FenceNoProgramVisibleStorageAccess@L74303`, `rule.19.Lower-Expr-Fence@L74318`, `rule.19.Lower-Ordered-Access@L74333`
- `diagnostics.19.MemoryOrdering@L74351`
- `grammar.19.KeyPaths@L71190`, `parse.19.KeyPathRules@L71212`, `ast.19.KeyPathForms@L71238`, `requirement.19.KeyPathWellFormedness@L71263`, `requirement.19.KeyAnalysisSharedOnly@L71276`, `def.19.RootExtraction@L71291`, `def.19.ObjectIdentity@L71315`, `def.19.KeyPathFormation@L71336`
- `requirement.19.PointerDereferenceKeyAccess@L71352`, `requirement.19.SharedDynamicClassObjects@L71370`, `def.19.DynMethods@L71383`, `rule.19.K-Witness-Shared-WF@L71396`, `requirement.19.SharedDynamicClassRejectsMutatingReceivers@L71412`, `requirement.19.RuntimeKeyRootIdentityConstraints@L71427`, `def.19.SharedDynamicMethodCallKeyPath@L71440`, `def.19.KeyLoweringForms@L71457`
- `rule.19.Lower-KeyPath@L71472`, `rule.19.Lower-KeyAccess-Uncovered@L71488`, `rule.19.Lower-KeyAccess-Covered@L71504`, `diagnostics.19.KeyPaths@L71522`, `grammar.19.KeyAcquisitionBlocks@L71546`, `requirement.19.OrderedKeyBlockModifier@L71566`, `parse.19.KeyBlockRules@L71581`, `ast.19.KeyBlockForms@L71614`
- `def.19.KeyTriple@L71647`, `rule.19.K-Mode-Read@L71673`, `rule.19.K-Mode-Write@L71689`, `requirement.19.RestrictiveContextApplies@L71705`, `def.19.ReadContexts@L71718`, `def.19.WriteContexts@L71740`, `def.19.KeyStateContext@L71763`, `def.19.Covered@L71786`
- `requirement.19.ValidKeyContext@L71801`, `rule.19.K-Acquire-New@L71820`, `rule.19.K-Acquire-Covered@L71836`, `requirement.19.KeyAcquisitionEvaluationOrder@L71852`, `rule.19.K-Block-Acquire@L71867`, `rule.19.K-Read-Block-No-Write@L71883`, `requirement.19.KeyCoarseningInlineMarker@L71901`, `rule.19.K-Coarsen-Inline@L71914`
- `requirement.19.FieldKeyBoundary@L71930`, `requirement.19.ClosureDependencySetConsumption@L71945`, `def.19.SharedCaptures@L71958`, `def.19.LocalClosureKeyPath@L71971`, `rule.19.K-Closure-Escape-Keys@L71988`, `requirement.19.EscapingClosureSharedLifetime@L72004`, `requirement.19.EscapingClosureRuntimeIdentityCoverage@L72017`, `requirement.19.KeyBlockCanonicalOrderReferences@L72036`
- `def.19.KeyBlockRuntimeJudgments@L72049`, `def.19.AcquireKeysSigma@L72062`, `def.19.ReleaseKeysSigma@L72079`, `def.19.ModeOf@L72095`, `rule.19.ExecSigma-KeyBlock@L72112`, `rule.19.ExecSigma-KeyBlock-Ctrl@L72128`, `rule.19.Step-Exec-KeyBlock-Enter@L72144`, `rule.19.Step-Exec-KeyBlock-Body@L72160`
- `rule.19.Step-Exec-KeyBlock-Exit-Ok@L72176`, `rule.19.Step-Exec-KeyBlock-Exit-Ctrl@L72192`, `requirement.19.ScopeExitKeyRelease@L72208`, `requirement.19.LocalClosureInvocationSharedCaptures@L72223`, `requirement.19.EscapingClosureInvocationSharedCaptures@L72241`, `def.19.LowerKeyPathsEmpty@L72261`, `def.19.LowerKeyPathsCons@L72274`, `rule.19.Lower-Stmt-KeyBlock@L72287`
- `requirement.19.KeyScopeBound@L72308`, `requirement.19.KeyEscapeRestrictions@L72323`, `requirement.19.FineGrainedKeyLoopWarning@L72338`, `requirement.19.KeyEscapeDiagnosticPrecedence@L72351`, `diagnostics.19.KeyAcquisitionBlocks@L72364`, `requirement.19.ConflictDetectionNoAdditionalSyntax@L72394`, `requirement.19.ConflictDetectionNoAdditionalParsingRules@L72409`, `def.19.PrefixAndDisjoint@L72424`
- `def.19.KeyPathOrdering@L72439`, `def.19.KeyCompatibility@L72474`, `def.19.IndexEquivalence@L72505`, `requirement.19.IndexEquivalenceConservativeSubset@L72528`, `rule.19.K-Disjoint-Safe@L72541`, `rule.19.K-Prefix-Coverage@L72557`, `def.19.DynamicIndexDisjointness@L72575`, `requirement.19.DynamicIndexDisjointnessConservativeSubset@L72598`
- `rule.19.K-Dynamic-Index-Conflict@L72611`, `def.19.ReadThenWrite@L72629`, `requirement.19.ReadThenWriteDiagnosticSurface@L72646`, `requirement.19.ReadThenWriteOtherWriteForms@L72659`, `rule.19.K-Read-Write-Reject@L72672`, `rule.19.K-RMW-Permitted@L72688`, `rule.19.K-RMW-Explicit-Warn@L72704`, `rule.19.K-RMW-Contention-Warn@L72720`
- `def.19.OrderedComparablePaths@L72736`, `rule.19.K-Ordered-Ok@L72752`, `rule.19.K-Ordered-Base-Err@L72768`, `rule.19.K-Ordered-Redundant-Warn@L72784`, `requirement.19.CanonicalOrderDynamicUse@L72802`, `requirement.19.KeyConflictRuntimeCompatibility@L72815`, `def.19.LowerConflictChecks@L72830`, `rule.19.Lower-Key-ConflictChecks@L72847`
- `diagnostics.19.ConflictDetection@L72865`, `requirement.19.NestedReleaseNoAdditionalSyntax@L72890`, `requirement.19.NestedReleaseNoAdditionalParsingRules@L72905`, `ast.19.NestedReleaseForm@L72920`, `rule.19.K-Nested-Same-Path@L72935`, `def.19.SharedParam@L72956`, `def.19.DirectCalleeAccesses@L72970`, `def.19.CalleeAccessSummary@L72983`
- `def.19.CalleeAccessInstantiation@L72996`, `rule.19.K-Reentrant@L73011`, `requirement.19.UnknownCalleeAccessWarning@L73026`, `rule.19.CallSharedArgumentNoKeyAcquisition@L73041`, `requirement.19.StaleOkSuppressesReleaseWarning@L73056`, `rule.19.K-Release-SameMode-Err@L73069`, `requirement.19.NestedReleaseExecutionSequence@L73087`, `rule.19.K-Release-Sequence@L73106`
- `requirement.19.NestedReleaseInterleavingWindow@L73126`, `def.19.HeldKeyAccessors@L73139`, `def.19.ReleasedKeyState@L73155`, `rule.19.ExecSigma-KeyBlock-Release@L73175`, `rule.19.Lower-Stmt-KeyBlock-Release@L73193`, `diagnostics.19.NestedRelease@L73216`, `grammar.19.SpeculativeExecution@L73239`, `parse.19.SpeculativeBlocks@L73256`
- `ast.19.SpeculativeBlockForm@L73271`, `def.19.SpeculativeSetsAndStates@L73284`, `rule.19.K-Spec-Write-Required@L73312`, `rule.19.K-Spec-Pure-Body@L73328`, `requirement.19.SpeculativePermittedOperations@L73344`, `requirement.19.SpeculativeProhibitedOperations@L73362`, `def.19.IsCallLike@L73382`, `rule.19.K-Spec-No-Nested-Key@L73395`
- `rule.19.K-Spec-No-Impure-Call@L73411`, `rule.19.K-Spec-No-Memory-Ordering@L73427`, `rule.19.K-Spec-No-Wait@L73443`, `rule.19.K-Spec-No-Defer@L73459`, `rule.19.K-Spec-No-Release@L73475`, `rule.19.ExecSigma-KeyBlock-Speculative@L73495`, `def.19.SpecLoop@L73511`, `rule.19.Spec-Start@L73532`
- `rule.19.Spec-Snapshot@L73547`, `rule.19.Spec-Exec-Ok@L73563`, `rule.19.Spec-Exec-Panic@L73579`, `rule.19.Spec-Commit-Success@L73595`, `rule.19.Spec-Commit-Fail-Retry@L73611`, `rule.19.Spec-Commit-Fail-Fallback@L73627`, `rule.19.Spec-Retry@L73643`, `rule.19.Spec-Fallback@L73658`
- `rule.19.SpecBlock-Ok@L73674`, `rule.19.SpecBlock-Panic@L73690`, `def.19.SpeculativeRuntimeHelpers@L73706`, `requirement.19.SpeculativePanicDiscardsWrites@L73727`, `requirement.19.SpeculativeAtomicity@L73740`, `requirement.19.SpeculativeAbstractSemanticsAndFallback@L73753`, `def.19.SpeculativeIR@L73770`, `rule.19.Lower-Stmt-KeyBlock-Speculative@L73783`
- `diagnostics.19.SpeculativeExecution@L73803`, `requirement.19.DynamicKeyVerificationNoAdditionalSyntax@L73831`, `requirement.19.DynamicKeyVerificationNoAdditionalParsingRules@L73846`, `def.19.StaticallySafeConditions@L73861`, `requirement.19.StaticallySafeSoundProofRequired@L73883`, `rule.19.K-Static-Safe@L73902`, `requirement.19.NoRuntimeSyncMeaning@L73918`, `rule.19.K-Static-Required@L73933`
- `requirement.19.RuntimeSynchronizationRequirements@L73951`, `requirement.19.DynamicIndexRuntimeOrdering@L73969`, `requirement.19.DynamicIndexedPathCoarsening@L73988`, `requirement.19.CanonicalOrderDeadlockFreedom@L74003`, `requirement.19.StaticAndRuntimeKeySafetyEquivalence@L74018`, `rule.19.K-Dynamic-Permitted@L74033`, `requirement.19.DynamicContextStaticSafeLowering@L74049`, `diagnostics.19.DynamicKeyVerification@L74064`
- `grammar.19.MemoryOrdering@L74085`, `parse.19.MemoryOrdering@L74105`, `ast.19.MemoryOrderingForms@L74122`, `requirement.19.MemoryOrderingDefaultsAndKeySemantics@L74145`, `def.19.MemoryOrderingLevels@L74160`, `requirement.19.MemoryOrderAttributeAttachment@L74181`, `requirement.19.ExpressionMemoryOrderWellFormedness@L74199`, `requirement.19.MemoryOrderDoesNotAlterKeySemantics@L74212`
- `requirement.19.MemoryOrderNotInsideSpeculativeBlocks@L74225`, `rule.19.T-Fence@L74238`, `requirement.19.FenceContextAndHeldKeys@L74254`, `requirement.19.FenceEvaluation@L74269`, `requirement.19.FenceOrderingConstraints@L74286`, `requirement.19.FenceNoProgramVisibleStorageAccess@L74303`, `rule.19.Lower-Expr-Fence@L74318`, `rule.19.Lower-Ordered-Access@L74333`
- `diagnostics.19.MemoryOrdering@L74351`

#### `spec.structured-parallelism`

Count: 181 total; 180 required; 0 recommended; 0 informative. Ledger line span: L74083-L77316.

- `grammar.20.ParallelBlocks@L74370`, `parse.20.ParallelBlockRules@L74395`, `ast.20.ParallelBlockForms@L74425`, `def.20.ParallelBlockOptionValidation@L74461`, `rule.20.Dim3Const-Err@L74487`, `def.20.ParallelDomainCtorValidation@L74503`, `rule.20.T-Parallel@L74523`, `requirement.20.ParallelBlockWellFormedness@L74539`
- `rule.20.Parallel-Domain-Param-Err@L74556`, `requirement.20.ParallelCancelOptionType@L74572`, `def.20.ParallelState@L74587`, `def.20.ParallelGpuTopologyOptions@L74606`, `def.20.AwaitSpawned@L74638`, `rule.20.EvalSigma-Parallel@L74651`, `rule.20.EvalSigma-Parallel-Body-Ctrl@L74667`, `rule.20.EvalSigma-Parallel-Domain-Ctrl@L74683`
- `requirement.20.ParallelPanicPropagationReference@L74699`, `def.20.ParallelLoweringJudgments@L74714`, `rule.20.Lower-Expr-Parallel@L74727`, `diagnostics.20.ParallelBlocks@L74745`, `requirement.20.ExecutionDomainSyntax@L74766`, `grammar.20.ExecutionDomainExamples@L74779`, `requirement.20.ExecutionDomainsNoAdditionalParsingProductions@L74798`, `parse.20.GpuPtrGenericType@L74811`
- `def.20.GpuDomainJudgments@L74826`, `def.20.GpuMemoryForms@L74845`, `def.20.GpuPtrType@L74865`, `def.20.DispatchGpuTopologyComputation@L74879`, `def.20.GpuExecutionTopology@L74902`, `def.20.GpuIntrinsicTable@L74925`, `def.20.GpuRuntimeState@L74951`, `def.20.ExecutionDomainClass@L74977`
- `requirement.20.ExecutionDomainContextMethods@L74997`, `def.20.GpuSafeType@L75024`, `def.20.GpuSafePredicateClauses@L75053`, `rule.20.GpuSafe-Prim@L75068`, `rule.20.GpuSafe-RawPtr@L75084`, `rule.20.GpuSafe-Array@L75100`, `rule.20.GpuSafe-Tuple@L75116`, `rule.20.GpuSafe-Perm@L75132`
- `rule.20.GpuSafe-Record@L75148`, `rule.20.GpuSafe-Enum@L75164`, `rule.20.GpuSafe-StringView@L75180`, `rule.20.GpuSafe-BytesView@L75196`, `rule.20.GpuSafeType-Err@L75212`, `rule.20.GpuSafe-Record-Field-Err@L75228`, `rule.20.GpuSafe-Generic-Unbounded-Err@L75244`, `rule.20.T-GpuIntrinsic@L75260`
- `rule.20.Barrier-Outside-Err@L75276`, `rule.20.GpuIntrinsic-Outside-Err@L75292`, `rule.20.GpuPtr-AddrSpace-Err@L75308`, `requirement.20.ExecutionDomainDispatchableClass@L75324`, `requirement.20.GpuSafeGenericBounds@L75337`, `requirement.20.KeySystemUnavailableInGpuContext@L75350`, `requirement.20.InlineDomainSemantics@L75365`, `def.20.GpuMemoryVisibility@L75383`
- `rule.20.GpuPtr-Deref-Visible@L75399`, `rule.20.GpuPtr-Deref-Err@L75415`, `def.20.GpuTopologyValidity@L75431`, `rule.20.EvalSigma-GPU-Parallel@L75450`, `rule.20.EvalSigma-GPU-Dispatch@L75466`, `rule.20.GpuExecute-Step@L75482`, `rule.20.GpuBarrier-Sync@L75498`, `requirement.20.GpuBarrierWait@L75514`
- `rule.20.EvalSigma-GpuBarrier@L75529`, `rule.20.Barrier-Divergence-Err@L75545`, `rule.20.KeyBlock-GPU-Err@L75561`, `rule.20.WorkgroupSize-Err@L75577`, `rule.20.Lower-Domain-CPU@L75595`, `rule.20.Lower-Domain-GPU@L75610`, `rule.20.Lower-Domain-Inline@L75625`, `rule.20.Lower-Expr-Parallel-GPU@L75640`
- `rule.20.Lower-Expr-GpuBarrier@L75656`, `diagnostics.20.ExecutionDomains@L75673`, `requirement.20.CaptureSemanticsNoAdditionalSyntax@L75701`, `requirement.20.CaptureSemanticsNoAdditionalParsingRules@L75716`, `requirement.20.CaptureSetComputationReference@L75731`, `def.20.GpuCaptureJudgments@L75753`, `requirement.20.ParallelCapturePermissions@L75770`, `rule.20.Parallel-Closure-Capture-Const@L75787`
- `rule.20.Parallel-Closure-Capture-Shared@L75803`, `rule.20.Parallel-Closure-Capture-Unique-Err@L75819`, `def.20.OuterParallelMoveSelection@L75835`, `rule.20.Parallel-Closure-Capture-Unique-Move-Ok@L75849`, `rule.20.Parallel-Closure-Capture-OuterMove-Err@L75865`, `rule.20.Parallel-Escaping-Closure-Spawn-Err@L75881`, `requirement.20.ParallelClosuresLocalForKeys@L75897`, `rule.20.GpuCaptureOk-Const@L75910`
- `rule.20.GpuCaptureOk-Unique-Move@L75926`, `rule.20.GpuCapture-Shared-Err@L75942`, `rule.20.GpuCapture-HeapProv-Err@L75958`, `rule.20.GpuCapture-NonGpuSafe-Err@L75974`, `requirement.20.MovedBindingValidityReference@L75990`, `requirement.20.CaptureSemanticsNoAdditionalRuntimeMechanism@L76005`, `requirement.20.CaptureSemanticsGenericLowering@L76024`, `diagnostics.20.CaptureSemantics@L76039`
- `grammar.20.Spawn@L76063`, `parse.20.SpawnRules@L76084`, `ast.20.SpawnForms@L76111`, `def.20.SpawnOptionValidation@L76144`, `requirement.20.SpawnRequiresParallelContext@L76163`, `rule.20.T-Spawn@L76176`, `def.20.SpawnHandleAndEnqueue@L76194`, `requirement.20.SpawnEvaluationProcedure@L76211`
- `rule.20.EvalSigma-Spawn@L76231`, `requirement.20.SpawnedResultRetrievalReference@L76247`, `rule.20.Lower-Expr-Spawn@L76262`, `diagnostics.20.Spawn@L76280`, `grammar.20.Dispatch@L76299`, `parse.20.DispatchRules@L76324`, `ast.20.DispatchForms@L76358`, `requirement.20.DispatchRequiresParallelContext@L76401`
- `rule.20.T-Dispatch@L76414`, `rule.20.T-Dispatch-Reduce@L76430`, `rule.20.T-GPU-Dispatch@L76446`, `rule.20.T-GPU-Dispatch-Reduce@L76462`, `def.20.DispatchAccessInference@L76478`, `def.20.DispatchOptionsAndDynamicKeys@L76516`, `rule.20.Dispatch-Infer-Err@L76547`, `rule.20.Dispatch-Outside-Err@L76563`
- `rule.20.Dispatch-Chunk-Type-Err@L76579`, `rule.20.Dispatch-Dependency-Err@L76595`, `rule.20.Dispatch-Reduce-Assoc-Err@L76611`, `rule.20.Dispatch-DynamicKey-Warn@L76627`, `requirement.20.DispatchKeyInferenceRequired@L76643`, `rule.20.DispatchIndexedDisjointness@L76656`, `requirement.20.DispatchReductionAssociativity@L76671`, `requirement.20.DispatchChunkSemanticsStatic@L76684`
- `def.20.DispatchPartitionSpec@L76699`, `def.20.DispatchIndexAndPathDisjointness@L76714`, `def.20.DispatchPartitioning@L76757`, `def.20.DispatchReductionAndChunking@L76778`, `rule.20.EvalSigma-Dispatch@L76800`, `rule.20.EvalSigma-Dispatch-Range-Ctrl@L76816`, `rule.20.EvalSigma-Dispatch-Chunk-Ctrl@L76832`, `def.20.DispatchRun@L76848`
- `rule.20.Lower-Expr-Dispatch@L76869`, `diagnostics.20.Dispatch@L76887`, `requirement.20.CancellationSyntax@L76910`, `requirement.20.CancellationNoAdditionalParsingRules@L76925`, `ast.20.CancelTokenForms@L76940`, `requirement.20.CancelTokenStaticSemantics@L76967`, `requirement.20.CancelTokenParallelAvailability@L76992`, `def.20.CancelRuntimeHelpers@L77005`
- `rule.20.Cancel-New@L77028`, `rule.20.Cancel-Child@L77044`, `rule.20.Cancel-IsCancelled@L77060`, `rule.20.Cancel-DoCancel@L77076`, `rule.20.Cancel-WaitCancelled-Completed@L77092`, `rule.20.Cancel-WaitCancelled-Suspended@L77108`, `requirement.20.CooperativeCancellationBehavior@L77124`, `def.20.CancelIR@L77146`
- `rule.20.Lower-Cancel-New@L77159`, `rule.20.Lower-Cancel-Request@L77174`, `rule.20.Lower-Cancel-Wait@L77189`, `requirement.20.CancellationCheckpointLowering@L77204`, `requirement.20.SpawnDispatchCancellationLowering@L77217`, `diagnostics.20.Cancellation@L77232`, `requirement.20.PanicHandlingNoAdditionalSyntax@L77249`, `requirement.20.PanicHandlingNoAdditionalParsingRules@L77264`
- `ast.20.ParallelPanicPropagationInputs@L77279`, `requirement.20.PanicHandlingNoAdditionalStaticTypingRules@L77294`, `requirement.20.ParallelWorkItemPanicSemantics@L77309`, `rule.20.EvalSigma-Parallel-Spawn-Panic@L77326`, `requirement.20.ParallelPanicCancellationRequest@L77342`, `def.20.FirstCompletedFailure@L77355`, `rule.20.Lower-Parallel-Join-Panic@L77370`, `diagnostics.20.PanicHandling@L77387`
- `requirement.20.DeterminismNestingNoAdditionalSyntax@L77404`, `requirement.20.DeterminismNestingNoAdditionalParsingRules@L77419`, `ast.20.DeterminismNestingForms@L77434`, `requirement.20.DispatchDeterminismConditions@L77449`, `requirement.20.OrderedDispatchSequentialSideEffects@L77466`, `requirement.20.NoNestedGpuParallel@L77479`, `requirement.20.NestedParallelRuntimeSemantics@L77494`, `def.20.ParallelDeterministicOrdering@L77515`
- `rule.20.Lower-Deterministic-Dispatch@L77541`, `rule.20.Lower-Nested-Parallel@L77557`, `diagnostics.20.DeterminismAndNesting@L77573`, `requirement.20.StructuredParallelismRuntimePanicOwnership@L77590`, `diagnostics.20.StructuredParallelismSupplement@L77603`
- `grammar.20.ParallelBlocks@L74370`, `parse.20.ParallelBlockRules@L74395`, `ast.20.ParallelBlockForms@L74425`, `def.20.ParallelBlockOptionValidation@L74461`, `rule.20.Dim3Const-Err@L74487`, `def.20.ParallelDomainCtorValidation@L74503`, `rule.20.T-Parallel@L74523`, `requirement.20.ParallelBlockWellFormedness@L74539`
- `rule.20.Parallel-Domain-Param-Err@L74556`, `requirement.20.ParallelCancelOptionType@L74572`, `def.20.ParallelState@L74587`, `def.20.ParallelGpuTopologyOptions@L74606`, `def.20.AwaitSpawned@L74638`, `rule.20.EvalSigma-Parallel@L74651`, `rule.20.EvalSigma-Parallel-Body-Ctrl@L74667`, `rule.20.EvalSigma-Parallel-Domain-Ctrl@L74683`
- `requirement.20.ParallelPanicPropagationReference@L74699`, `def.20.ParallelLoweringJudgments@L74714`, `rule.20.Lower-Expr-Parallel@L74727`, `diagnostics.20.ParallelBlocks@L74745`, `requirement.20.ExecutionDomainSyntax@L74766`, `grammar.20.ExecutionDomainExamples@L74779`, `requirement.20.ExecutionDomainsNoAdditionalParsingProductions@L74798`, `parse.20.GpuPtrGenericType@L74811`
- `def.20.GpuDomainJudgments@L74826`, `def.20.GpuMemoryForms@L74845`, `def.20.GpuPtrType@L74865`, `def.20.DispatchGpuTopologyComputation@L74879`, `def.20.GpuExecutionTopology@L74902`, `def.20.GpuIntrinsicTable@L74925`, `def.20.GpuRuntimeState@L74951`, `def.20.ExecutionDomainClass@L74977`
- `requirement.20.ExecutionDomainContextMethods@L74997`, `def.20.GpuSafeType@L75024`, `def.20.GpuSafePredicateClauses@L75053`, `rule.20.GpuSafe-Prim@L75068`, `rule.20.GpuSafe-RawPtr@L75084`, `rule.20.GpuSafe-Array@L75100`, `rule.20.GpuSafe-Tuple@L75116`, `rule.20.GpuSafe-Perm@L75132`
- `rule.20.GpuSafe-Record@L75148`, `rule.20.GpuSafe-Enum@L75164`, `rule.20.GpuSafe-StringView@L75180`, `rule.20.GpuSafe-BytesView@L75196`, `rule.20.GpuSafeType-Err@L75212`, `rule.20.GpuSafe-Record-Field-Err@L75228`, `rule.20.GpuSafe-Generic-Unbounded-Err@L75244`, `rule.20.T-GpuIntrinsic@L75260`
- `rule.20.Barrier-Outside-Err@L75276`, `rule.20.GpuIntrinsic-Outside-Err@L75292`, `rule.20.GpuPtr-AddrSpace-Err@L75308`, `requirement.20.ExecutionDomainDispatchableClass@L75324`, `requirement.20.GpuSafeGenericBounds@L75337`, `requirement.20.KeySystemUnavailableInGpuContext@L75350`, `requirement.20.InlineDomainSemantics@L75365`, `def.20.GpuMemoryVisibility@L75383`
- `rule.20.GpuPtr-Deref-Visible@L75399`, `rule.20.GpuPtr-Deref-Err@L75415`, `def.20.GpuTopologyValidity@L75431`, `rule.20.EvalSigma-GPU-Parallel@L75450`, `rule.20.EvalSigma-GPU-Dispatch@L75466`, `rule.20.GpuExecute-Step@L75482`, `rule.20.GpuBarrier-Sync@L75498`, `requirement.20.GpuBarrierWait@L75514`
- `rule.20.EvalSigma-GpuBarrier@L75529`, `rule.20.Barrier-Divergence-Err@L75545`, `rule.20.KeyBlock-GPU-Err@L75561`, `rule.20.WorkgroupSize-Err@L75577`, `rule.20.Lower-Domain-CPU@L75595`, `rule.20.Lower-Domain-GPU@L75610`, `rule.20.Lower-Domain-Inline@L75625`, `rule.20.Lower-Expr-Parallel-GPU@L75640`
- `rule.20.Lower-Expr-GpuBarrier@L75656`, `diagnostics.20.ExecutionDomains@L75673`, `requirement.20.CaptureSemanticsNoAdditionalSyntax@L75701`, `requirement.20.CaptureSemanticsNoAdditionalParsingRules@L75716`, `requirement.20.CaptureSetComputationReference@L75731`, `def.20.GpuCaptureJudgments@L75753`, `requirement.20.ParallelCapturePermissions@L75770`, `rule.20.Parallel-Closure-Capture-Const@L75787`
- `rule.20.Parallel-Closure-Capture-Shared@L75803`, `rule.20.Parallel-Closure-Capture-Unique-Err@L75819`, `def.20.OuterParallelMoveSelection@L75835`, `rule.20.Parallel-Closure-Capture-Unique-Move-Ok@L75849`, `rule.20.Parallel-Closure-Capture-OuterMove-Err@L75865`, `rule.20.Parallel-Escaping-Closure-Spawn-Err@L75881`, `requirement.20.ParallelClosuresLocalForKeys@L75897`, `rule.20.GpuCaptureOk-Const@L75910`
- `rule.20.GpuCaptureOk-Unique-Move@L75926`, `rule.20.GpuCapture-Shared-Err@L75942`, `rule.20.GpuCapture-HeapProv-Err@L75958`, `rule.20.GpuCapture-NonGpuSafe-Err@L75974`, `requirement.20.MovedBindingValidityReference@L75990`, `requirement.20.CaptureSemanticsNoAdditionalRuntimeMechanism@L76005`, `requirement.20.CaptureSemanticsGenericLowering@L76024`, `diagnostics.20.CaptureSemantics@L76039`
- `grammar.20.Spawn@L76063`, `parse.20.SpawnRules@L76084`, `ast.20.SpawnForms@L76111`, `def.20.SpawnOptionValidation@L76144`, `requirement.20.SpawnRequiresParallelContext@L76163`, `rule.20.T-Spawn@L76176`, `def.20.SpawnHandleAndEnqueue@L76194`, `requirement.20.SpawnEvaluationProcedure@L76211`
- `rule.20.EvalSigma-Spawn@L76231`, `requirement.20.SpawnedResultRetrievalReference@L76247`, `rule.20.Lower-Expr-Spawn@L76262`, `diagnostics.20.Spawn@L76280`, `grammar.20.Dispatch@L76299`, `parse.20.DispatchRules@L76324`, `ast.20.DispatchForms@L76358`, `requirement.20.DispatchRequiresParallelContext@L76401`
- `rule.20.T-Dispatch@L76414`, `rule.20.T-Dispatch-Reduce@L76430`, `rule.20.T-GPU-Dispatch@L76446`, `rule.20.T-GPU-Dispatch-Reduce@L76462`, `def.20.DispatchAccessInference@L76478`, `def.20.DispatchOptionsAndDynamicKeys@L76516`, `rule.20.Dispatch-Infer-Err@L76547`, `rule.20.Dispatch-Outside-Err@L76563`
- `rule.20.Dispatch-Chunk-Type-Err@L76579`, `rule.20.Dispatch-Dependency-Err@L76595`, `rule.20.Dispatch-Reduce-Assoc-Err@L76611`, `rule.20.Dispatch-DynamicKey-Warn@L76627`, `requirement.20.DispatchKeyInferenceRequired@L76643`, `rule.20.DispatchIndexedDisjointness@L76656`, `requirement.20.DispatchReductionAssociativity@L76671`, `requirement.20.DispatchChunkSemanticsStatic@L76684`
- `def.20.DispatchPartitionSpec@L76699`, `def.20.DispatchIndexAndPathDisjointness@L76714`, `def.20.DispatchPartitioning@L76757`, `def.20.DispatchReductionAndChunking@L76778`, `rule.20.EvalSigma-Dispatch@L76800`, `rule.20.EvalSigma-Dispatch-Range-Ctrl@L76816`, `rule.20.EvalSigma-Dispatch-Chunk-Ctrl@L76832`, `def.20.DispatchRun@L76848`
- `rule.20.Lower-Expr-Dispatch@L76869`, `diagnostics.20.Dispatch@L76887`, `requirement.20.CancellationSyntax@L76910`, `requirement.20.CancellationNoAdditionalParsingRules@L76925`, `ast.20.CancelTokenForms@L76940`, `requirement.20.CancelTokenStaticSemantics@L76967`, `requirement.20.CancelTokenParallelAvailability@L76992`, `def.20.CancelRuntimeHelpers@L77005`
- `rule.20.Cancel-New@L77028`, `rule.20.Cancel-Child@L77044`, `rule.20.Cancel-IsCancelled@L77060`, `rule.20.Cancel-DoCancel@L77076`, `rule.20.Cancel-WaitCancelled-Completed@L77092`, `rule.20.Cancel-WaitCancelled-Suspended@L77108`, `requirement.20.CooperativeCancellationBehavior@L77124`, `def.20.CancelIR@L77146`
- `rule.20.Lower-Cancel-New@L77159`, `rule.20.Lower-Cancel-Request@L77174`, `rule.20.Lower-Cancel-Wait@L77189`, `requirement.20.CancellationCheckpointLowering@L77204`, `requirement.20.SpawnDispatchCancellationLowering@L77217`, `diagnostics.20.Cancellation@L77232`, `requirement.20.PanicHandlingNoAdditionalSyntax@L77249`, `requirement.20.PanicHandlingNoAdditionalParsingRules@L77264`
- `ast.20.ParallelPanicPropagationInputs@L77279`, `requirement.20.PanicHandlingNoAdditionalStaticTypingRules@L77294`, `requirement.20.ParallelWorkItemPanicSemantics@L77309`, `rule.20.EvalSigma-Parallel-Spawn-Panic@L77326`, `requirement.20.ParallelPanicCancellationRequest@L77342`, `def.20.FirstCompletedFailure@L77355`, `rule.20.Lower-Parallel-Join-Panic@L77370`, `diagnostics.20.PanicHandling@L77387`
- `requirement.20.DeterminismNestingNoAdditionalSyntax@L77404`, `requirement.20.DeterminismNestingNoAdditionalParsingRules@L77419`, `ast.20.DeterminismNestingForms@L77434`, `requirement.20.DispatchDeterminismConditions@L77449`, `requirement.20.OrderedDispatchSequentialSideEffects@L77466`, `requirement.20.NoNestedGpuParallel@L77479`, `requirement.20.NestedParallelRuntimeSemantics@L77494`, `def.20.ParallelDeterministicOrdering@L77515`
- `rule.20.Lower-Deterministic-Dispatch@L77541`, `rule.20.Lower-Nested-Parallel@L77557`, `diagnostics.20.DeterminismAndNesting@L77573`, `requirement.20.StructuredParallelismRuntimePanicOwnership@L77590`, `diagnostics.20.StructuredParallelismSupplement@L77603`

#### `spec.async`

Count: 254 total; 253 required; 0 recommended; 0 informative. Ledger line span: L77337-L82007.

- `requirement.21.AsyncTypeNoAdditionalConcreteGrammar@L77624`, `requirement.21.ReservedAsyncTypeConstructors@L77637`, `requirement.21.AsyncParameterDefaults@L77657`, `requirement.21.ReservedAsyncStates@L77670`, `parse.21.AsyncTypes@L77689`, `parse.21.UnappliedAsyncPath@L77704`, `ast.21.AsyncModalDeclaration@L77719`, `ast.21.AsyncAliases@L77790`
- `ast.21.AsyncCombinatorMembers@L77811`, `def.21.AsyncSigAndBodyReturnType@L77831`, `rule.21.Sub-Async@L77858`, `rule.21.WF-Async@L77879`, `rule.21.WF-Async-ArgCount-Err@L77897`, `rule.21.WF-Async-Arg-WF-Err@L77915`, `rule.21.WF-Async-Path-Err@L77933`, `requirement.21.AsyncFailedUninhabitedForNeverError@L77951`
- `requirement.21.AsyncTypeDynamicSemanticsReference@L77966`, `def.21.AsyncTypeLoweringForms@L77983`, `requirement.21.AsyncNeverErrorLowering@L78014`, `rule.21.Lower-Async-Type@L78027`, `rule.21.Lower-Async-Alias@L78047`, `diagnostics.21.AsyncType@L78067`, `grammar.21.SuspensionForms@L78086`, `parse.21.SuspensionFormsPrimaryExpressions@L78105`
- `rule.21.Parse-Wait-Expr@L78120`, `rule.21.Parse-Yield-From-Expr@L78140`, `rule.21.Parse-Yield-Expr@L78163`, `ast.21.SuspensionForms@L78186`, `ast.21.SuspensionFormResolution@L78206`, `ast.21.SuspensionFormEvaluationOrder@L78225`, `rule.21.T-Wait@L78248`, `rule.21.T-Wait-Future@L78266`
- `rule.21.Wait-Handle-Err@L78284`, `rule.21.T-Yield@L78304`, `rule.21.Yield-NotAsync-Err@L78322`, `rule.21.Yield-Out-Err@L78340`, `rule.21.T-Yield-From@L78360`, `rule.21.YieldFrom-NotAsync-Err@L78378`, `rule.21.YieldFrom-Out-Err@L78396`, `rule.21.YieldFrom-In-Err@L78415`
- `rule.21.YieldFrom-ErrType-Err@L78433`, `requirement.21.SuspensionKeyRestrictionsReference@L78451`, `requirement.21.WaitRuntimeSemantics@L78466`, `def.21.WaitRuntimeHelpers@L78488`, `rule.21.EvalSigma-Wait-Spawned-Ready@L78512`, `rule.21.EvalSigma-Wait-Spawned-Pending@L78530`, `requirement.21.FailedSpawnedWaitHandledByParallelPanic@L78549`, `rule.21.EvalSigma-Wait-Tracked-Ready@L78562`
- `rule.21.EvalSigma-Wait-Tracked-Pending@L78580`, `rule.21.EvalSigma-Wait-Ctrl@L78599`, `requirement.21.YieldRuntimeSemantics@L78617`, `def.21.ResumptionHelpers@L78637`, `rule.21.EvalSigma-Yield@L78672`, `rule.21.EvalSigma-Yield-Release@L78691`, `rule.21.EvalSigma-Yield-Resume@L78710`, `requirement.21.YieldFromRuntimeSemantics@L78729`
- `rule.21.EvalSigma-YieldFrom-Suspended@L78749`, `rule.21.EvalSigma-YieldFrom-Completed@L78769`, `rule.21.EvalSigma-YieldFrom-Failed@L78787`, `rule.21.EvalSigma-YieldFrom-Resume@L78805`, `def.21.EvalYieldFromContinueSignature@L78824`, `rule.21.EvalYieldFromContinue-Suspended@L78839`, `rule.21.EvalYieldFromContinue-Completed@L78859`, `rule.21.EvalYieldFromContinue-Failed@L78877`
- `def.21.SuspensionLoweringForms@L78897`, `rule.21.Lower-Wait-Spawned@L78918`, `rule.21.Lower-Wait-Tracked@L78936`, `rule.21.Lower-Yield@L78954`, `rule.21.Lower-Yield-Release@L78972`, `requirement.21.YieldReleaseReacquireLowering@L78990`, `rule.21.Lower-YieldFrom@L79003`, `requirement.21.YieldFromEnterLoweringLoop@L79021`
- `diagnostics.21.SuspensionForms@L79039`, `requirement.21.AsyncIterationSyntax@L79064`, `grammar.21.CompositionForms@L79079`, `requirement.21.AsyncMethodCallSurfaces@L79098`, `requirement.21.UntilMethodCallSurface@L79118`, `parse.21.CompositionPrimaryExpressions@L79133`, `rule.21.Parse-Sync-Expr@L79148`, `rule.21.Parse-Race-Expr@L79168`
- `rule.21.Parse-RaceArms-Cons@L79188`, `rule.21.Parse-RaceArm@L79206`, `rule.21.Parse-RaceArmsTail-End@L79226`, `rule.21.Parse-RaceArmsTail-TrailingComma@L79244`, `rule.21.Parse-RaceArmsTail-Comma@L79262`, `rule.21.Parse-RaceHandler-Yield@L79280`, `rule.21.Parse-RaceHandler-Return@L79298`, `rule.21.Parse-All-Expr@L79318`
- `parse.21.CompositionOrdinarySurfaces@L79336`, `ast.21.CompositionForms@L79351`, `ast.21.AsyncIterationLoopForm@L79373`, `ast.21.CompositionMethodCallForms@L79390`, `ast.21.CompositionResolution@L79405`, `ast.21.CompositionEvaluationOrder@L79434`, `rule.21.T-Loop-Iter-Async@L79460`, `rule.21.Loop-Async-Err@L79482`
- `requirement.21.ManualSteppingRequirement@L79500`, `def.21.SyncYieldContainment@L79515`, `rule.21.Sync-Yield-Err@L79531`, `rule.21.Sync-YieldFrom-Err@L79549`, `rule.21.T-Sync@L79567`, `rule.21.Sync-Async-Context-Err@L79585`, `rule.21.Sync-Out-Err@L79603`, `rule.21.Sync-In-Err@L79622`
- `def.21.RaceMode@L79642`, `rule.21.T-Race@L79660`, `rule.21.T-Race-Stream@L79681`, `rule.21.Race-Arity-Err@L79702`, `rule.21.Race-Handler-Mix-Err@L79720`, `rule.21.Race-Operand-Out-Err@L79738`, `rule.21.Race-Operand-Err@L79757`, `rule.21.Race-Stream-Operand-Err@L79776`
- `rule.21.Race-Handler-Type-Err@L79795`, `rule.21.Race-Stream-Handler-Type-Err@L79816`, `rule.21.T-All@L79839`, `rule.21.All-Out-Err@L79858`, `rule.21.All-In-Err@L79876`, `def.21.UntilType@L79894`, `def.21.AsyncCombinatorTypes@L79911`, `requirement.21.AsyncCombinatorMemberLookup@L79932`
- `rule.21.T-Async-Map@L79947`, `rule.21.T-Async-Filter@L79965`, `rule.21.T-Async-Take@L79983`, `rule.21.T-Async-Fold@L80001`, `rule.21.T-Async-Chain@L80019`, `requirement.21.AsyncIterationRuntimeSemantics@L80039`, `requirement.21.ManualSteppingRuntimeSemantics@L80057`, `requirement.21.SyncRuntimeSemantics@L80070`
- `def.21.SyncStepSignature@L80090`, `rule.21.SyncStep-Suspended@L80105`, `rule.21.EvalSigma-Sync-Suspended@L80123`, `rule.21.EvalSigma-Sync-Completed@L80142`, `rule.21.EvalSigma-Sync-Failed@L80160`, `requirement.21.RaceReturnRuntimeSemantics@L80178`, `requirement.21.RaceStreamingRuntimeSemantics@L80196`, `def.21.RaceSelectionAndState@L80217`
- `rule.21.InitRace@L80247`, `def.21.RaceStepReturnSignature@L80266`, `rule.21.RaceStepReturn-Completed@L80281`, `rule.21.RaceStepReturn-Failed@L80301`, `rule.21.RaceStepReturn-Continue@L80320`, `rule.21.EvalSigma-Race-Return@L80340`, `def.21.RaceStepStreamSignature@L80359`, `rule.21.RaceStepStream-Yield-Initial@L80374`
- `rule.21.RaceStepStream-AllComplete@L80394`, `rule.21.RaceStepStream-Failed@L80412`, `rule.21.EvalSigma-Race-Stream@L80431`, `def.21.CancelAllSignature@L80450`, `rule.21.CancelAll@L80465`, `def.21.RaceStreamSuspensionState@L80485`, `rule.21.RaceStepStream-Yield-Resumable@L80505`, `rule.21.ResumeRaceState-Step@L80525`
- `rule.21.ResumeRaceState-Done@L80544`, `rule.21.EvalSigma-Race-Stream-Resume@L80561`, `requirement.21.StreamingRaceResumptionOrder@L80581`, `requirement.21.AllRuntimeSemantics@L80594`, `def.21.AllStateAndInitSignature@L80614`, `rule.21.InitAll@L80630`, `def.21.AllStepSignature@L80649`, `rule.21.AllStep-Complete@L80664`
- `rule.21.AllStep-Failed@L80683`, `rule.21.AllStep-Resume@L80703`, `def.21.AllLoopSignature@L80723`, `rule.21.AllLoop-AllCompleted@L80738`, `rule.21.AllLoop-Failed@L80756`, `rule.21.AllLoop-Continue@L80774`, `rule.21.EvalSigma-All@L80793`, `requirement.21.UntilRuntimeSemantics@L80811`
- `def.21.AsyncCombinatorRuntimeWrappers@L80828`, `rule.21.EvalSigma-Map-Create@L80851`, `rule.21.EvalSigma-Map-Resume-Yield@L80869`, `rule.21.EvalSigma-Map-Resume-Complete@L80887`, `rule.21.EvalSigma-Map-Resume-Failed@L80905`, `rule.21.EvalSigma-Filter-Create@L80923`, `rule.21.EvalSigma-Filter-Resume-Pass@L80941`, `rule.21.EvalSigma-Filter-Resume-Skip@L80959`
- `rule.21.EvalSigma-Filter-Resume-Complete@L80978`, `rule.21.EvalSigma-Take-Create@L80996`, `rule.21.EvalSigma-Take-Resume-Yield@L81014`, `rule.21.EvalSigma-Take-Resume-Done@L81032`, `rule.21.EvalSigma-Take-Resume-Source-Complete@L81050`, `rule.21.EvalSigma-Fold-Create@L81068`, `rule.21.EvalSigma-Fold-Resume-Accumulate@L81086`, `rule.21.EvalSigma-Fold-Resume-Complete@L81104`
- `rule.21.EvalSigma-Fold-Resume-Failed@L81122`, `rule.21.EvalSigma-Chain-Create@L81140`, `rule.21.EvalSigma-Chain-Resume-Source-Complete@L81158`, `rule.21.EvalSigma-Chain-Resume-Chained@L81176`, `rule.21.EvalSigma-Chain-Resume-Source-Failed@L81194`, `def.21.AsyncComposeIR@L81214`, `rule.21.Lower-Expr-Sync@L81227`, `requirement.21.SyncLoopIRSemantics@L81245`
- `rule.21.Lower-Expr-Race-Return@L81258`, `rule.21.Lower-Expr-Race-Stream@L81276`, `requirement.21.RaceInitIRSemantics@L81294`, `requirement.21.RaceResumeIRSemantics@L81310`, `rule.21.Lower-Expr-All@L81323`, `requirement.21.AllJoinIRSemantics@L81339`, `requirement.21.AsyncCombinatorWrapperLowering@L81352`, `rule.21.Lower-Async-Map@L81365`
- `rule.21.Lower-Async-Filter@L81381`, `rule.21.Lower-Async-Take@L81397`, `rule.21.Lower-Async-Fold@L81413`, `rule.21.Lower-Async-Chain@L81429`, `requirement.21.AsyncWrapperLoweringSemantics@L81445`, `diagnostics.21.AsyncCompositionDiagnostics@L81460`, `requirement.21.AsyncStateMachineSyntaxSurface@L81490`, `def.21.AsyncProcedureDefinition@L81503`
- `requirement.21.AsyncStateMachineParsingSurface@L81518`, `def.21.AsyncStateMachineHelperForms@L81535`, `requirement.21.AsyncFrameStoredState@L81564`, `def.21.LiveAcrossSuspension@L81583`, `rule.21.Warn-Async-LargeCapture@L81598`, `rule.21.Warn-Async-LargeCapture-Ok@L81616`, `requirement.21.AsyncLargeCaptureWarningEmission@L81634`, `rule.21.Async-Capture-Err@L81647`
- `rule.21.P-Async-Create@L81665`, `rule.21.Prov-Async-Escape-Err@L81684`, `requirement.21.AsyncErrorPropagationTypingReference@L81703`, `requirement.21.AsyncProcedureCallRuntimeSemantics@L81718`, `requirement.21.AsyncSettlementRuntimeSemantics@L81736`, `requirement.21.AsyncResumeRuntimeSemantics@L81753`, `requirement.21.AsyncFailureRuntimeSemantics@L81769`, `def.21.AsyncStateMachineLoweringJudgements@L81789`
- `def.21.AsyncStateMachineFrameHelpers@L81803`, `rule.21.Lower-Async-Proc@L81819`, `requirement.21.AsyncFrameInitIRSemantics@L81839`, `rule.21.Lower-Async-Resume@L81858`, `requirement.21.AsyncResumeSwitchIRSemantics@L81877`, `rule.21.Lower-Async-Suspend@L81891`, `rule.21.Lower-Async-Complete@L81910`, `rule.21.Lower-Async-Fail@L81929`
- `requirement.21.AsyncFailStateIRSemantics@L81948`, `diagnostics.21.AsyncStateMachineDiagnostics@L81964`, `requirement.21.AsyncKeySyntaxSurface@L81985`, `requirement.21.AsyncKeyParsingSurface@L82000`, `def.21.AsyncKeyExistingAstForms@L82017`, `requirement.21.AsyncKeyNoAdditionalAstVariants@L82033`, `requirement.21.AsyncKeyRestrictions@L82050`, `rule.21.A-Closure-Yield-Keys-Err@L82067`
- `requirement.21.SharedCapturingClosureYieldKeys@L82086`, `requirement.21.YieldReleaseStalenessWarning@L82099`, `requirements.21.AsyncCapabilityRequirements@L82114`, `requirement.21.AsyncSuspensionAccessRights@L82134`, `requirement.21.YieldReleaseRuntimeReference@L82147`, `requirement.21.AsyncKeyFailureHandlingReference@L82160`, `def.21.AsyncKeyIR@L82175`, `rule.21.Lower-Wait-Key-Illegal@L82188`
- `rule.21.Lower-Yield-Release-Keys@L82206`, `rule.21.Lower-YieldFrom-Release-Keys@L82224`, `rule.21.Lower-Closure-Yield-Shared@L82242`, `requirement.21.StaleValueMarkIRDiagnostics@L82260`, `diagnostics.21.AsyncKeyDiagnostics@L82275`, `diagnostics.21.AsyncDiagnosticsSupplement@L82294`
- `requirement.21.AsyncTypeNoAdditionalConcreteGrammar@L77624`, `requirement.21.ReservedAsyncTypeConstructors@L77637`, `requirement.21.AsyncParameterDefaults@L77657`, `requirement.21.ReservedAsyncStates@L77670`, `parse.21.AsyncTypes@L77689`, `parse.21.UnappliedAsyncPath@L77704`, `ast.21.AsyncModalDeclaration@L77719`, `ast.21.AsyncAliases@L77790`
- `ast.21.AsyncCombinatorMembers@L77811`, `def.21.AsyncSigAndBodyReturnType@L77831`, `rule.21.Sub-Async@L77858`, `rule.21.WF-Async@L77879`, `rule.21.WF-Async-ArgCount-Err@L77897`, `rule.21.WF-Async-Arg-WF-Err@L77915`, `rule.21.WF-Async-Path-Err@L77933`, `requirement.21.AsyncFailedUninhabitedForNeverError@L77951`
- `requirement.21.AsyncTypeDynamicSemanticsReference@L77966`, `def.21.AsyncTypeLoweringForms@L77983`, `requirement.21.AsyncNeverErrorLowering@L78014`, `rule.21.Lower-Async-Type@L78027`, `rule.21.Lower-Async-Alias@L78047`, `diagnostics.21.AsyncType@L78067`, `grammar.21.SuspensionForms@L78086`, `parse.21.SuspensionFormsPrimaryExpressions@L78105`
- `rule.21.Parse-Wait-Expr@L78120`, `rule.21.Parse-Yield-From-Expr@L78140`, `rule.21.Parse-Yield-Expr@L78163`, `ast.21.SuspensionForms@L78186`, `ast.21.SuspensionFormResolution@L78206`, `ast.21.SuspensionFormEvaluationOrder@L78225`, `rule.21.T-Wait@L78248`, `rule.21.T-Wait-Future@L78266`
- `rule.21.Wait-Handle-Err@L78284`, `rule.21.T-Yield@L78304`, `rule.21.Yield-NotAsync-Err@L78322`, `rule.21.Yield-Out-Err@L78340`, `rule.21.T-Yield-From@L78360`, `rule.21.YieldFrom-NotAsync-Err@L78378`, `rule.21.YieldFrom-Out-Err@L78396`, `rule.21.YieldFrom-In-Err@L78415`
- `rule.21.YieldFrom-ErrType-Err@L78433`, `requirement.21.SuspensionKeyRestrictionsReference@L78451`, `requirement.21.WaitRuntimeSemantics@L78466`, `def.21.WaitRuntimeHelpers@L78488`, `rule.21.EvalSigma-Wait-Spawned-Ready@L78512`, `rule.21.EvalSigma-Wait-Spawned-Pending@L78530`, `requirement.21.FailedSpawnedWaitHandledByParallelPanic@L78549`, `rule.21.EvalSigma-Wait-Tracked-Ready@L78562`
- `rule.21.EvalSigma-Wait-Tracked-Pending@L78580`, `rule.21.EvalSigma-Wait-Ctrl@L78599`, `requirement.21.YieldRuntimeSemantics@L78617`, `def.21.ResumptionHelpers@L78637`, `rule.21.EvalSigma-Yield@L78672`, `rule.21.EvalSigma-Yield-Release@L78691`, `rule.21.EvalSigma-Yield-Resume@L78710`, `requirement.21.YieldFromRuntimeSemantics@L78729`
- `rule.21.EvalSigma-YieldFrom-Suspended@L78749`, `rule.21.EvalSigma-YieldFrom-Completed@L78769`, `rule.21.EvalSigma-YieldFrom-Failed@L78787`, `rule.21.EvalSigma-YieldFrom-Resume@L78805`, `def.21.EvalYieldFromContinueSignature@L78824`, `rule.21.EvalYieldFromContinue-Suspended@L78839`, `rule.21.EvalYieldFromContinue-Completed@L78859`, `rule.21.EvalYieldFromContinue-Failed@L78877`
- `def.21.SuspensionLoweringForms@L78897`, `rule.21.Lower-Wait-Spawned@L78918`, `rule.21.Lower-Wait-Tracked@L78936`, `rule.21.Lower-Yield@L78954`, `rule.21.Lower-Yield-Release@L78972`, `requirement.21.YieldReleaseReacquireLowering@L78990`, `rule.21.Lower-YieldFrom@L79003`, `requirement.21.YieldFromEnterLoweringLoop@L79021`
- `diagnostics.21.SuspensionForms@L79039`, `requirement.21.AsyncIterationSyntax@L79064`, `grammar.21.CompositionForms@L79079`, `requirement.21.AsyncMethodCallSurfaces@L79098`, `requirement.21.UntilMethodCallSurface@L79118`, `parse.21.CompositionPrimaryExpressions@L79133`, `rule.21.Parse-Sync-Expr@L79148`, `rule.21.Parse-Race-Expr@L79168`
- `rule.21.Parse-RaceArms-Cons@L79188`, `rule.21.Parse-RaceArm@L79206`, `rule.21.Parse-RaceArmsTail-End@L79226`, `rule.21.Parse-RaceArmsTail-TrailingComma@L79244`, `rule.21.Parse-RaceArmsTail-Comma@L79262`, `rule.21.Parse-RaceHandler-Yield@L79280`, `rule.21.Parse-RaceHandler-Return@L79298`, `rule.21.Parse-All-Expr@L79318`
- `parse.21.CompositionOrdinarySurfaces@L79336`, `ast.21.CompositionForms@L79351`, `ast.21.AsyncIterationLoopForm@L79373`, `ast.21.CompositionMethodCallForms@L79390`, `ast.21.CompositionResolution@L79405`, `ast.21.CompositionEvaluationOrder@L79434`, `rule.21.T-Loop-Iter-Async@L79460`, `rule.21.Loop-Async-Err@L79482`
- `requirement.21.ManualSteppingRequirement@L79500`, `def.21.SyncYieldContainment@L79515`, `rule.21.Sync-Yield-Err@L79531`, `rule.21.Sync-YieldFrom-Err@L79549`, `rule.21.T-Sync@L79567`, `rule.21.Sync-Async-Context-Err@L79585`, `rule.21.Sync-Out-Err@L79603`, `rule.21.Sync-In-Err@L79622`
- `def.21.RaceMode@L79642`, `rule.21.T-Race@L79660`, `rule.21.T-Race-Stream@L79681`, `rule.21.Race-Arity-Err@L79702`, `rule.21.Race-Handler-Mix-Err@L79720`, `rule.21.Race-Operand-Out-Err@L79738`, `rule.21.Race-Operand-Err@L79757`, `rule.21.Race-Stream-Operand-Err@L79776`
- `rule.21.Race-Handler-Type-Err@L79795`, `rule.21.Race-Stream-Handler-Type-Err@L79816`, `rule.21.T-All@L79839`, `rule.21.All-Out-Err@L79858`, `rule.21.All-In-Err@L79876`, `def.21.UntilType@L79894`, `def.21.AsyncCombinatorTypes@L79911`, `requirement.21.AsyncCombinatorMemberLookup@L79932`
- `rule.21.T-Async-Map@L79947`, `rule.21.T-Async-Filter@L79965`, `rule.21.T-Async-Take@L79983`, `rule.21.T-Async-Fold@L80001`, `rule.21.T-Async-Chain@L80019`, `requirement.21.AsyncIterationRuntimeSemantics@L80039`, `requirement.21.ManualSteppingRuntimeSemantics@L80057`, `requirement.21.SyncRuntimeSemantics@L80070`
- `def.21.SyncStepSignature@L80090`, `rule.21.SyncStep-Suspended@L80105`, `rule.21.EvalSigma-Sync-Suspended@L80123`, `rule.21.EvalSigma-Sync-Completed@L80142`, `rule.21.EvalSigma-Sync-Failed@L80160`, `requirement.21.RaceReturnRuntimeSemantics@L80178`, `requirement.21.RaceStreamingRuntimeSemantics@L80196`, `def.21.RaceSelectionAndState@L80217`
- `rule.21.InitRace@L80247`, `def.21.RaceStepReturnSignature@L80266`, `rule.21.RaceStepReturn-Completed@L80281`, `rule.21.RaceStepReturn-Failed@L80301`, `rule.21.RaceStepReturn-Continue@L80320`, `rule.21.EvalSigma-Race-Return@L80340`, `def.21.RaceStepStreamSignature@L80359`, `rule.21.RaceStepStream-Yield-Initial@L80374`
- `rule.21.RaceStepStream-AllComplete@L80394`, `rule.21.RaceStepStream-Failed@L80412`, `rule.21.EvalSigma-Race-Stream@L80431`, `def.21.CancelAllSignature@L80450`, `rule.21.CancelAll@L80465`, `def.21.RaceStreamSuspensionState@L80485`, `rule.21.RaceStepStream-Yield-Resumable@L80505`, `rule.21.ResumeRaceState-Step@L80525`
- `rule.21.ResumeRaceState-Done@L80544`, `rule.21.EvalSigma-Race-Stream-Resume@L80561`, `requirement.21.StreamingRaceResumptionOrder@L80581`, `requirement.21.AllRuntimeSemantics@L80594`, `def.21.AllStateAndInitSignature@L80614`, `rule.21.InitAll@L80630`, `def.21.AllStepSignature@L80649`, `rule.21.AllStep-Complete@L80664`
- `rule.21.AllStep-Failed@L80683`, `rule.21.AllStep-Resume@L80703`, `def.21.AllLoopSignature@L80723`, `rule.21.AllLoop-AllCompleted@L80738`, `rule.21.AllLoop-Failed@L80756`, `rule.21.AllLoop-Continue@L80774`, `rule.21.EvalSigma-All@L80793`, `requirement.21.UntilRuntimeSemantics@L80811`
- `def.21.AsyncCombinatorRuntimeWrappers@L80828`, `rule.21.EvalSigma-Map-Create@L80851`, `rule.21.EvalSigma-Map-Resume-Yield@L80869`, `rule.21.EvalSigma-Map-Resume-Complete@L80887`, `rule.21.EvalSigma-Map-Resume-Failed@L80905`, `rule.21.EvalSigma-Filter-Create@L80923`, `rule.21.EvalSigma-Filter-Resume-Pass@L80941`, `rule.21.EvalSigma-Filter-Resume-Skip@L80959`
- `rule.21.EvalSigma-Filter-Resume-Complete@L80978`, `rule.21.EvalSigma-Take-Create@L80996`, `rule.21.EvalSigma-Take-Resume-Yield@L81014`, `rule.21.EvalSigma-Take-Resume-Done@L81032`, `rule.21.EvalSigma-Take-Resume-Source-Complete@L81050`, `rule.21.EvalSigma-Fold-Create@L81068`, `rule.21.EvalSigma-Fold-Resume-Accumulate@L81086`, `rule.21.EvalSigma-Fold-Resume-Complete@L81104`
- `rule.21.EvalSigma-Fold-Resume-Failed@L81122`, `rule.21.EvalSigma-Chain-Create@L81140`, `rule.21.EvalSigma-Chain-Resume-Source-Complete@L81158`, `rule.21.EvalSigma-Chain-Resume-Chained@L81176`, `rule.21.EvalSigma-Chain-Resume-Source-Failed@L81194`, `def.21.AsyncComposeIR@L81214`, `rule.21.Lower-Expr-Sync@L81227`, `requirement.21.SyncLoopIRSemantics@L81245`
- `rule.21.Lower-Expr-Race-Return@L81258`, `rule.21.Lower-Expr-Race-Stream@L81276`, `requirement.21.RaceInitIRSemantics@L81294`, `requirement.21.RaceResumeIRSemantics@L81310`, `rule.21.Lower-Expr-All@L81323`, `requirement.21.AllJoinIRSemantics@L81339`, `requirement.21.AsyncCombinatorWrapperLowering@L81352`, `rule.21.Lower-Async-Map@L81365`
- `rule.21.Lower-Async-Filter@L81381`, `rule.21.Lower-Async-Take@L81397`, `rule.21.Lower-Async-Fold@L81413`, `rule.21.Lower-Async-Chain@L81429`, `requirement.21.AsyncWrapperLoweringSemantics@L81445`, `diagnostics.21.AsyncCompositionDiagnostics@L81460`, `requirement.21.AsyncStateMachineSyntaxSurface@L81490`, `def.21.AsyncProcedureDefinition@L81503`
- `requirement.21.AsyncStateMachineParsingSurface@L81518`, `def.21.AsyncStateMachineHelperForms@L81535`, `requirement.21.AsyncFrameStoredState@L81564`, `def.21.LiveAcrossSuspension@L81583`, `rule.21.Warn-Async-LargeCapture@L81598`, `rule.21.Warn-Async-LargeCapture-Ok@L81616`, `requirement.21.AsyncLargeCaptureWarningEmission@L81634`, `rule.21.Async-Capture-Err@L81647`
- `rule.21.P-Async-Create@L81665`, `rule.21.Prov-Async-Escape-Err@L81684`, `requirement.21.AsyncErrorPropagationTypingReference@L81703`, `requirement.21.AsyncProcedureCallRuntimeSemantics@L81718`, `requirement.21.AsyncSettlementRuntimeSemantics@L81736`, `requirement.21.AsyncResumeRuntimeSemantics@L81753`, `requirement.21.AsyncFailureRuntimeSemantics@L81769`, `def.21.AsyncStateMachineLoweringJudgements@L81789`
- `def.21.AsyncStateMachineFrameHelpers@L81803`, `rule.21.Lower-Async-Proc@L81819`, `requirement.21.AsyncFrameInitIRSemantics@L81839`, `rule.21.Lower-Async-Resume@L81858`, `requirement.21.AsyncResumeSwitchIRSemantics@L81877`, `rule.21.Lower-Async-Suspend@L81891`, `rule.21.Lower-Async-Complete@L81910`, `rule.21.Lower-Async-Fail@L81929`
- `requirement.21.AsyncFailStateIRSemantics@L81948`, `diagnostics.21.AsyncStateMachineDiagnostics@L81964`, `requirement.21.AsyncKeySyntaxSurface@L81985`, `requirement.21.AsyncKeyParsingSurface@L82000`, `def.21.AsyncKeyExistingAstForms@L82017`, `requirement.21.AsyncKeyNoAdditionalAstVariants@L82033`, `requirement.21.AsyncKeyRestrictions@L82050`, `rule.21.A-Closure-Yield-Keys-Err@L82067`
- `requirement.21.SharedCapturingClosureYieldKeys@L82086`, `requirement.21.YieldReleaseStalenessWarning@L82099`, `requirements.21.AsyncCapabilityRequirements@L82114`, `requirement.21.AsyncSuspensionAccessRights@L82134`, `requirement.21.YieldReleaseRuntimeReference@L82147`, `requirement.21.AsyncKeyFailureHandlingReference@L82160`, `def.21.AsyncKeyIR@L82175`, `rule.21.Lower-Wait-Key-Illegal@L82188`
- `rule.21.Lower-Yield-Release-Keys@L82206`, `rule.21.Lower-YieldFrom-Release-Keys@L82224`, `rule.21.Lower-Closure-Yield-Shared@L82242`, `requirement.21.StaleValueMarkIRDiagnostics@L82260`, `diagnostics.21.AsyncKeyDiagnostics@L82275`, `diagnostics.21.AsyncDiagnosticsSupplement@L82294`

#### `spec.comptime`

Count: 181 total; 181 required; 0 recommended; 0 informative. Ledger line span: L82027-L85037.

- `requirement.22.Phase2ExecutionPosition@L82314`, `grammar.22.CompileTimeForms@L82331`, `def.22.CtParseJudg@L82353`, `rule.22.Parse-CtProc@L82366`, `rule.22.Parse-CtStmt@L82382`, `rule.22.Parse-CtExpr@L82398`, `rule.22.Parse-CtIf@L82414`, `rule.22.Parse-CtLoopIter@L82430`
- `rule.22.Parse-CtElseOpt-None@L82446`, `rule.22.Parse-CtElseOpt-Block@L82462`, `rule.22.Parse-CtElseOpt-ElseIf@L82478`, `def.22.CtNodeForms@L82496`, `def.22.CtExecutionState@L82515`, `def.22.CompileTimeJudgementSets@L82543`, `def.22.CtValueForms@L82562`, `def.22.CompileTimeTypingEnvironment@L82583`
- `def.22.CtAvailabilityAndForbiddenTypes@L82596`, `requirement.22.CompileTimeTypeAvailabilityRejection@L82629`, `requirement.22.CompileTimeProhibitedConstructs@L82642`, `rule.22.T-CtStmt@L82660`, `rule.22.T-CtExpr@L82676`, `rule.22.T-CtIf@L82692`, `rule.22.T-CtLoopIter@L82708`, `rule.22.T-CtProc@L82724`
- `requirement.22.CompileTimeProcedureContracts@L82740`, `requirement.22.CompileTimeProcedureContextRestriction@L82753`, `requirement.22.ComptimeIfSelectedBranchOnly@L82766`, `requirement.22.ComptimeLoopIterationSemantics@L82779`, `def.22.Phase2ModuleOrder@L82795`, `def.22.CtDynamicHelpers@L82808`, `requirement.22.ComptimePassExecutionRequirements@L82830`, `requirement.22.CtEvalOrdinarySemantics@L82849`
- `requirement.22.CtExpandOrdinaryTraversal@L82862`, `rule.22.ComptimePass-Empty@L82875`, `rule.22.ComptimePass-Cons@L82890`, `rule.22.ComptimePass@L82906`, `rule.22.CtExecModule@L82924`, `rule.22.CtExpandItemSeq-Empty@L82940`, `rule.22.CtExpandItemSeq-Cons@L82955`, `def.22.CtExpandItemResult@L82971`
- `requirement.22.CtPendingEmitsTransfer@L82984`, `rule.22.CtExpandItem-CtProc@L82997`, `rule.22.CtExpandStmtSeq-Empty@L83013`, `rule.22.CtExpandStmtSeq-Cons@L83028`, `rule.22.CtExpandBlock@L83044`, `rule.22.CtExpandStmt-CtStmt@L83060`, `rule.22.CtExpandExpr-CtExpr@L83076`, `rule.22.CtExpandExpr-CtIf-True@L83092`
- `rule.22.CtExpandExpr-CtIf-False@L83108`, `rule.22.CtExpandExpr-CtLoopIter@L83124`, `rule.22.CtLoopIterUnroll-Empty@L83140`, `rule.22.CtLoopIterUnroll-Cons@L83155`, `def.22.CtLiteralize@L83171`, `requirement.22.CompileTimeFormsLowering@L83195`, `diagnostics.22.CompileTimeFormsDiagnosticsReference@L83215`, `requirement.22.CompileTimeCapabilitiesSyntaxSurface@L83232`
- `def.22.CtCapName@L83247`, `rule.22.Parse-CtCapRef@L83260`, `requirement.22.CtCapMethodCallParsing@L83276`, `def.22.CtCapabilitiesAndBuiltinTypes@L83291`, `def.22.CtReflectionInfoFields@L83314`, `def.22.CtValueConversionHelpers@L83330`, `def.22.TypeEmitterInterface@L83356`, `def.22.IntrospectInterface@L83372`
- `def.22.ProjectFilesInterface@L83394`, `def.22.ComptimeDiagnosticsInterface@L83414`, `requirement.22.IntrospectAndDiagnosticsAvailability@L83436`, `requirement.22.TypeEmitterAvailability@L83449`, `requirement.22.ProjectFilesAvailability@L83464`, `def.22.CtCapBindings@L83477`, `requirement.22.ProjectFilesPathRestrictions@L83490`, `requirement.22.TypeEmitterEmitTypeRequirement@L83507`
- `def.22.CtCapabilityDynamicHelpers@L83522`, `rule.22.CtBuiltin-Emit@L83545`, `rule.22.CtBuiltin-ProjectRoot@L83561`, `rule.22.CtBuiltin-Read@L83577`, `rule.22.CtBuiltin-Read-InvalidPath@L83593`, `rule.22.CtBuiltin-ReadBytes@L83609`, `rule.22.CtBuiltin-ReadBytes-InvalidPath@L83625`, `rule.22.CtBuiltin-Exists@L83641`
- `rule.22.CtBuiltin-Exists-InvalidPath@L83657`, `rule.22.CtBuiltin-ListDir@L83673`, `rule.22.CtBuiltin-ListDir-InvalidPath@L83689`, `rule.22.CtBuiltin-Diagnostics-Error@L83705`, `rule.22.CtBuiltin-Diagnostics-Warning@L83721`, `rule.22.CtBuiltin-Diagnostics-Note@L83737`, `rule.22.CtBuiltin-Diagnostics-CurrentSpan@L83753`, `rule.22.CtBuiltin-Diagnostics-CurrentModule@L83769`
- `requirement.22.ProjectFileSnapshotStability@L83785`, `requirement.22.CompileTimeCapabilitiesLowering@L83800`, `diagnostics.22.CompileTimeCapabilitiesDiagnosticsReference@L83815`, `grammar.22.TypeLiteral@L83832`, `def.22.ReflectParseJudg@L83849`, `rule.22.Parse-TypeLiteral@L83862`, `def.22.Reflectable@L83880`, `def.22.ReflectJudgementsAndTypeLiteralExpr@L83905`
- `def.22.TypeCategory@L83919`, `def.22.ReflectFields@L83960`, `def.22.ReflectVariants@L83977`, `def.22.ReflectStates@L83994`, `def.22.ReflectionPayloadAndModuleHelpers@L84012`, `rule.22.T-TypeLiteral@L84037`, `requirement.22.IntrospectCategoryValidity@L84053`, `requirement.22.IntrospectMemberValidity@L84066`
- `requirement.22.ReflectionCanonicalOrder@L84081`, `requirement.22.IntrospectImplementsFormSemantics@L84097`, `rule.22.CtEval-TypeLiteral@L84112`, `rule.22.CtBuiltin-Reflect-Category@L84128`, `rule.22.CtBuiltin-Reflect-Fields@L84144`, `rule.22.CtBuiltin-Reflect-Variants@L84160`, `rule.22.CtBuiltin-Reflect-States@L84176`, `rule.22.CtBuiltin-Reflect-Form@L84192`
- `rule.22.CtBuiltin-Reflect-TypeName@L84208`, `rule.22.CtBuiltin-Reflect-ModulePath@L84224`, `requirement.22.ReflectionPurityAndImmutability@L84240`, `requirement.22.ReflectionLowering@L84255`, `diagnostics.22.ReflectionDiagnosticsReference@L84270`, `grammar.22.QuoteSpliceEmission@L84287`, `def.22.QuoteParseJudg@L84309`, `def.22.CaptureQuotedTokens@L84322`
- `rule.22.Parse-Quote-Raw@L84335`, `rule.22.Parse-Quote-Type@L84351`, `rule.22.Parse-Quote-Pattern@L84367`, `def.22.AstForms@L84385`, `def.22.QuoteSpliceHygieneForms@L84404`, `def.22.QuoteJudg@L84421`, `def.22.ExpectedAstKind@L84434`, `def.22.CtLiteralType@L84452`
- `def.22.SpliceCompat@L84473`, `requirement.22.QuoteCompileTimeOnly@L84493`, `def.22.ResolveQuoteKind@L84506`, `requirement.22.QuotedContentValidity@L84521`, `requirement.22.SpliceContextAndTypeCompatibility@L84534`, `requirement.22.SpliceIdentifierPositionRestrictions@L84547`, `requirement.22.StringSpliceIdentifierHygiene@L84560`, `requirement.22.EmitterEmitWellFormedness@L84573`
- `def.22.ParseQuotedBody@L84588`, `def.22.RenderSplice@L84605`, `requirement.22.HygienizeAstProperties@L84623`, `requirement.22.HygienicInternalReferences@L84639`, `requirement.22.ImportUsingHygiene@L84652`, `rule.22.CtEval-Quote@L84665`, `requirement.22.QuoteBuildSpliceOrder@L84681`, `requirement.22.EmissionOrder@L84695`
- `requirement.22.QuoteSpliceEmissionLowering@L84712`, `diagnostics.22.QuoteSpliceEmissionDiagnosticsReference@L84727`, `grammar.22.DeriveTargetsAndContracts@L84744`, `def.22.DeriveParseJudg@L84765`, `requirement.22.DeriveAttributeParsingReference@L84778`, `rule.22.Parse-DeriveTargetDecl@L84791`, `rule.22.Parse-DeriveContractOpt-None@L84807`, `rule.22.Parse-DeriveContractOpt-Yes@L84823`
- `rule.22.Parse-DeriveClauseList-Cons@L84839`, `rule.22.Parse-DeriveClause-Requires@L84855`, `rule.22.Parse-DeriveClause-Emits@L84871`, `rule.22.Parse-DeriveClauseTail-End@L84887`, `rule.22.Parse-DeriveClauseTail-Comma@L84903`, `def.22.DeriveTargetDecl@L84921`, `def.22.DeriveGraphAndOrder@L84936`, `requirement.22.DeriveAttributeTargetKinds@L84960`
- `requirement.22.DeriveTargetNameResolution@L84973`, `requirement.22.DeriveTargetBodyBindings@L84986`, `requirement.22.DeriveTargetBodyRestrictions@L85003`, `requirement.22.DeriveExecutionOrder@L85016`, `requirement.22.DeriveOrderTieBreaker@L85031`, `requirement.22.DeriveRequiresValidation@L85044`, `requirement.22.DeriveEmitsValidation@L85057`, `requirement.22.DeriveRequiresEmitsScope@L85070`
- `requirement.22.DeriveTargetDeclPhase2Lifetime@L85085`, `rule.22.CtExpandItem-DeriveTargetDecl@L85098`, `rule.22.RunDeriveSet-Empty@L85114`, `rule.22.RunDeriveSet-Cons@L85129`, `rule.22.RunDeriveTarget@L85145`, `def.22.BindDeriveTargetInputs@L85161`, `rule.22.CtExpandItem-DeriveAnnotatedDecl@L85174`, `requirement.22.DeriveTargetExecutionTiming@L85190`
- `requirement.22.DeriveTargetFailureSemantics@L85203`, `requirement.22.DeriveTargetsLowering@L85218`, `diagnostics.22.DeriveTargetsDiagnosticsReference@L85233`, `diagnostics.22.CompileTimeDiagnosticsSupplement@L85248`, `requirement.22.UserDiagnosticBuiltinEmission@L85324`
- `requirement.22.Phase2ExecutionPosition@L82314`, `grammar.22.CompileTimeForms@L82331`, `def.22.CtParseJudg@L82353`, `rule.22.Parse-CtProc@L82366`, `rule.22.Parse-CtStmt@L82382`, `rule.22.Parse-CtExpr@L82398`, `rule.22.Parse-CtIf@L82414`, `rule.22.Parse-CtLoopIter@L82430`
- `rule.22.Parse-CtElseOpt-None@L82446`, `rule.22.Parse-CtElseOpt-Block@L82462`, `rule.22.Parse-CtElseOpt-ElseIf@L82478`, `def.22.CtNodeForms@L82496`, `def.22.CtExecutionState@L82515`, `def.22.CompileTimeJudgementSets@L82543`, `def.22.CtValueForms@L82562`, `def.22.CompileTimeTypingEnvironment@L82583`
- `def.22.CtAvailabilityAndForbiddenTypes@L82596`, `requirement.22.CompileTimeTypeAvailabilityRejection@L82629`, `requirement.22.CompileTimeProhibitedConstructs@L82642`, `rule.22.T-CtStmt@L82660`, `rule.22.T-CtExpr@L82676`, `rule.22.T-CtIf@L82692`, `rule.22.T-CtLoopIter@L82708`, `rule.22.T-CtProc@L82724`
- `requirement.22.CompileTimeProcedureContracts@L82740`, `requirement.22.CompileTimeProcedureContextRestriction@L82753`, `requirement.22.ComptimeIfSelectedBranchOnly@L82766`, `requirement.22.ComptimeLoopIterationSemantics@L82779`, `def.22.Phase2ModuleOrder@L82795`, `def.22.CtDynamicHelpers@L82808`, `requirement.22.ComptimePassExecutionRequirements@L82830`, `requirement.22.CtEvalOrdinarySemantics@L82849`
- `requirement.22.CtExpandOrdinaryTraversal@L82862`, `rule.22.ComptimePass-Empty@L82875`, `rule.22.ComptimePass-Cons@L82890`, `rule.22.ComptimePass@L82906`, `rule.22.CtExecModule@L82924`, `rule.22.CtExpandItemSeq-Empty@L82940`, `rule.22.CtExpandItemSeq-Cons@L82955`, `def.22.CtExpandItemResult@L82971`
- `requirement.22.CtPendingEmitsTransfer@L82984`, `rule.22.CtExpandItem-CtProc@L82997`, `rule.22.CtExpandStmtSeq-Empty@L83013`, `rule.22.CtExpandStmtSeq-Cons@L83028`, `rule.22.CtExpandBlock@L83044`, `rule.22.CtExpandStmt-CtStmt@L83060`, `rule.22.CtExpandExpr-CtExpr@L83076`, `rule.22.CtExpandExpr-CtIf-True@L83092`
- `rule.22.CtExpandExpr-CtIf-False@L83108`, `rule.22.CtExpandExpr-CtLoopIter@L83124`, `rule.22.CtLoopIterUnroll-Empty@L83140`, `rule.22.CtLoopIterUnroll-Cons@L83155`, `def.22.CtLiteralize@L83171`, `requirement.22.CompileTimeFormsLowering@L83195`, `diagnostics.22.CompileTimeFormsDiagnosticsReference@L83215`, `requirement.22.CompileTimeCapabilitiesSyntaxSurface@L83232`
- `def.22.CtCapName@L83247`, `rule.22.Parse-CtCapRef@L83260`, `requirement.22.CtCapMethodCallParsing@L83276`, `def.22.CtCapabilitiesAndBuiltinTypes@L83291`, `def.22.CtReflectionInfoFields@L83314`, `def.22.CtValueConversionHelpers@L83330`, `def.22.TypeEmitterInterface@L83356`, `def.22.IntrospectInterface@L83372`
- `def.22.ProjectFilesInterface@L83394`, `def.22.ComptimeDiagnosticsInterface@L83414`, `requirement.22.IntrospectAndDiagnosticsAvailability@L83436`, `requirement.22.TypeEmitterAvailability@L83449`, `requirement.22.ProjectFilesAvailability@L83464`, `def.22.CtCapBindings@L83477`, `requirement.22.ProjectFilesPathRestrictions@L83490`, `requirement.22.TypeEmitterEmitTypeRequirement@L83507`
- `def.22.CtCapabilityDynamicHelpers@L83522`, `rule.22.CtBuiltin-Emit@L83545`, `rule.22.CtBuiltin-ProjectRoot@L83561`, `rule.22.CtBuiltin-Read@L83577`, `rule.22.CtBuiltin-Read-InvalidPath@L83593`, `rule.22.CtBuiltin-ReadBytes@L83609`, `rule.22.CtBuiltin-ReadBytes-InvalidPath@L83625`, `rule.22.CtBuiltin-Exists@L83641`
- `rule.22.CtBuiltin-Exists-InvalidPath@L83657`, `rule.22.CtBuiltin-ListDir@L83673`, `rule.22.CtBuiltin-ListDir-InvalidPath@L83689`, `rule.22.CtBuiltin-Diagnostics-Error@L83705`, `rule.22.CtBuiltin-Diagnostics-Warning@L83721`, `rule.22.CtBuiltin-Diagnostics-Note@L83737`, `rule.22.CtBuiltin-Diagnostics-CurrentSpan@L83753`, `rule.22.CtBuiltin-Diagnostics-CurrentModule@L83769`
- `requirement.22.ProjectFileSnapshotStability@L83785`, `requirement.22.CompileTimeCapabilitiesLowering@L83800`, `diagnostics.22.CompileTimeCapabilitiesDiagnosticsReference@L83815`, `grammar.22.TypeLiteral@L83832`, `def.22.ReflectParseJudg@L83849`, `rule.22.Parse-TypeLiteral@L83862`, `def.22.Reflectable@L83880`, `def.22.ReflectJudgementsAndTypeLiteralExpr@L83905`
- `def.22.TypeCategory@L83919`, `def.22.ReflectFields@L83960`, `def.22.ReflectVariants@L83977`, `def.22.ReflectStates@L83994`, `def.22.ReflectionPayloadAndModuleHelpers@L84012`, `rule.22.T-TypeLiteral@L84037`, `requirement.22.IntrospectCategoryValidity@L84053`, `requirement.22.IntrospectMemberValidity@L84066`
- `requirement.22.ReflectionCanonicalOrder@L84081`, `requirement.22.IntrospectImplementsFormSemantics@L84097`, `rule.22.CtEval-TypeLiteral@L84112`, `rule.22.CtBuiltin-Reflect-Category@L84128`, `rule.22.CtBuiltin-Reflect-Fields@L84144`, `rule.22.CtBuiltin-Reflect-Variants@L84160`, `rule.22.CtBuiltin-Reflect-States@L84176`, `rule.22.CtBuiltin-Reflect-Form@L84192`
- `rule.22.CtBuiltin-Reflect-TypeName@L84208`, `rule.22.CtBuiltin-Reflect-ModulePath@L84224`, `requirement.22.ReflectionPurityAndImmutability@L84240`, `requirement.22.ReflectionLowering@L84255`, `diagnostics.22.ReflectionDiagnosticsReference@L84270`, `grammar.22.QuoteSpliceEmission@L84287`, `def.22.QuoteParseJudg@L84309`, `def.22.CaptureQuotedTokens@L84322`
- `rule.22.Parse-Quote-Raw@L84335`, `rule.22.Parse-Quote-Type@L84351`, `rule.22.Parse-Quote-Pattern@L84367`, `def.22.AstForms@L84385`, `def.22.QuoteSpliceHygieneForms@L84404`, `def.22.QuoteJudg@L84421`, `def.22.ExpectedAstKind@L84434`, `def.22.CtLiteralType@L84452`
- `def.22.SpliceCompat@L84473`, `requirement.22.QuoteCompileTimeOnly@L84493`, `def.22.ResolveQuoteKind@L84506`, `requirement.22.QuotedContentValidity@L84521`, `requirement.22.SpliceContextAndTypeCompatibility@L84534`, `requirement.22.SpliceIdentifierPositionRestrictions@L84547`, `requirement.22.StringSpliceIdentifierHygiene@L84560`, `requirement.22.EmitterEmitWellFormedness@L84573`
- `def.22.ParseQuotedBody@L84588`, `def.22.RenderSplice@L84605`, `requirement.22.HygienizeAstProperties@L84623`, `requirement.22.HygienicInternalReferences@L84639`, `requirement.22.ImportUsingHygiene@L84652`, `rule.22.CtEval-Quote@L84665`, `requirement.22.QuoteBuildSpliceOrder@L84681`, `requirement.22.EmissionOrder@L84695`
- `requirement.22.QuoteSpliceEmissionLowering@L84712`, `diagnostics.22.QuoteSpliceEmissionDiagnosticsReference@L84727`, `grammar.22.DeriveTargetsAndContracts@L84744`, `def.22.DeriveParseJudg@L84765`, `requirement.22.DeriveAttributeParsingReference@L84778`, `rule.22.Parse-DeriveTargetDecl@L84791`, `rule.22.Parse-DeriveContractOpt-None@L84807`, `rule.22.Parse-DeriveContractOpt-Yes@L84823`
- `rule.22.Parse-DeriveClauseList-Cons@L84839`, `rule.22.Parse-DeriveClause-Requires@L84855`, `rule.22.Parse-DeriveClause-Emits@L84871`, `rule.22.Parse-DeriveClauseTail-End@L84887`, `rule.22.Parse-DeriveClauseTail-Comma@L84903`, `def.22.DeriveTargetDecl@L84921`, `def.22.DeriveGraphAndOrder@L84936`, `requirement.22.DeriveAttributeTargetKinds@L84960`
- `requirement.22.DeriveTargetNameResolution@L84973`, `requirement.22.DeriveTargetBodyBindings@L84986`, `requirement.22.DeriveTargetBodyRestrictions@L85003`, `requirement.22.DeriveExecutionOrder@L85016`, `requirement.22.DeriveOrderTieBreaker@L85031`, `requirement.22.DeriveRequiresValidation@L85044`, `requirement.22.DeriveEmitsValidation@L85057`, `requirement.22.DeriveRequiresEmitsScope@L85070`
- `requirement.22.DeriveTargetDeclPhase2Lifetime@L85085`, `rule.22.CtExpandItem-DeriveTargetDecl@L85098`, `rule.22.RunDeriveSet-Empty@L85114`, `rule.22.RunDeriveSet-Cons@L85129`, `rule.22.RunDeriveTarget@L85145`, `def.22.BindDeriveTargetInputs@L85161`, `rule.22.CtExpandItem-DeriveAnnotatedDecl@L85174`, `requirement.22.DeriveTargetExecutionTiming@L85190`
- `requirement.22.DeriveTargetFailureSemantics@L85203`, `requirement.22.DeriveTargetsLowering@L85218`, `diagnostics.22.DeriveTargetsDiagnosticsReference@L85233`, `diagnostics.22.CompileTimeDiagnosticsSupplement@L85248`, `requirement.22.UserDiagnosticBuiltinEmission@L85324`

#### `spec.ffi`

Count: 203 total; 203 required; 0 recommended; 0 informative. Ledger line span: L85054-L88478.

- `requirement.23.FFIBoundaryDefinition@L85341`, `def.23.FFIBoundary@L85354`, `requirement.23.FfiSafeSyntaxNoAdditionalForm@L85371`, `requirement.23.FfiSafeParsingNoAdditionalRules@L85386`, `def.23.FfiSafeTypePredicateAstForm@L85401`, `def.23.FfiSafePredicateMeaning@L85416`, `def.23.FfiSafeJudgements@L85429`, `def.23.FfiPrimitiveTypes@L85442`
- `def.23.FfiLayoutAndPayloadHelpers@L85455`, `def.23.FfiTypeParameterSetHelper@L85471`, `def.23.FfiAliasHelpers@L85484`, `def.23.TypeSubst@L85498`, `def.23.TypeParamsIn@L85535`, `def.23.FfiFieldAndPayloadTypeParamHelpers@L85573`, `def.23.FfiSafePredicateClauseHelpers@L85587`, `def.23.ProhibitedFfiType@L85601`
- `def.23.FfiByValueHelpers@L85632`, `rule.23.FfiSafe-Prim@L85647`, `rule.23.FfiSafe-RawPtr@L85663`, `rule.23.FfiSafe-Array@L85679`, `rule.23.FfiSafe-Func@L85695`, `rule.23.FfiSafe-Perm@L85711`, `rule.23.FfiSafe-Alias@L85727`, `rule.23.FfiSafe-Alias-Apply@L85743`
- `rule.23.FfiSafe-Record@L85759`, `rule.23.FfiSafe-Record-Apply@L85775`, `rule.23.FfiSafe-Enum@L85791`, `rule.23.FfiSafe-Enum-Apply@L85807`, `rule.23.FfiSafe-Prohibited-Err@L85823`, `rule.23.FfiSafe-Record-LayoutC-Err@L85839`, `rule.23.FfiSafe-Enum-LayoutC-Err@L85855`, `rule.23.FfiSafe-Record-Field-Err@L85871`
- `rule.23.FfiSafe-Record-Field-Apply-Err@L85887`, `rule.23.FfiSafe-Enum-Field-Err@L85903`, `rule.23.FfiSafe-Enum-Field-Apply-Err@L85919`, `rule.23.FfiSafe-Incomplete-Err@L85935`, `rule.23.FfiSafe-Record-Generic-Unbounded-Err@L85951`, `rule.23.FfiSafe-Enum-Generic-Unbounded-Err@L85967`, `rule.23.FfiSafe-Record-Apply-Generic-Unbounded-Err@L85983`, `rule.23.FfiSafe-Enum-Apply-Generic-Unbounded-Err@L85999`
- `requirement.23.FfiSafeProhibitedCategories@L86015`, `requirement.23.FfiSafeRaiiByValueRule@L86042`, `requirement.23.FfiSafeGenericBounds@L86055`, `requirement.23.FfiSafeDynamicSemantics@L86070`, `requirement.23.FfiSafeLowering@L86085`, `diagnostics.23.FfiSafeDiagnostics@L86100`, `grammar.23.ExternProcedureDecl@L86126`, `rule.23.Parse-ExternProcDecl@L86143`
- `ast.23.ExternProcDeclForm@L86161`, `def.23.ExternProcedureDerivedForms@L86178`, `def.23.ExternProcedureMeaning@L86198`, `def.23.ExternAbiStrings@L86211`, `def.23.ExternSignatureRequirements@L86233`, `requirement.23.ExternFfiConstraints@L86256`, `requirement.23.ExternCallSafety@L86273`, `requirement.23.ExternDynamicSemantics@L86290`
- `requirement.23.ExternLowering@L86305`, `diagnostics.23.ExternProcedureDiagnostics@L86320`, `diagnostics.23.ExternProcedureDiagnosticOwnership@L86336`, `requirement.23.RawExportedProcedureClassification@L86353`, `requirement.23.RawExportParsingUsesOrdinaryProcedureParser@L86368`, `requirement.23.RawExportParsingClassification@L86381`, `ast.23.RawExportProcedureForm@L86396`, `def.23.RawExportedProcedureMeaning@L86413`
- `def.23.ZeroValueHelpers@L86426`, `def.23.ExportSignatureHelpers@L86443`, `rule.23.ExportSig-Ok@L86457`, `requirement.23.RawExportOrdinaryBodyAndCatchReturn@L86475`, `requirement.23.RawExportLibraryImageLifecycle@L86488`, `requirement.23.SharedLibraryLinkedCallLifecycle@L86501`, `requirement.23.RawExportLowering@L86516`, `diagnostics.23.RawExportDiagnostics@L86531`
- `diagnostics.23.RawExportDiagnosticOwnership@L86547`, `requirement.23.HostedExportClassification@L86562`, `requirement.23.HostedExportParsingUsesOrdinaryProcedureParser@L86577`, `requirement.23.HostedExportParsingClassification@L86590`, `ast.23.HostedExportProcedureForm@L86605`, `def.23.HostedExportProcedureHelpers@L86618`, `requirement.23.HostedRootCapsMeaning@L86645`, `def.23.HostedExportMeaning@L86660`
- `requirement.23.HostedExportForeignVisibleSignature@L86673`, `requirement.23.HostedExportForeignVisiblePassKind@L86686`, `def.23.HostExportSignatureJudgements@L86699`, `rule.23.HostExportSig-Ok@L86712`, `rule.23.HostExport-Library-Err@L86728`, `rule.23.HostExport-MixedMode-Err@L86744`, `rule.23.HostExport-Generic-Err@L86760`, `rule.23.HostExport-Context-Err@L86776`
- `rule.23.HostExport-Context-Raw-Err@L86792`, `rule.23.HostExport-Context-Move-Err@L86808`, `requirement.23.HostedExportSessionHandleValidity@L86826`, `requirement.23.HostedExportCapabilityIsolation@L86839`, `requirement.23.HostedSessionRootCapsGrant@L86852`, `requirement.23.HostedExportBoundaryEntrySequence@L86865`, `requirement.23.HostedExportInvalidHandleBehavior@L86884`, `requirement.23.HostedExportCatchFailureReturn@L86900`
- `requirement.23.HostedExportLoweringPreservesRawFfiRules@L86915`, `requirement.23.HostedExportThunkAbiDetermination@L86928`, `def.23.HostThunkCarrierHelpers@L86946`, `rule.23.HostThunkParamCarrier-ByRef@L86974`, `rule.23.HostThunkParamCarrier-ByValue-Default@L86990`, `rule.23.HostThunkParamCarrier-Win64-DirectAgg@L87006`, `rule.23.HostThunkParamCarrier-Win64-IndirectAgg@L87022`, `rule.23.HostThunkRetCarrier-Default@L87038`
- `rule.23.HostThunkRetCarrier-Win64-DirectAgg@L87054`, `rule.23.HostThunkRetCarrier-Win64-SRetAgg@L87070`, `requirement.23.HostedExportThunkShapeUse@L87086`, `requirement.23.HostedExportNoWin64AggregateSplitting@L87099`, `requirement.23.HostedExportNoExtraAbiRewriting@L87112`, `requirement.23.HostedThunkModeIndependentForeignClassification@L87125`, `requirement.23.HostedThunkToSourceCallReconstruction@L87138`, `requirement.23.HostedStateSymbolResolution@L87151`
- `requirement.23.HostedLibraryLifecycleExports@L87164`, `requirement.23.HostedLifecycleExportsBackendGenerated@L87181`, `requirement.23.HostedLifecycleExportsPanicAndDestroyFailure@L87194`, `requirement.23.HostedSessionHandleNoReissue@L87207`, `requirement.23.HostedExportThunkForeignVisibleAbi@L87220`, `requirement.23.HostedExportThunkEmissionAndEntrypoint@L87239`, `diagnostics.23.HostedExportDiagnostics@L87254`, `diagnostics.23.HostedExportDiagnosticOwnership@L87273`
- `grammar.23.FfiAttributes@L87290`, `requirement.23.FfiAttributesParsing@L87319`, `ast.23.FfiAttributesAttachedEntries@L87334`, `ast.23.FfiAttributeTargets@L87347`, `requirement.23.MangleAttributeSemantics@L87371`, `def.23.LibraryLinkKinds@L87390`, `requirement.23.LibraryAttributeSemantics@L87410`, `def.23.ResolveLibraryName@L87427`
- `requirement.23.UnsupportedLibraryKindIllFormed@L87454`, `requirement.23.RawDylibResolution@L87467`, `def.23.UnwindModes@L87485`, `requirement.23.UnwindDefaultMode@L87503`, `requirement.23.UnwindAttributeTargetValidity@L87516`, `requirement.23.UnwindCatchAbiRequirement@L87529`, `requirement.23.ExportAttributeSemantics@L87549`, `requirement.23.HostExportAttributeSemantics@L87569`
- `requirement.23.FfiPassByValueAttributeSemantics@L87591`, `requirement.23.FfiAttributeConstraints@L87604`, `requirement.23.FfiAttributesDynamicSemantics@L87630`, `requirement.23.FfiAttributesLowering@L87645`, `diagnostics.23.FfiAttributeDiagnostics@L87660`, `requirement.23.CapabilityIsolationSyntaxNoAdditionalForm@L87693`, `requirement.23.CapabilityIsolationParsingNoAdditionalRules@L87708`, `ast.23.CapabilityIsolationNoDedicatedAst@L87723`
- `requirement.23.CapabilityIsolationSemantics@L87738`, `def.23.CapabilityIsolationHelpers@L87754`, `rule.23.FFI-Arg-RegionLocalRawPtr-Err@L87771`, `rule.23.FFI-Return-RegionLocalRawPtr-Err@L87789`, `requirement.23.CapabilityIsolationDynamicSemantics@L87809`, `requirement.23.CapabilityIsolationLowering@L87824`, `diagnostics.23.CapabilityIsolationDiagnostics@L87839`, `diagnostics.23.CapabilityIsolationDiagnosticOwnership@L87854`
- `grammar.23.ForeignContracts@L87871`, `def.23.ForeignContractStart@L87896`, `rule.23.Parse-ForeignContractClauseListOpt-None@L87909`, `rule.23.Parse-ForeignContractClauseListOpt-Yes@L87925`, `rule.23.Parse-ForeignContractClauseList-Cons@L87941`, `rule.23.Parse-ForeignContractClauseListTail-End@L87957`, `rule.23.Parse-ForeignContractClauseListTail-Cons@L87973`, `rule.23.Parse-ForeignContractClause-Assumes@L87989`
- `rule.23.Parse-ForeignContractClause-Ensures@L88005`, `def.23.ForeignEnsuresKindAndExpr@L88021`, `rule.23.Parse-EnsuresPredicate-Error@L88040`, `rule.23.Parse-EnsuresPredicate-NullResult@L88056`, `rule.23.Parse-EnsuresPredicate-Plain@L88072`, `ast.23.ForeignContractsForm@L88089`, `ast.23.EnsuresPredicateForms@L88112`, `def.23.ForeignPreconditions@L88134`
- `requirement.23.ForeignPredicateContext@L88147`, `def.23.ForeignPreconditionVerificationModes@L88173`, `requirement.23.ForeignPreconditionVerificationLowering@L88191`, `def.23.ForeignPostconditions@L88206`, `requirement.23.ForeignPostconditionPredicateBindings@L88219`, `def.23.ForeignPostconditionClassification@L88239`, `requirement.23.NullResultWellFormedness@L88272`, `def.23.NullableFfiResult@L88288`
- `rule.23.ForeignEnsures-NullResult-Err@L88304`, `requirement.23.ErrorPredicateWellFormedness@L88323`, `def.23.ForeignPostconditionVerificationModes@L88336`, `requirement.23.ForeignPostconditionStaticVerification@L88354`, `def.23.ForeignContractVerificationSummary@L88369`, `requirement.23.ForeignPreconditionDynamicFailure@L88389`, `requirement.23.ForeignPostconditionDynamicChecks@L88402`, `requirement.23.ForeignContractsLowering@L88417`
- `diagnostics.23.ForeignContractDiagnostics@L88432`, `requirement.23.BoundaryUnwindingSyntax@L88459`, `requirement.23.BoundaryUnwindingParsingNoAdditionalRules@L88474`, `ast.23.BoundaryUnwindPolicySource@L88489`, `def.23.UnwindModeAstHelpers@L88502`, `def.23.DetermineUnwindMode@L88524`, `def.23.ParseUnwindArg@L88549`, `rule.23.UnwindMode-Valid@L88571`
- `rule.23.UnwindMode-Invalid-Err@L88589`, `requirement.23.BoundaryUnwindDynamicEffects@L88609`, `requirement.23.GeneralDestructionAndUnwindCleanupReference@L88628`, `def.23.BoundaryUnwindCodeGenerationEffects@L88643`, `rule.23.CodeGen-UnwindAbort-Import@L88663`, `rule.23.CodeGen-UnwindCatch-Import@L88681`, `rule.23.CodeGen-UnwindAbort-Export@L88699`, `rule.23.CodeGen-UnwindCatch-Export@L88717`
- `diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics@L88737`, `diagnostics.23.BoundaryUnwindingDiagnosticOwnership@L88750`, `diagnostics.23.FfiDiagnosticsSupplement@L88765`
- `requirement.23.FFIBoundaryDefinition@L85341`, `def.23.FFIBoundary@L85354`, `requirement.23.FfiSafeSyntaxNoAdditionalForm@L85371`, `requirement.23.FfiSafeParsingNoAdditionalRules@L85386`, `def.23.FfiSafeTypePredicateAstForm@L85401`, `def.23.FfiSafePredicateMeaning@L85416`, `def.23.FfiSafeJudgements@L85429`, `def.23.FfiPrimitiveTypes@L85442`
- `def.23.FfiLayoutAndPayloadHelpers@L85455`, `def.23.FfiTypeParameterSetHelper@L85471`, `def.23.FfiAliasHelpers@L85484`, `def.23.TypeSubst@L85498`, `def.23.TypeParamsIn@L85535`, `def.23.FfiFieldAndPayloadTypeParamHelpers@L85573`, `def.23.FfiSafePredicateClauseHelpers@L85587`, `def.23.ProhibitedFfiType@L85601`
- `def.23.FfiByValueHelpers@L85632`, `rule.23.FfiSafe-Prim@L85647`, `rule.23.FfiSafe-RawPtr@L85663`, `rule.23.FfiSafe-Array@L85679`, `rule.23.FfiSafe-Func@L85695`, `rule.23.FfiSafe-Perm@L85711`, `rule.23.FfiSafe-Alias@L85727`, `rule.23.FfiSafe-Alias-Apply@L85743`
- `rule.23.FfiSafe-Record@L85759`, `rule.23.FfiSafe-Record-Apply@L85775`, `rule.23.FfiSafe-Enum@L85791`, `rule.23.FfiSafe-Enum-Apply@L85807`, `rule.23.FfiSafe-Prohibited-Err@L85823`, `rule.23.FfiSafe-Record-LayoutC-Err@L85839`, `rule.23.FfiSafe-Enum-LayoutC-Err@L85855`, `rule.23.FfiSafe-Record-Field-Err@L85871`
- `rule.23.FfiSafe-Record-Field-Apply-Err@L85887`, `rule.23.FfiSafe-Enum-Field-Err@L85903`, `rule.23.FfiSafe-Enum-Field-Apply-Err@L85919`, `rule.23.FfiSafe-Incomplete-Err@L85935`, `rule.23.FfiSafe-Record-Generic-Unbounded-Err@L85951`, `rule.23.FfiSafe-Enum-Generic-Unbounded-Err@L85967`, `rule.23.FfiSafe-Record-Apply-Generic-Unbounded-Err@L85983`, `rule.23.FfiSafe-Enum-Apply-Generic-Unbounded-Err@L85999`
- `requirement.23.FfiSafeProhibitedCategories@L86015`, `requirement.23.FfiSafeRaiiByValueRule@L86042`, `requirement.23.FfiSafeGenericBounds@L86055`, `requirement.23.FfiSafeDynamicSemantics@L86070`, `requirement.23.FfiSafeLowering@L86085`, `diagnostics.23.FfiSafeDiagnostics@L86100`, `grammar.23.ExternProcedureDecl@L86126`, `rule.23.Parse-ExternProcDecl@L86143`
- `ast.23.ExternProcDeclForm@L86161`, `def.23.ExternProcedureDerivedForms@L86178`, `def.23.ExternProcedureMeaning@L86198`, `def.23.ExternAbiStrings@L86211`, `def.23.ExternSignatureRequirements@L86233`, `requirement.23.ExternFfiConstraints@L86256`, `requirement.23.ExternCallSafety@L86273`, `requirement.23.ExternDynamicSemantics@L86290`
- `requirement.23.ExternLowering@L86305`, `diagnostics.23.ExternProcedureDiagnostics@L86320`, `diagnostics.23.ExternProcedureDiagnosticOwnership@L86336`, `requirement.23.RawExportedProcedureClassification@L86353`, `requirement.23.RawExportParsingUsesOrdinaryProcedureParser@L86368`, `requirement.23.RawExportParsingClassification@L86381`, `ast.23.RawExportProcedureForm@L86396`, `def.23.RawExportedProcedureMeaning@L86413`
- `def.23.ZeroValueHelpers@L86426`, `def.23.ExportSignatureHelpers@L86443`, `rule.23.ExportSig-Ok@L86457`, `requirement.23.RawExportOrdinaryBodyAndCatchReturn@L86475`, `requirement.23.RawExportLibraryImageLifecycle@L86488`, `requirement.23.SharedLibraryLinkedCallLifecycle@L86501`, `requirement.23.RawExportLowering@L86516`, `diagnostics.23.RawExportDiagnostics@L86531`
- `diagnostics.23.RawExportDiagnosticOwnership@L86547`, `requirement.23.HostedExportClassification@L86562`, `requirement.23.HostedExportParsingUsesOrdinaryProcedureParser@L86577`, `requirement.23.HostedExportParsingClassification@L86590`, `ast.23.HostedExportProcedureForm@L86605`, `def.23.HostedExportProcedureHelpers@L86618`, `requirement.23.HostedRootCapsMeaning@L86645`, `def.23.HostedExportMeaning@L86660`
- `requirement.23.HostedExportForeignVisibleSignature@L86673`, `requirement.23.HostedExportForeignVisiblePassKind@L86686`, `def.23.HostExportSignatureJudgements@L86699`, `rule.23.HostExportSig-Ok@L86712`, `rule.23.HostExport-Library-Err@L86728`, `rule.23.HostExport-MixedMode-Err@L86744`, `rule.23.HostExport-Generic-Err@L86760`, `rule.23.HostExport-Context-Err@L86776`
- `rule.23.HostExport-Context-Raw-Err@L86792`, `rule.23.HostExport-Context-Move-Err@L86808`, `requirement.23.HostedExportSessionHandleValidity@L86826`, `requirement.23.HostedExportCapabilityIsolation@L86839`, `requirement.23.HostedSessionRootCapsGrant@L86852`, `requirement.23.HostedExportBoundaryEntrySequence@L86865`, `requirement.23.HostedExportInvalidHandleBehavior@L86884`, `requirement.23.HostedExportCatchFailureReturn@L86900`
- `requirement.23.HostedExportLoweringPreservesRawFfiRules@L86915`, `requirement.23.HostedExportThunkAbiDetermination@L86928`, `def.23.HostThunkCarrierHelpers@L86946`, `rule.23.HostThunkParamCarrier-ByRef@L86974`, `rule.23.HostThunkParamCarrier-ByValue-Default@L86990`, `rule.23.HostThunkParamCarrier-Win64-DirectAgg@L87006`, `rule.23.HostThunkParamCarrier-Win64-IndirectAgg@L87022`, `rule.23.HostThunkRetCarrier-Default@L87038`
- `rule.23.HostThunkRetCarrier-Win64-DirectAgg@L87054`, `rule.23.HostThunkRetCarrier-Win64-SRetAgg@L87070`, `requirement.23.HostedExportThunkShapeUse@L87086`, `requirement.23.HostedExportNoWin64AggregateSplitting@L87099`, `requirement.23.HostedExportNoExtraAbiRewriting@L87112`, `requirement.23.HostedThunkModeIndependentForeignClassification@L87125`, `requirement.23.HostedThunkToSourceCallReconstruction@L87138`, `requirement.23.HostedStateSymbolResolution@L87151`
- `requirement.23.HostedLibraryLifecycleExports@L87164`, `requirement.23.HostedLifecycleExportsBackendGenerated@L87181`, `requirement.23.HostedLifecycleExportsPanicAndDestroyFailure@L87194`, `requirement.23.HostedSessionHandleNoReissue@L87207`, `requirement.23.HostedExportThunkForeignVisibleAbi@L87220`, `requirement.23.HostedExportThunkEmissionAndEntrypoint@L87239`, `diagnostics.23.HostedExportDiagnostics@L87254`, `diagnostics.23.HostedExportDiagnosticOwnership@L87273`
- `grammar.23.FfiAttributes@L87290`, `requirement.23.FfiAttributesParsing@L87319`, `ast.23.FfiAttributesAttachedEntries@L87334`, `ast.23.FfiAttributeTargets@L87347`, `requirement.23.MangleAttributeSemantics@L87371`, `def.23.LibraryLinkKinds@L87390`, `requirement.23.LibraryAttributeSemantics@L87410`, `def.23.ResolveLibraryName@L87427`
- `requirement.23.UnsupportedLibraryKindIllFormed@L87454`, `requirement.23.RawDylibResolution@L87467`, `def.23.UnwindModes@L87485`, `requirement.23.UnwindDefaultMode@L87503`, `requirement.23.UnwindAttributeTargetValidity@L87516`, `requirement.23.UnwindCatchAbiRequirement@L87529`, `requirement.23.ExportAttributeSemantics@L87549`, `requirement.23.HostExportAttributeSemantics@L87569`
- `requirement.23.FfiPassByValueAttributeSemantics@L87591`, `requirement.23.FfiAttributeConstraints@L87604`, `requirement.23.FfiAttributesDynamicSemantics@L87630`, `requirement.23.FfiAttributesLowering@L87645`, `diagnostics.23.FfiAttributeDiagnostics@L87660`, `requirement.23.CapabilityIsolationSyntaxNoAdditionalForm@L87693`, `requirement.23.CapabilityIsolationParsingNoAdditionalRules@L87708`, `ast.23.CapabilityIsolationNoDedicatedAst@L87723`
- `requirement.23.CapabilityIsolationSemantics@L87738`, `def.23.CapabilityIsolationHelpers@L87754`, `rule.23.FFI-Arg-RegionLocalRawPtr-Err@L87771`, `rule.23.FFI-Return-RegionLocalRawPtr-Err@L87789`, `requirement.23.CapabilityIsolationDynamicSemantics@L87809`, `requirement.23.CapabilityIsolationLowering@L87824`, `diagnostics.23.CapabilityIsolationDiagnostics@L87839`, `diagnostics.23.CapabilityIsolationDiagnosticOwnership@L87854`
- `grammar.23.ForeignContracts@L87871`, `def.23.ForeignContractStart@L87896`, `rule.23.Parse-ForeignContractClauseListOpt-None@L87909`, `rule.23.Parse-ForeignContractClauseListOpt-Yes@L87925`, `rule.23.Parse-ForeignContractClauseList-Cons@L87941`, `rule.23.Parse-ForeignContractClauseListTail-End@L87957`, `rule.23.Parse-ForeignContractClauseListTail-Cons@L87973`, `rule.23.Parse-ForeignContractClause-Assumes@L87989`
- `rule.23.Parse-ForeignContractClause-Ensures@L88005`, `def.23.ForeignEnsuresKindAndExpr@L88021`, `rule.23.Parse-EnsuresPredicate-Error@L88040`, `rule.23.Parse-EnsuresPredicate-NullResult@L88056`, `rule.23.Parse-EnsuresPredicate-Plain@L88072`, `ast.23.ForeignContractsForm@L88089`, `ast.23.EnsuresPredicateForms@L88112`, `def.23.ForeignPreconditions@L88134`
- `requirement.23.ForeignPredicateContext@L88147`, `def.23.ForeignPreconditionVerificationModes@L88173`, `requirement.23.ForeignPreconditionVerificationLowering@L88191`, `def.23.ForeignPostconditions@L88206`, `requirement.23.ForeignPostconditionPredicateBindings@L88219`, `def.23.ForeignPostconditionClassification@L88239`, `requirement.23.NullResultWellFormedness@L88272`, `def.23.NullableFfiResult@L88288`
- `rule.23.ForeignEnsures-NullResult-Err@L88304`, `requirement.23.ErrorPredicateWellFormedness@L88323`, `def.23.ForeignPostconditionVerificationModes@L88336`, `requirement.23.ForeignPostconditionStaticVerification@L88354`, `def.23.ForeignContractVerificationSummary@L88369`, `requirement.23.ForeignPreconditionDynamicFailure@L88389`, `requirement.23.ForeignPostconditionDynamicChecks@L88402`, `requirement.23.ForeignContractsLowering@L88417`
- `diagnostics.23.ForeignContractDiagnostics@L88432`, `requirement.23.BoundaryUnwindingSyntax@L88459`, `requirement.23.BoundaryUnwindingParsingNoAdditionalRules@L88474`, `ast.23.BoundaryUnwindPolicySource@L88489`, `def.23.UnwindModeAstHelpers@L88502`, `def.23.DetermineUnwindMode@L88524`, `def.23.ParseUnwindArg@L88549`, `rule.23.UnwindMode-Valid@L88571`
- `rule.23.UnwindMode-Invalid-Err@L88589`, `requirement.23.BoundaryUnwindDynamicEffects@L88609`, `requirement.23.GeneralDestructionAndUnwindCleanupReference@L88628`, `def.23.BoundaryUnwindCodeGenerationEffects@L88643`, `rule.23.CodeGen-UnwindAbort-Import@L88663`, `rule.23.CodeGen-UnwindCatch-Import@L88681`, `rule.23.CodeGen-UnwindAbort-Export@L88699`, `rule.23.CodeGen-UnwindCatch-Export@L88717`
- `diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics@L88737`, `diagnostics.23.BoundaryUnwindingDiagnosticOwnership@L88750`, `diagnostics.23.FfiDiagnosticsSupplement@L88765`

#### `spec.lowering`

Count: 158 total; 158 required; 0 recommended; 0 informative. Ledger line span: L88499-L91244.

- `requirement.24.SharedLoweringScope@L88786`, `def.24.CodegenModelAndTargets@L88801`, `def.24.CodegenJudgements@L88820`, `def.24.IRDefined@L88833`, `def.24.CodegenCorrectnessPredicates@L88846`, `def.24.CodegenCorrectAndUndefined@L88863`, `def.24.IRFormsAndEmissionJudgements@L88880`, `def.24.PanicOutCodegenParams@L88898`
- `def.24.MethodAndTransitionParams@L88912`, `def.24.SeqIR@L88930`, `def.24.EvalOrderJudgements@L88945`, `def.24.ChildExpressionListHelpers@L88958`, `def.24.ChildrenLTRExpressions@L89004`, `def.24.LowerExprJudgementsAndRetType@L89055`, `rule.24.Lower-Expr-Correctness@L89070`, `def.24.LowerExprTotal@L89086`
- `def.24.ExecIRJudgements@L89100`, `rule.24.ExecIR-ReadVar@L89113`, `rule.24.ExecIR-ReadPath@L89129`, `rule.24.ExecIR-StoreVar@L89145`, `rule.24.ExecIR-StoreVarNoDrop@L89161`, `rule.24.ExecIR-BindVar@L89177`, `rule.24.ExecIR-ReadPtr@L89193`, `rule.24.ExecIR-WritePtr@L89209`
- `def.24.AllocTarget@L89225`, `rule.24.ExecIR-Alloc@L89239`, `rule.24.MoveState-Root@L89255`, `rule.24.MoveState-Field@L89271`, `rule.24.ExecIR-MoveState@L89287`, `def.24.ExecIRControlResults@L89303`, `rule.24.ExecIR-Defer@L89319`, `def.24.ExecIRBlockHelpers@L89335`
- `rule.24.ExecIR-If-True@L89350`, `rule.24.ExecIR-If-False@L89366`, `rule.24.ExecIR-Block@L89382`, `rule.24.ExecIR-IfCase@L89398`, `rule.24.ExecIR-Loop-Infinite-Step@L89414`, `rule.24.ExecIR-Loop-Infinite-Continue@L89430`, `rule.24.ExecIR-Loop-Infinite-Break@L89446`, `rule.24.ExecIR-Loop-Infinite-Ctrl@L89462`
- `rule.24.ExecIR-Loop-Cond-False@L89478`, `rule.24.ExecIR-Loop-Cond-True-Step@L89494`, `rule.24.ExecIR-Loop-Cond-Continue@L89510`, `rule.24.ExecIR-Loop-Cond-Break@L89526`, `rule.24.ExecIR-Loop-Cond-Ctrl@L89542`, `rule.24.ExecIR-Loop-Cond-Body-Ctrl@L89558`, `def.24.LoopIterIRJudgement@L89574`, `rule.24.ExecIR-Loop-Iter@L89587`
- `rule.24.ExecIR-Loop-Iter-Ctrl@L89603`, `rule.24.LoopIterIR-Done@L89619`, `rule.24.LoopIterIR-Step-Val@L89635`, `rule.24.LoopIterIR-Step-Continue@L89651`, `rule.24.LoopIterIR-Step-Break@L89667`, `rule.24.LoopIterIR-Step-Ctrl@L89683`, `rule.24.ExecIR-Region@L89699`, `rule.24.ExecIR-Frame-Implicit@L89715`
- `rule.24.ExecIR-Frame-Explicit@L89731`, `rule.24.LowerList-Empty@L89747`, `rule.24.LowerList-Cons@L89762`, `rule.24.LowerFieldInits-Empty@L89778`, `rule.24.LowerFieldInits-Cons@L89793`, `rule.24.LowerOpt-None@L89809`, `rule.24.LowerOpt-Some@L89824`, `def.24.RefSyms@L89840`
- `def.24.ExpandIR@L89902`, `def.24.UniqueEmits@L89915`, `def.24.ModuleItems@L89937`, `rule.24.CG-Project@L89950`, `requirement.24.NoAdditionalFeatureLocalCodegenItemRules@L89966`, `rule.24.CG-Module@L89979`, `rule.24.CG-Expr@L89995`, `rule.24.CG-Stmt@L90011`
- `rule.24.CG-Block@L90027`, `rule.24.CG-Place@L90043`, `rule.24.LowerIR-Module@L90059`, `rule.24.LowerIR-Err@L90075`, `rule.24.EmitLLVM-Ok@L90091`, `def.24.LLVMText21Acceptance@L90107`, `rule.24.EmitLLVM-Err@L90121`, `requirement.24.LLVMToolAcceptanceAndResolveOwnership@L90137`
- `rule.24.EmitObj-Ok@L90151`, `def.24.LLVMEmitObj21@L90167`, `rule.24.EmitObj-Err@L90180`, `def.24.PointerPrimitiveSizeAndAlignment@L90200`, `def.24.LayoutJudgements@L90255`, `rule.24.Size-Prim@L90268`, `rule.24.Align-Prim@L90284`, `rule.24.Layout-Prim@L90300`
- `def.24.ConstantEncodingHelpers@L90316`, `rule.24.Encode-Bool@L90333`, `rule.24.Encode-Char@L90349`, `rule.24.Encode-Int@L90365`, `rule.24.Encode-Float@L90381`, `rule.24.Encode-Unit@L90397`, `rule.24.Encode-Never@L90413`, `rule.24.Encode-RawPtr-Null@L90429`
- `def.24.ValidValueJudgement@L90445`, `rule.24.Valid-Bool@L90458`, `rule.24.Valid-Char@L90472`, `rule.24.Valid-Scalar@L90486`, `rule.24.Valid-Unit@L90501`, `rule.24.Valid-Never@L90515`, `def.24.ValidValueFallback@L90529`, `rule.24.Layout-Perm@L90545`
- `rule.24.Size-Perm@L90561`, `rule.24.Align-Perm@L90577`, `def.24.ValueBitsPerm@L90593`, `rule.24.Size-Ptr@L90606`, `rule.24.Align-Ptr@L90622`, `rule.24.Layout-Ptr@L90638`, `rule.24.Size-RawPtr@L90654`, `rule.24.Align-RawPtr@L90670`
- `rule.24.Layout-RawPtr@L90686`, `rule.24.Size-Func@L90702`, `rule.24.Align-Func@L90718`, `rule.24.Layout-Func@L90734`, `def.24.DefaultCallingConventionAndTargetArtifacts@L90752`, `def.24.ExternAbiSetAndConventionMapping@L90832`, `def.24.ConventionLayout@L90854`, `def.24.AssignParamRegs@L90904`
- `def.24.StackFrameForm@L90930`, `rule.24.StackFrame-Layout@L90950`, `rule.24.Conv-Compatible@L90967`, `rule.24.Conv-FFI-Required@L90983`, `def.24.ABITypeAndABITyJudgement@L91001`, `rule.24.ABI-Prim@L91015`, `rule.24.ABI-Perm@L91031`, `rule.24.ABI-Ptr@L91047`
- `rule.24.ABI-RawPtr@L91063`, `rule.24.ABI-Func@L91079`, `rule.24.ABI-Alias@L91095`, `rule.24.ABI-Record@L91111`, `rule.24.ABI-Tuple@L91127`, `rule.24.ABI-Array@L91143`, `rule.24.ABI-Slice@L91159`, `rule.24.ABI-Range@L91175`
- `rule.24.ABI-RangeInclusive@L91191`, `rule.24.ABI-RangeFrom@L91207`, `rule.24.ABI-RangeTo@L91223`, `rule.24.ABI-RangeToInclusive@L91239`, `rule.24.ABI-RangeFull@L91255`, `rule.24.ABI-Enum@L91271`, `rule.24.ABI-Union@L91287`, `rule.24.ABI-Modal@L91303`
- `rule.24.ABI-Dynamic@L91319`, `rule.24.ABI-StringBytes@L91335`, `def.24.ABIParameterReturnPassingHelpers@L91353`, `requirement.24.ForeignVisibleABIUsesForeignJudgements@L91374`, `rule.24.ABI-Param-ByRef-Alias@L91387`, `rule.24.ABI-Param-ByValue-Move@L91403`, `rule.24.ABI-Param-ByRef-Move@L91419`, `rule.24.ABI-Ret-ByValue@L91435`
- `rule.24.ABI-Ret-ByRef@L91451`, `rule.24.ABI-Call@L91467`, `rule.24.ABI-ForeignParam-ByValue@L91483`, `rule.24.ABI-ForeignParam-ByRef@L91499`, `rule.24.ABI-ForeignCall@L91515`, `def.24.PanicRecordAndPanicOut@L91531`
- `requirement.24.SharedLoweringScope@L88786`, `def.24.CodegenModelAndTargets@L88801`, `def.24.CodegenJudgements@L88820`, `def.24.IRDefined@L88833`, `def.24.CodegenCorrectnessPredicates@L88846`, `def.24.CodegenCorrectAndUndefined@L88863`, `def.24.IRFormsAndEmissionJudgements@L88880`, `def.24.PanicOutCodegenParams@L88898`
- `def.24.MethodAndTransitionParams@L88912`, `def.24.SeqIR@L88930`, `def.24.EvalOrderJudgements@L88945`, `def.24.ChildExpressionListHelpers@L88958`, `def.24.ChildrenLTRExpressions@L89004`, `def.24.LowerExprJudgementsAndRetType@L89055`, `rule.24.Lower-Expr-Correctness@L89070`, `def.24.LowerExprTotal@L89086`
- `def.24.ExecIRJudgements@L89100`, `rule.24.ExecIR-ReadVar@L89113`, `rule.24.ExecIR-ReadPath@L89129`, `rule.24.ExecIR-StoreVar@L89145`, `rule.24.ExecIR-StoreVarNoDrop@L89161`, `rule.24.ExecIR-BindVar@L89177`, `rule.24.ExecIR-ReadPtr@L89193`, `rule.24.ExecIR-WritePtr@L89209`
- `def.24.AllocTarget@L89225`, `rule.24.ExecIR-Alloc@L89239`, `rule.24.MoveState-Root@L89255`, `rule.24.MoveState-Field@L89271`, `rule.24.ExecIR-MoveState@L89287`, `def.24.ExecIRControlResults@L89303`, `rule.24.ExecIR-Defer@L89319`, `def.24.ExecIRBlockHelpers@L89335`
- `rule.24.ExecIR-If-True@L89350`, `rule.24.ExecIR-If-False@L89366`, `rule.24.ExecIR-Block@L89382`, `rule.24.ExecIR-IfCase@L89398`, `rule.24.ExecIR-Loop-Infinite-Step@L89414`, `rule.24.ExecIR-Loop-Infinite-Continue@L89430`, `rule.24.ExecIR-Loop-Infinite-Break@L89446`, `rule.24.ExecIR-Loop-Infinite-Ctrl@L89462`
- `rule.24.ExecIR-Loop-Cond-False@L89478`, `rule.24.ExecIR-Loop-Cond-True-Step@L89494`, `rule.24.ExecIR-Loop-Cond-Continue@L89510`, `rule.24.ExecIR-Loop-Cond-Break@L89526`, `rule.24.ExecIR-Loop-Cond-Ctrl@L89542`, `rule.24.ExecIR-Loop-Cond-Body-Ctrl@L89558`, `def.24.LoopIterIRJudgement@L89574`, `rule.24.ExecIR-Loop-Iter@L89587`
- `rule.24.ExecIR-Loop-Iter-Ctrl@L89603`, `rule.24.LoopIterIR-Done@L89619`, `rule.24.LoopIterIR-Step-Val@L89635`, `rule.24.LoopIterIR-Step-Continue@L89651`, `rule.24.LoopIterIR-Step-Break@L89667`, `rule.24.LoopIterIR-Step-Ctrl@L89683`, `rule.24.ExecIR-Region@L89699`, `rule.24.ExecIR-Frame-Implicit@L89715`
- `rule.24.ExecIR-Frame-Explicit@L89731`, `rule.24.LowerList-Empty@L89747`, `rule.24.LowerList-Cons@L89762`, `rule.24.LowerFieldInits-Empty@L89778`, `rule.24.LowerFieldInits-Cons@L89793`, `rule.24.LowerOpt-None@L89809`, `rule.24.LowerOpt-Some@L89824`, `def.24.RefSyms@L89840`
- `def.24.ExpandIR@L89902`, `def.24.UniqueEmits@L89915`, `def.24.ModuleItems@L89937`, `rule.24.CG-Project@L89950`, `requirement.24.NoAdditionalFeatureLocalCodegenItemRules@L89966`, `rule.24.CG-Module@L89979`, `rule.24.CG-Expr@L89995`, `rule.24.CG-Stmt@L90011`
- `rule.24.CG-Block@L90027`, `rule.24.CG-Place@L90043`, `rule.24.LowerIR-Module@L90059`, `rule.24.LowerIR-Err@L90075`, `rule.24.EmitLLVM-Ok@L90091`, `def.24.LLVMText21Acceptance@L90107`, `rule.24.EmitLLVM-Err@L90121`, `requirement.24.LLVMToolAcceptanceAndResolveOwnership@L90137`
- `rule.24.EmitObj-Ok@L90151`, `def.24.LLVMEmitObj21@L90167`, `rule.24.EmitObj-Err@L90180`, `def.24.PointerPrimitiveSizeAndAlignment@L90200`, `def.24.LayoutJudgements@L90255`, `rule.24.Size-Prim@L90268`, `rule.24.Align-Prim@L90284`, `rule.24.Layout-Prim@L90300`
- `def.24.ConstantEncodingHelpers@L90316`, `rule.24.Encode-Bool@L90333`, `rule.24.Encode-Char@L90349`, `rule.24.Encode-Int@L90365`, `rule.24.Encode-Float@L90381`, `rule.24.Encode-Unit@L90397`, `rule.24.Encode-Never@L90413`, `rule.24.Encode-RawPtr-Null@L90429`
- `def.24.ValidValueJudgement@L90445`, `rule.24.Valid-Bool@L90458`, `rule.24.Valid-Char@L90472`, `rule.24.Valid-Scalar@L90486`, `rule.24.Valid-Unit@L90501`, `rule.24.Valid-Never@L90515`, `def.24.ValidValueFallback@L90529`, `rule.24.Layout-Perm@L90545`
- `rule.24.Size-Perm@L90561`, `rule.24.Align-Perm@L90577`, `def.24.ValueBitsPerm@L90593`, `rule.24.Size-Ptr@L90606`, `rule.24.Align-Ptr@L90622`, `rule.24.Layout-Ptr@L90638`, `rule.24.Size-RawPtr@L90654`, `rule.24.Align-RawPtr@L90670`
- `rule.24.Layout-RawPtr@L90686`, `rule.24.Size-Func@L90702`, `rule.24.Align-Func@L90718`, `rule.24.Layout-Func@L90734`, `def.24.DefaultCallingConventionAndTargetArtifacts@L90752`, `def.24.ExternAbiSetAndConventionMapping@L90832`, `def.24.ConventionLayout@L90854`, `def.24.AssignParamRegs@L90904`
- `def.24.StackFrameForm@L90930`, `rule.24.StackFrame-Layout@L90950`, `rule.24.Conv-Compatible@L90967`, `rule.24.Conv-FFI-Required@L90983`, `def.24.ABITypeAndABITyJudgement@L91001`, `rule.24.ABI-Prim@L91015`, `rule.24.ABI-Perm@L91031`, `rule.24.ABI-Ptr@L91047`
- `rule.24.ABI-RawPtr@L91063`, `rule.24.ABI-Func@L91079`, `rule.24.ABI-Alias@L91095`, `rule.24.ABI-Record@L91111`, `rule.24.ABI-Tuple@L91127`, `rule.24.ABI-Array@L91143`, `rule.24.ABI-Slice@L91159`, `rule.24.ABI-Range@L91175`
- `rule.24.ABI-RangeInclusive@L91191`, `rule.24.ABI-RangeFrom@L91207`, `rule.24.ABI-RangeTo@L91223`, `rule.24.ABI-RangeToInclusive@L91239`, `rule.24.ABI-RangeFull@L91255`, `rule.24.ABI-Enum@L91271`, `rule.24.ABI-Union@L91287`, `rule.24.ABI-Modal@L91303`
- `rule.24.ABI-Dynamic@L91319`, `rule.24.ABI-StringBytes@L91335`, `def.24.ABIParameterReturnPassingHelpers@L91353`, `requirement.24.ForeignVisibleABIUsesForeignJudgements@L91374`, `rule.24.ABI-Param-ByRef-Alias@L91387`, `rule.24.ABI-Param-ByValue-Move@L91403`, `rule.24.ABI-Param-ByRef-Move@L91419`, `rule.24.ABI-Ret-ByValue@L91435`
- `rule.24.ABI-Ret-ByRef@L91451`, `rule.24.ABI-Call@L91467`, `rule.24.ABI-ForeignParam-ByValue@L91483`, `rule.24.ABI-ForeignParam-ByRef@L91499`, `rule.24.ABI-ForeignCall@L91515`, `def.24.PanicRecordAndPanicOut@L91531`

#### `spec.symbols`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L91273-L92088.

- `def.24.MangleJudgementAndConstructors@L91560`, `def.24.PathSymbolHelpers@L91576`, `def.24.ItemPath@L91593`, `def.24.PathOfTypeAndClassPath@L91615`, `def.24.LiteralSymbolHashing@L91636`, `def.24.ScopedRawAndHostBodySymbols@L91655`, `def.24.AttributeSymbolHelpers@L91670`, `def.24.ExternAbiSymbolHelpers@L91690`
- `def.24.LinkName@L91709`, `def.24.HostThunkLinkNameAndItemName@L91727`, `rule.24.Mangle-HostExport-Proc@L91744`, `rule.24.Mangle-Proc@L91760`, `rule.24.Mangle-ExternProc@L91776`, `rule.24.Mangle-Main@L91792`, `rule.24.Mangle-Record-Method@L91808`, `rule.24.Mangle-Class-Method@L91824`
- `rule.24.Mangle-State-Method@L91840`, `rule.24.Mangle-Transition@L91856`, `rule.24.Mangle-Static@L91872`, `rule.24.Mangle-StaticBinding@L91888`, `rule.24.Mangle-VTable@L91904`, `rule.24.Mangle-Literal@L91920`, `rule.24.Mangle-DefaultImpl@L91936`, `req.24.ClosureIndexUniqueness@L91952`
- `def.24.EnclosingSym@L91965`, `rule.24.Mangle-Closure@L91978`, `rule.24.Mangle-ClosureEnv@L91994`, `def.24.ClosureCodeSym@L92010`, `def.24.LinkageDefinitions@L92025`, `rule.24.Linkage-UserItem@L92039`, `rule.24.Linkage-ExternProc@L92055`, `rule.24.Linkage-UserItem-Internal@L92071`
- `rule.24.Linkage-StaticBinding@L92087`, `rule.24.Linkage-StaticBinding-Internal@L92103`, `rule.24.Linkage-ClassMethod@L92119`, `rule.24.Linkage-ClassMethod-Internal@L92135`, `rule.24.Linkage-StateMethod@L92151`, `rule.24.Linkage-StateMethod-Internal@L92167`, `rule.24.Linkage-Transition@L92183`, `rule.24.Linkage-Transition-Internal@L92199`
- `rule.24.Linkage-InitFn@L92215`, `rule.24.Linkage-DeinitFn@L92231`, `rule.24.Linkage-VTable@L92247`, `rule.24.Linkage-LiteralData@L92263`, `rule.24.Linkage-DropGlue@L92279`, `rule.24.Linkage-DefaultImpl@L92295`, `rule.24.Linkage-DefaultImpl-Internal@L92311`, `rule.24.Linkage-PanicSym@L92327`
- `rule.24.Linkage-BuiltinModalSym@L92343`, `rule.24.Linkage-BuiltinSym@L92359`, `rule.24.Linkage-EntrySym@L92375`
- `def.24.MangleJudgementAndConstructors@L91560`, `def.24.PathSymbolHelpers@L91576`, `def.24.ItemPath@L91593`, `def.24.PathOfTypeAndClassPath@L91615`, `def.24.LiteralSymbolHashing@L91636`, `def.24.ScopedRawAndHostBodySymbols@L91655`, `def.24.AttributeSymbolHelpers@L91670`, `def.24.ExternAbiSymbolHelpers@L91690`
- `def.24.LinkName@L91709`, `def.24.HostThunkLinkNameAndItemName@L91727`, `rule.24.Mangle-HostExport-Proc@L91744`, `rule.24.Mangle-Proc@L91760`, `rule.24.Mangle-ExternProc@L91776`, `rule.24.Mangle-Main@L91792`, `rule.24.Mangle-Record-Method@L91808`, `rule.24.Mangle-Class-Method@L91824`
- `rule.24.Mangle-State-Method@L91840`, `rule.24.Mangle-Transition@L91856`, `rule.24.Mangle-Static@L91872`, `rule.24.Mangle-StaticBinding@L91888`, `rule.24.Mangle-VTable@L91904`, `rule.24.Mangle-Literal@L91920`, `rule.24.Mangle-DefaultImpl@L91936`, `req.24.ClosureIndexUniqueness@L91952`
- `def.24.EnclosingSym@L91965`, `rule.24.Mangle-Closure@L91978`, `rule.24.Mangle-ClosureEnv@L91994`, `def.24.ClosureCodeSym@L92010`, `def.24.LinkageDefinitions@L92025`, `rule.24.Linkage-UserItem@L92039`, `rule.24.Linkage-ExternProc@L92055`, `rule.24.Linkage-UserItem-Internal@L92071`
- `rule.24.Linkage-StaticBinding@L92087`, `rule.24.Linkage-StaticBinding-Internal@L92103`, `rule.24.Linkage-ClassMethod@L92119`, `rule.24.Linkage-ClassMethod-Internal@L92135`, `rule.24.Linkage-StateMethod@L92151`, `rule.24.Linkage-StateMethod-Internal@L92167`, `rule.24.Linkage-Transition@L92183`, `rule.24.Linkage-Transition-Internal@L92199`
- `rule.24.Linkage-InitFn@L92215`, `rule.24.Linkage-DeinitFn@L92231`, `rule.24.Linkage-VTable@L92247`, `rule.24.Linkage-LiteralData@L92263`, `rule.24.Linkage-DropGlue@L92279`, `rule.24.Linkage-DefaultImpl@L92295`, `rule.24.Linkage-DefaultImpl-Internal@L92311`, `rule.24.Linkage-PanicSym@L92327`
- `rule.24.Linkage-BuiltinModalSym@L92343`, `rule.24.Linkage-BuiltinSym@L92359`, `rule.24.Linkage-EntrySym@L92375`

#### `spec.initialization`

Count: 102 total; 102 required; 0 recommended; 0 informative. Ledger line span: L92108-L93623.

- `def.24.GlobalsJudg@L92395`, `def.24.ConstInitJudg@L92408`, `def.24.ConstInitLiteral@L92421`, `def.24.StaticName@L92434`, `def.24.StaticBindTypes@L92449`, `def.24.StaticBindList@L92462`, `def.24.StaticBinding@L92475`, `def.24.StaticSym@L92488`
- `rule.24.Emit-Static-Const@L92503`, `rule.24.Emit-Static-Init@L92519`, `rule.24.Emit-Static-Multi@L92535`, `def.24.InitSym@L92551`, `rule.24.InitFn@L92564`, `def.24.DeinitSym@L92580`, `rule.24.DeinitFn@L92593`, `def.24.StaticItems@L92609`
- `def.24.StaticItemOf@L92622`, `def.24.StaticSymPath@L92635`, `def.24.StaticAddr@L92648`, `req.24.HostedStaticAddrSessionInterpretation@L92661`, `def.24.AddrOfSym@L92674`, `def.24.StaticType@L92687`, `def.24.StaticBindInfo@L92700`, `def.24.SeqIRList@L92713`
- `def.24.StaticStoreIR@L92727`, `rule.24.Lower-StaticInit-Item@L92741`, `rule.24.Lower-StaticInitItems-Empty@L92757`, `rule.24.Lower-StaticInitItems-Cons@L92772`, `rule.24.Lower-StaticInit@L92788`, `rule.24.InitCallIR@L92804`, `def.24.Rev@L92820`, `rule.24.Lower-StaticDeinitNames-Empty@L92834`
- `rule.24.Lower-StaticDeinitNames-Cons-Resp@L92849`, `rule.24.Lower-StaticDeinitNames-Cons-NoResp@L92865`, `rule.24.Lower-StaticDeinit-Item@L92881`, `rule.24.Lower-StaticDeinitItems-Empty@L92897`, `rule.24.Lower-StaticDeinitItems-Cons@L92912`, `rule.24.Lower-StaticDeinit@L92928`, `rule.24.DeinitCallIR@L92944`, `def.24.HostedStateAddressDefinitions@L92960`
- `def.24.LibraryStateSymbolDefinitions@L92975`, `def.24.HostedStateJudg@L92991`, `req.24.SessionStateInitDefinesHostedCells@L93004`, `req.24.SessionStateDestroyRemovesHostedCells@L93017`, `req.24.HostedLibraryStateAddressInterpretation@L93030`, `def.24.InitializationGraphOrdering@L93047`, `rule.24.Topo-Ok@L93066`, `rule.24.Topo-Cycle@L93082`
- `def.24.ProjectInitializationItems@L93098`, `def.24.InitializationPlanDefinitions@L93114`, `def.24.EvalFromEvalSigma@L93136`, `rule.24.EmitInitPlan@L93150`, `rule.24.EmitInitPlan-Err@L93166`, `rule.24.EmitDeinitPlan@L93182`, `rule.24.EmitDeinitPlan-Err@L93198`, `def.24.InitStateMachineDefinitions@L93214`
- `rule.24.Init-Start@L93229`, `rule.24.Init-Step@L93244`, `rule.24.Init-Next-Module@L93260`, `rule.24.Init-Panic@L93276`, `rule.24.Init-Done@L93292`, `rule.24.Init-Ok@L93308`, `rule.24.Init-Fail@L93324`, `rule.24.Deinit-Ok@L93340`
- `rule.24.Deinit-Panic@L93356`, `def.24.EntryJudg@L93374`, `rule.24.EntrySym-Decl@L93387`, `rule.24.ContextInitSym-Decl@L93402`, `def.24.PanicRecordInit@L93417`, `def.24.EntryStubSpec@L93430`, `rule.24.EntryStub-Decl@L93448`, `rule.24.EntrySym-Err@L93464`
- `rule.24.EntryStub-Err@L93480`, `def.24.LibraryImageJudg@L93498`, `def.24.LibraryImageStateDefinitions@L93511`, `req.24.DistinctLibraryImageState@L93531`, `req.24.LibraryImageLivenessTransitions@L93544`, `req.24.LibraryImageInitDefinesSharedCells@L93557`, `req.24.LibraryImageDestroyRemovesSharedCells@L93570`, `req.24.SharedLibraryImageStateInterpretation@L93583`
- `req.24.PartialInitPanicCleanupPrefix@L93596`, `req.24.RawExportImageLifecycle@L93609`, `req.24.SharedLibraryLinkedCallImageLifecycle@L93622`, `req.24.SharedLibraryLoaderEntrypoint@L93635`, `rule.24.LibraryImageInitSigma@L93648`, `rule.24.RawLibraryCallSigma-Ok@L93664`, `rule.24.LibraryImageDestroySigma@L93680`, `def.24.HostedSessionJudg@L93696`
- `def.24.HostedSessionStateDefinitions@L93709`, `req.24.DistinctHostedState@L93731`, `req.24.HostedSessionLifecycleState@L93744`, `req.24.HostedSessionNoConcurrentReentry@L93757`, `rule.24.HostSessionInitSigma@L93770`, `rule.24.HostedCallSigma-Ok@L93786`, `rule.24.HostSessionDestroySigma@L93802`, `def.24.InterpJudg@L93820`
- `def.24.ContextValue@L93833`, `rule.24.ContextInitSigma@L93846`, `rule.24.Interpret-Project-Ok@L93862`, `rule.24.Interpret-Project-Init-Panic@L93878`, `rule.24.Interpret-Project-Main-Ctrl@L93894`, `rule.24.Interpret-Project-Deinit-Panic@L93910`
- `def.24.GlobalsJudg@L92395`, `def.24.ConstInitJudg@L92408`, `def.24.ConstInitLiteral@L92421`, `def.24.StaticName@L92434`, `def.24.StaticBindTypes@L92449`, `def.24.StaticBindList@L92462`, `def.24.StaticBinding@L92475`, `def.24.StaticSym@L92488`
- `rule.24.Emit-Static-Const@L92503`, `rule.24.Emit-Static-Init@L92519`, `rule.24.Emit-Static-Multi@L92535`, `def.24.InitSym@L92551`, `rule.24.InitFn@L92564`, `def.24.DeinitSym@L92580`, `rule.24.DeinitFn@L92593`, `def.24.StaticItems@L92609`
- `def.24.StaticItemOf@L92622`, `def.24.StaticSymPath@L92635`, `def.24.StaticAddr@L92648`, `req.24.HostedStaticAddrSessionInterpretation@L92661`, `def.24.AddrOfSym@L92674`, `def.24.StaticType@L92687`, `def.24.StaticBindInfo@L92700`, `def.24.SeqIRList@L92713`
- `def.24.StaticStoreIR@L92727`, `rule.24.Lower-StaticInit-Item@L92741`, `rule.24.Lower-StaticInitItems-Empty@L92757`, `rule.24.Lower-StaticInitItems-Cons@L92772`, `rule.24.Lower-StaticInit@L92788`, `rule.24.InitCallIR@L92804`, `def.24.Rev@L92820`, `rule.24.Lower-StaticDeinitNames-Empty@L92834`
- `rule.24.Lower-StaticDeinitNames-Cons-Resp@L92849`, `rule.24.Lower-StaticDeinitNames-Cons-NoResp@L92865`, `rule.24.Lower-StaticDeinit-Item@L92881`, `rule.24.Lower-StaticDeinitItems-Empty@L92897`, `rule.24.Lower-StaticDeinitItems-Cons@L92912`, `rule.24.Lower-StaticDeinit@L92928`, `rule.24.DeinitCallIR@L92944`, `def.24.HostedStateAddressDefinitions@L92960`
- `def.24.LibraryStateSymbolDefinitions@L92975`, `def.24.HostedStateJudg@L92991`, `req.24.SessionStateInitDefinesHostedCells@L93004`, `req.24.SessionStateDestroyRemovesHostedCells@L93017`, `req.24.HostedLibraryStateAddressInterpretation@L93030`, `def.24.InitializationGraphOrdering@L93047`, `rule.24.Topo-Ok@L93066`, `rule.24.Topo-Cycle@L93082`
- `def.24.ProjectInitializationItems@L93098`, `def.24.InitializationPlanDefinitions@L93114`, `def.24.EvalFromEvalSigma@L93136`, `rule.24.EmitInitPlan@L93150`, `rule.24.EmitInitPlan-Err@L93166`, `rule.24.EmitDeinitPlan@L93182`, `rule.24.EmitDeinitPlan-Err@L93198`, `def.24.InitStateMachineDefinitions@L93214`
- `rule.24.Init-Start@L93229`, `rule.24.Init-Step@L93244`, `rule.24.Init-Next-Module@L93260`, `rule.24.Init-Panic@L93276`, `rule.24.Init-Done@L93292`, `rule.24.Init-Ok@L93308`, `rule.24.Init-Fail@L93324`, `rule.24.Deinit-Ok@L93340`
- `rule.24.Deinit-Panic@L93356`, `def.24.EntryJudg@L93374`, `rule.24.EntrySym-Decl@L93387`, `rule.24.ContextInitSym-Decl@L93402`, `def.24.PanicRecordInit@L93417`, `def.24.EntryStubSpec@L93430`, `rule.24.EntryStub-Decl@L93448`, `rule.24.EntrySym-Err@L93464`
- `rule.24.EntryStub-Err@L93480`, `def.24.LibraryImageJudg@L93498`, `def.24.LibraryImageStateDefinitions@L93511`, `req.24.DistinctLibraryImageState@L93531`, `req.24.LibraryImageLivenessTransitions@L93544`, `req.24.LibraryImageInitDefinesSharedCells@L93557`, `req.24.LibraryImageDestroyRemovesSharedCells@L93570`, `req.24.SharedLibraryImageStateInterpretation@L93583`
- `req.24.PartialInitPanicCleanupPrefix@L93596`, `req.24.RawExportImageLifecycle@L93609`, `req.24.SharedLibraryLinkedCallImageLifecycle@L93622`, `req.24.SharedLibraryLoaderEntrypoint@L93635`, `rule.24.LibraryImageInitSigma@L93648`, `rule.24.RawLibraryCallSigma-Ok@L93664`, `rule.24.LibraryImageDestroySigma@L93680`, `def.24.HostedSessionJudg@L93696`
- `def.24.HostedSessionStateDefinitions@L93709`, `req.24.DistinctHostedState@L93731`, `req.24.HostedSessionLifecycleState@L93744`, `req.24.HostedSessionNoConcurrentReentry@L93757`, `rule.24.HostSessionInitSigma@L93770`, `rule.24.HostedCallSigma-Ok@L93786`, `rule.24.HostSessionDestroySigma@L93802`, `def.24.InterpJudg@L93820`
- `def.24.ContextValue@L93833`, `rule.24.ContextInitSigma@L93846`, `rule.24.Interpret-Project-Ok@L93862`, `rule.24.Interpret-Project-Init-Panic@L93878`, `rule.24.Interpret-Project-Main-Ctrl@L93894`, `rule.24.Interpret-Project-Deinit-Panic@L93910`

#### `spec.cleanup`

Count: 56 total; 56 required; 0 recommended; 0 informative. Ledger line span: L93645-L94497.

- `def.24.CleanupJudg@L93932`, `rule.24.CleanupPlan@L93945`, `def.24.EmitDropSpec@L93961`, `def.24.PanicOutAddr@L93977`, `def.24.PanicRecordOf@L93990`, `def.24.WritePanicRecord@L94003`, `def.24.InitPanicHandle@L94016`, `req.24.InitPanicHandleResponsiblePrefix@L94029`
- `rule.24.PanicSym@L94042`, `def.24.PanicReasonCodes@L94057`, `def.24.PanicSites@L94082`, `def.24.ClearPanic@L94106`, `def.24.PanicCheck@L94119`, `def.24.LowerPanic@L94132`, `def.24.ResponsibleBinding@L94147`, `grammar.24.CleanupItem@L94160`
- `def.24.DropJudgmentDefinitions@L94173`, `def.24.RecordType@L94190`, `def.24.DropCall@L94203`, `def.24.ReleaseValue@L94220`, `def.24.DropChildren@L94234`, `def.24.DropList@L94254`, `rule.24.DropAction-Moved@L94268`, `rule.24.DropAction-Partial@L94284`
- `rule.24.DropAction-Valid@L94300`, `rule.24.DropStaticAction@L94316`, `def.24.NonRecordFOk@L94332`, `rule.24.DropValueOut-DropPanic@L94345`, `rule.24.DropValueOut-ChildPanic@L94361`, `rule.24.DropValueOut-Ok@L94377`, `def.24.CleanupStateDefinitions@L94395`, `rule.24.Cleanup-Start@L94409`
- `rule.24.Cleanup-Step-Drop-Ok@L94424`, `rule.24.Cleanup-Step-Drop-Panic@L94440`, `rule.24.Cleanup-Step-Drop-Abort@L94456`, `rule.24.Cleanup-Step-DropStatic-Ok@L94472`, `rule.24.Cleanup-Step-DropStatic-Panic@L94488`, `rule.24.Cleanup-Step-DropStatic-Abort@L94504`, `rule.24.Cleanup-Step-Defer-Ok@L94520`, `rule.24.Cleanup-Step-Defer-Panic@L94536`
- `rule.24.Cleanup-Step-Defer-Abort@L94552`, `rule.24.Cleanup-Done@L94568`, `rule.24.Destroy-Empty@L94584`, `rule.24.Destroy-Cons@L94599`, `def.24.CleanupJudgDyn@L94615`, `rule.24.Cleanup-Empty@L94628`, `rule.24.Cleanup-Cons-Drop@L94643`, `rule.24.Cleanup-Cons-Drop-Panic@L94659`
- `rule.24.Cleanup-Cons-DropStatic@L94675`, `rule.24.Cleanup-Cons-DropStatic-Panic@L94691`, `rule.24.Cleanup-Cons-Defer-Ok@L94707`, `rule.24.Cleanup-Cons-Defer-Panic@L94723`, `def.24.CleanupScopeJudg@L94739`, `rule.24.CleanupScope-From-SmallStep@L94752`, `rule.24.Unwind-Step@L94768`, `rule.24.Unwind-Abort@L94784`
- `def.24.CleanupJudg@L93932`, `rule.24.CleanupPlan@L93945`, `def.24.EmitDropSpec@L93961`, `def.24.PanicOutAddr@L93977`, `def.24.PanicRecordOf@L93990`, `def.24.WritePanicRecord@L94003`, `def.24.InitPanicHandle@L94016`, `req.24.InitPanicHandleResponsiblePrefix@L94029`
- `rule.24.PanicSym@L94042`, `def.24.PanicReasonCodes@L94057`, `def.24.PanicSites@L94082`, `def.24.ClearPanic@L94106`, `def.24.PanicCheck@L94119`, `def.24.LowerPanic@L94132`, `def.24.ResponsibleBinding@L94147`, `grammar.24.CleanupItem@L94160`
- `def.24.DropJudgmentDefinitions@L94173`, `def.24.RecordType@L94190`, `def.24.DropCall@L94203`, `def.24.ReleaseValue@L94220`, `def.24.DropChildren@L94234`, `def.24.DropList@L94254`, `rule.24.DropAction-Moved@L94268`, `rule.24.DropAction-Partial@L94284`
- `rule.24.DropAction-Valid@L94300`, `rule.24.DropStaticAction@L94316`, `def.24.NonRecordFOk@L94332`, `rule.24.DropValueOut-DropPanic@L94345`, `rule.24.DropValueOut-ChildPanic@L94361`, `rule.24.DropValueOut-Ok@L94377`, `def.24.CleanupStateDefinitions@L94395`, `rule.24.Cleanup-Start@L94409`
- `rule.24.Cleanup-Step-Drop-Ok@L94424`, `rule.24.Cleanup-Step-Drop-Panic@L94440`, `rule.24.Cleanup-Step-Drop-Abort@L94456`, `rule.24.Cleanup-Step-DropStatic-Ok@L94472`, `rule.24.Cleanup-Step-DropStatic-Panic@L94488`, `rule.24.Cleanup-Step-DropStatic-Abort@L94504`, `rule.24.Cleanup-Step-Defer-Ok@L94520`, `rule.24.Cleanup-Step-Defer-Panic@L94536`
- `rule.24.Cleanup-Step-Defer-Abort@L94552`, `rule.24.Cleanup-Done@L94568`, `rule.24.Destroy-Empty@L94584`, `rule.24.Destroy-Cons@L94599`, `def.24.CleanupJudgDyn@L94615`, `rule.24.Cleanup-Empty@L94628`, `rule.24.Cleanup-Cons-Drop@L94643`, `rule.24.Cleanup-Cons-Drop-Panic@L94659`
- `rule.24.Cleanup-Cons-DropStatic@L94675`, `rule.24.Cleanup-Cons-DropStatic-Panic@L94691`, `rule.24.Cleanup-Cons-Defer-Ok@L94707`, `rule.24.Cleanup-Cons-Defer-Panic@L94723`, `def.24.CleanupScopeJudg@L94739`, `rule.24.CleanupScope-From-SmallStep@L94752`, `rule.24.Unwind-Step@L94768`, `rule.24.Unwind-Abort@L94784`

#### `spec.runtime-interface`

Count: 64 total; 64 required; 0 recommended; 0 informative. Ledger line span: L94519-L95514.

- `def.24.RuntimeIfcJudg@L94806`, `def.24.BuiltinModalLayoutSpec@L94819`, `rule.24.BuiltinModalLayout@L94832`, `def.24.BuiltinModalSymMap@L94848`, `rule.24.BuiltinModalSym@L94875`, `rule.24.RegionAddr-AddrIsActive@L94891`, `rule.24.RegionAddr-AddrTagFrom@L94906`, `rule.24.BuiltinSym-FileSystem-OpenRead@L94921`
- `rule.24.BuiltinSym-FileSystem-OpenWrite@L94936`, `rule.24.BuiltinSym-FileSystem-OpenAppend@L94951`, `rule.24.BuiltinSym-FileSystem-CreateWrite@L94966`, `rule.24.BuiltinSym-FileSystem-ReadFile@L94981`, `rule.24.BuiltinSym-FileSystem-ReadBytes@L94996`, `rule.24.BuiltinSym-FileSystem-WriteFile@L95011`, `rule.24.BuiltinSym-FileSystem-WriteStdout@L95026`, `rule.24.BuiltinSym-FileSystem-WriteStderr@L95041`
- `rule.24.BuiltinSym-FileSystem-Exists@L95056`, `rule.24.BuiltinSym-FileSystem-Remove@L95071`, `rule.24.BuiltinSym-FileSystem-OpenDir@L95086`, `rule.24.BuiltinSym-FileSystem-CreateDir@L95101`, `rule.24.BuiltinSym-FileSystem-EnsureDir@L95116`, `rule.24.BuiltinSym-FileSystem-Kind@L95131`, `rule.24.BuiltinSym-FileSystem-Restrict@L95146`, `rule.24.BuiltinSym-Network-RestrictHost@L95161`
- `rule.24.BuiltinSym-HeapAllocator-WithQuota@L95176`, `rule.24.BuiltinSym-HeapAllocator-AllocRaw@L95191`, `rule.24.BuiltinSym-HeapAllocator-DeallocRaw@L95206`, `rule.24.BuiltinSym-Reactor-Run@L95221`, `rule.24.BuiltinSym-Reactor-Register@L95236`, `rule.24.BuiltinSym-System-Exit@L95251`, `rule.24.BuiltinSym-System-GetEnv@L95266`, `rule.24.BuiltinSym-System-Run@L95281`
- `def.24.BuiltinSymJudg@L95298`, `def.24.StringBytesBuiltinMethodSets@L95311`, `def.24.StringBuiltinSymbols@L95327`, `def.24.BytesBuiltinSymbols@L95346`, `rule.24.BuiltinSym-String-Err@L95368`, `rule.24.BuiltinSym-Bytes-Err@L95384`, `def.24.DropHookJudg@L95400`, `rule.24.StringDropSym-Decl@L95413`
- `rule.24.BytesDropSym-Decl@L95428`, `rule.24.StringDropSym-Err@L95443`, `rule.24.BytesDropSym-Err@L95459`, `def.24.RuntimeDeclJudg@L95477`, `def.24.RuntimeMethodAndSymbolSets@L95490`, `def.24.CapabilityBuiltinSigs@L95509`, `def.24.CoreRuntimeSigs@L95527`, `def.24.BuiltinModalProcSigs@L95543`
- `def.24.RuntimeSigBuiltinModalAndMethodDispatch@L95561`, `def.24.LLVMDeclType@L95580`, `rule.24.RuntimeDecls@L95593`, `def.24.RuntimeDeclarationCoverage@L95609`, `rule.24.Prim-Network-RestrictHost-Runtime@L95628`, `def.24.HeapJudg@L95644`, `req.24.HeapHostPrimitiveRelations@L95657`, `def.24.HeapStateAccountingDefinitions@L95670`
- `req.24.HeapPrimitiveSemantics@L95685`, `rule.24.Prim-Heap-WithQuota@L95711`, `rule.24.Prim-Heap-AllocRaw@L95727`, `rule.24.Prim-Heap-DeallocRaw@L95743`, `def.24.ReactorJudg@L95759`, `req.24.ReactorHostPrimitiveRelations@L95772`, `rule.24.Prim-Reactor-Run@L95785`, `rule.24.Prim-Reactor-Register@L95801`
- `def.24.RuntimeIfcJudg@L94806`, `def.24.BuiltinModalLayoutSpec@L94819`, `rule.24.BuiltinModalLayout@L94832`, `def.24.BuiltinModalSymMap@L94848`, `rule.24.BuiltinModalSym@L94875`, `rule.24.RegionAddr-AddrIsActive@L94891`, `rule.24.RegionAddr-AddrTagFrom@L94906`, `rule.24.BuiltinSym-FileSystem-OpenRead@L94921`
- `rule.24.BuiltinSym-FileSystem-OpenWrite@L94936`, `rule.24.BuiltinSym-FileSystem-OpenAppend@L94951`, `rule.24.BuiltinSym-FileSystem-CreateWrite@L94966`, `rule.24.BuiltinSym-FileSystem-ReadFile@L94981`, `rule.24.BuiltinSym-FileSystem-ReadBytes@L94996`, `rule.24.BuiltinSym-FileSystem-WriteFile@L95011`, `rule.24.BuiltinSym-FileSystem-WriteStdout@L95026`, `rule.24.BuiltinSym-FileSystem-WriteStderr@L95041`
- `rule.24.BuiltinSym-FileSystem-Exists@L95056`, `rule.24.BuiltinSym-FileSystem-Remove@L95071`, `rule.24.BuiltinSym-FileSystem-OpenDir@L95086`, `rule.24.BuiltinSym-FileSystem-CreateDir@L95101`, `rule.24.BuiltinSym-FileSystem-EnsureDir@L95116`, `rule.24.BuiltinSym-FileSystem-Kind@L95131`, `rule.24.BuiltinSym-FileSystem-Restrict@L95146`, `rule.24.BuiltinSym-Network-RestrictHost@L95161`
- `rule.24.BuiltinSym-HeapAllocator-WithQuota@L95176`, `rule.24.BuiltinSym-HeapAllocator-AllocRaw@L95191`, `rule.24.BuiltinSym-HeapAllocator-DeallocRaw@L95206`, `rule.24.BuiltinSym-Reactor-Run@L95221`, `rule.24.BuiltinSym-Reactor-Register@L95236`, `rule.24.BuiltinSym-System-Exit@L95251`, `rule.24.BuiltinSym-System-GetEnv@L95266`, `rule.24.BuiltinSym-System-Run@L95281`
- `def.24.BuiltinSymJudg@L95298`, `def.24.StringBytesBuiltinMethodSets@L95311`, `def.24.StringBuiltinSymbols@L95327`, `def.24.BytesBuiltinSymbols@L95346`, `rule.24.BuiltinSym-String-Err@L95368`, `rule.24.BuiltinSym-Bytes-Err@L95384`, `def.24.DropHookJudg@L95400`, `rule.24.StringDropSym-Decl@L95413`
- `rule.24.BytesDropSym-Decl@L95428`, `rule.24.StringDropSym-Err@L95443`, `rule.24.BytesDropSym-Err@L95459`, `def.24.RuntimeDeclJudg@L95477`, `def.24.RuntimeMethodAndSymbolSets@L95490`, `def.24.CapabilityBuiltinSigs@L95509`, `def.24.CoreRuntimeSigs@L95527`, `def.24.BuiltinModalProcSigs@L95543`
- `def.24.RuntimeSigBuiltinModalAndMethodDispatch@L95561`, `def.24.LLVMDeclType@L95580`, `rule.24.RuntimeDecls@L95593`, `def.24.RuntimeDeclarationCoverage@L95609`, `rule.24.Prim-Network-RestrictHost-Runtime@L95628`, `def.24.HeapJudg@L95644`, `req.24.HeapHostPrimitiveRelations@L95657`, `def.24.HeapStateAccountingDefinitions@L95670`
- `req.24.HeapPrimitiveSemantics@L95685`, `rule.24.Prim-Heap-WithQuota@L95711`, `rule.24.Prim-Heap-AllocRaw@L95727`, `rule.24.Prim-Heap-DeallocRaw@L95743`, `def.24.ReactorJudg@L95759`, `req.24.ReactorHostPrimitiveRelations@L95772`, `rule.24.Prim-Reactor-Run@L95785`, `rule.24.Prim-Reactor-Register@L95801`

#### `spec.backend`

Count: 190 total; 190 required; 0 recommended; 0 informative. Ledger line span: L95536-L98637.

- `def.24.LLVMHeader@L95823`, `def.24.OpaquePointerModel@L95838`, `def.24.LLVMAttrJudg@L95860`, `rule.24.PtrStateOf-Perm@L95873`, `rule.24.LLVM-PtrAttrs-Valid@L95889`, `rule.24.LLVM-PtrAttrs-Other@L95905`, `rule.24.LLVM-PtrAttrs-RawPtr@L95921`, `rule.24.LLVM-ArgAttrs-Ptr@L95937`
- `rule.24.LLVM-ArgAttrs-RawPtr@L95954`, `rule.24.LLVM-ArgAttrs-NonPtr@L95970`, `def.24.LLVMOptionalArgumentAttrs@L95986`, `def.24.LLVMUBAndPoisonAvoidance@L96004`, `def.24.MemoryIntrinsics@L96031`, `def.24.LLVMToolchain@L96054`, `req.24.HostedCompilerLLVMVersion@L96067`, `def.24.LLVMTyJudg@L96082`
- `def.24.LLVMPrimitiveTypeHelpers@L96095`, `def.24.StructElems@L96131`, `def.24.TaggedElems@L96149`, `rule.24.LLVMTy-Prim@L96166`, `rule.24.LLVMTy-Perm@L96182`, `rule.24.LLVMTy-Refine@L96198`, `rule.24.LLVMTy-Ptr@L96214`, `rule.24.LLVMTy-RawPtr@L96230`
- `rule.24.LLVMTy-Func@L96246`, `rule.24.LLVMTy-Closure@L96262`, `rule.24.LLVMTy-Alias@L96278`, `rule.24.LLVMTy-Record@L96294`, `rule.24.LLVMTy-Tuple@L96310`, `rule.24.LLVMTy-Array@L96326`, `rule.24.LLVMTy-Slice@L96342`, `rule.24.LLVMTy-Range@L96358`
- `rule.24.LLVMTy-RangeInclusive@L96374`, `rule.24.LLVMTy-RangeFrom@L96390`, `rule.24.LLVMTy-RangeTo@L96406`, `rule.24.LLVMTy-RangeToInclusive@L96422`, `rule.24.LLVMTy-RangeFull@L96438`, `rule.24.LLVMTy-Enum@L96453`, `rule.24.LLVMTy-Union-Niche@L96469`, `rule.24.LLVMTy-Union-Tagged@L96485`
- `rule.24.LLVMTy-Modal-Niche@L96501`, `rule.24.LLVMTy-Modal-Tagged@L96517`, `rule.24.LLVMTy-Modal-StringBytes@L96533`, `rule.24.LLVMTy-ModalState@L96551`, `rule.24.LLVMTy-Dynamic@L96567`, `rule.24.LLVMTy-StringView@L96583`, `rule.24.LLVMTy-StringManaged@L96599`, `rule.24.LLVMTy-BytesView@L96615`
- `rule.24.LLVMTy-BytesManaged@L96631`, `rule.24.LLVMTy-Err@L96647`, `def.24.LowerIRJudg@L96665`, `def.24.LLVMInstrHelpers@L96678`, `rule.24.LowerIRInstr-Empty@L96709`, `rule.24.LowerIRInstr-Seq@L96724`, `def.24.MemoryInstructionHelpers@L96740`, `def.24.ConstBytesEncoding@L96757`
- `def.24.StaticTypeBySymbol@L96783`, `def.24.StateRefJudg@L96797`, `rule.24.StateRef-Session@L96811`, `rule.24.StateRef-Global@L96827`, `def.24.CallSignatureHelpers@L96843`, `def.24.ParamInitHelpers@L96862`, `rule.24.LowerIRDecl-Proc-User@L96886`, `rule.24.LowerIRDecl-Proc-Gen@L96902`
- `rule.24.LowerIRDecl-GlobalConst@L96918`, `rule.24.LowerIRDecl-GlobalZero@L96934`, `req.24.HostedStateInitializerTemplates@L96950`, `rule.24.LowerIRDecl-VTable@L96963`, `rule.24.Lower-AllocIR@L96979`, `rule.24.Lower-BindVarIR@L96995`, `rule.24.Lower-ReadVarIR@L97011`, `rule.24.Lower-ReadVarIR-Err@L97027`
- `def.24.ProcSymbol@L97043`, `rule.24.Lower-ReadPathIR-Static-User@L97056`, `rule.24.Lower-ReadPathIR-Static-Gen@L97072`, `rule.24.Lower-ReadPathIR-Proc-User@L97088`, `rule.24.Lower-ReadPathIR-Proc-Gen@L97104`, `rule.24.Lower-ReadPathIR-Runtime@L97120`, `rule.24.Lower-ReadPathIR-Record@L97136`, `rule.24.Lower-StoreVarIR@L97152`
- `rule.24.Lower-StoreVarNoDropIR@L97168`, `rule.24.Lower-MoveStateIR@L97184`, `rule.24.Lower-StoreGlobal@L97200`, `rule.24.Lower-ReadPlaceIR@L97216`, `rule.24.Lower-WritePlaceIR@L97232`, `def.24.PtrType@L97248`, `rule.24.Lower-ReadPtrIR@L97261`, `rule.24.Lower-ReadPtrIR-Raw@L97277`
- `rule.24.Lower-ReadPtrIR-Null@L97293`, `rule.24.Lower-ReadPtrIR-Expired@L97309`, `rule.24.Lower-WritePtrIR@L97325`, `rule.24.Lower-WritePtrIR-Null@L97341`, `rule.24.Lower-WritePtrIR-Expired@L97357`, `rule.24.Lower-WritePtrIR-Raw@L97373`, `rule.24.Lower-WritePtrIR-Raw-Err@L97389`, `rule.24.Lower-AddrOfIR@L97405`
- `def.24.CallLoweringHelpers@L97421`, `rule.24.Lower-CallIR-Func@L97449`, `def.24.DynamicDispatchHelpers@L97465`, `rule.24.Lower-CallVTable@L97483`, `rule.24.LowerIRInstr-ClearPanic@L97499`, `rule.24.LowerIRInstr-PanicCheck@L97515`, `rule.24.LowerIRInstr-CheckPoison@L97531`, `rule.24.LowerIRInstr-LowerPanic@L97547`
- `def.24.IfLoweringHelpers@L97563`, `rule.24.Lower-IfIR@L97580`, `def.24.BlockCleanupLoweringHelpers@L97596`, `rule.24.Lower-BlockIR@L97612`, `def.24.StructuredIRLoweringForms@L97628`, `rule.24.Lower-LoopIR@L97656`, `rule.24.Lower-IfCaseIR@L97672`, `rule.24.Lower-RegionIR@L97688`
- `rule.24.Lower-FrameIR@L97704`, `def.24.BranchLowerForms@L97720`, `rule.24.Lower-BranchIR-Unconditional@L97734`, `rule.24.Lower-BranchIR-Conditional@L97750`, `def.24.PhiLowerForm@L97765`, `rule.24.Lower-PhiIR@L97778`, `rule.24.LowerIRDecl-Err@L97794`, `rule.24.LowerIRInstr-Err@L97810`
- `def.24.BindStorageJudg@L97828`, `def.24.BindRegionTarget@L97851`, `req.24.ResolveTargetNearestLiveAlias@L97870`, `rule.24.BindValid-Sigma@L97883`, `rule.24.BindSlot-Param-ByValue@L97899`, `rule.24.BindSlot-Param-ByRef@L97915`, `rule.24.BindSlot-Region@L97931`, `rule.24.BindSlot-Local@L97947`
- `rule.24.BindSlot-Static@L97963`, `rule.24.UpdateValid-BindVar@L97979`, `rule.24.UpdateValid-StoreVar@L97994`, `rule.24.UpdateValid-StoreVarNoDrop@L98009`, `rule.24.UpdateValid-MoveRoot@L98025`, `rule.24.UpdateValid-PartialMove-Init@L98041`, `rule.24.UpdateValid-PartialMove-Step@L98057`, `def.24.DropOnAssignHelpers@L98073`
- `rule.24.DropOnAssign-NotApplicable@L98089`, `rule.24.DropOnAssign-Record-Valid@L98105`, `rule.24.DropOnAssign-Record-Partial@L98121`, `rule.24.DropOnAssign-Record-Moved@L98137`, `rule.24.DropOnAssign-Aggregate-Ok@L98153`, `rule.24.DropOnAssign-Aggregate-Moved@L98169`, `rule.24.BindSlot-Err@L98185`, `rule.24.BindValid-Err@L98201`
- `rule.24.UpdateValid-Err@L98217`, `rule.24.DropOnAssign-Err@L98233`, `def.24.LLVMCallJudg@L98251`, `def.24.LLVMCallSigFields@L98264`, `rule.24.LLVMArgLower-ByValue-PtrValid@L98280`, `rule.24.LLVMArgLower-ByValue-Other@L98296`, `rule.24.LLVMArgLower-ByRef@L98312`, `rule.24.LLVMRetLower-ByValue-ZST@L98328`
- `rule.24.LLVMRetLower-ByValue@L98344`, `rule.24.LLVMRetLower-SRet@L98360`, `def.24.LLVMCallArgLists@L98376`, `rule.24.LLVMCall-ByValue@L98391`, `rule.24.LLVMCall-SRet@L98407`, `def.24.ByRefAccess@L98423`, `rule.24.LLVMArgLower-Err@L98438`, `rule.24.LLVMRetLower-Err@L98454`
- `rule.24.LLVMCall-Err@L98470`, `def.24.VTableJudg@L98488`, `def.24.VTableEmissionHelpers@L98501`, `rule.24.EmitDropGlue-Decl@L98525`, `rule.24.EmitVTable-Err@L98541`, `def.24.LiteralEmitJudg@L98559`, `def.24.StringBytesAndRawBytes@L98572`, `rule.24.EmitLiteralData-Decl@L98595`
- `rule.24.EmitLiteral-String@L98611`, `req.24.EmitLiteral-String-Utf8Valid@L98627`, `rule.24.EmitLiteral-Bytes@L98640`, `req.24.EmitLiteral-Bytes-UndefinedRawBytes@L98656`, `rule.24.EmitLiteral-Char@L98669`, `rule.24.EmitLiteral-Int@L98685`, `rule.24.EmitLiteral-Float@L98701`, `rule.24.EmitLiteral-Err@L98717`
- `def.24.PoisonJudg@L98735`, `def.24.PoisonSet@L98748`, `rule.24.PoisonFlag-Decl@L98761`, `def.24.PoisonFlagStorage@L98776`, `req.24.HostedPoisonFlagTemplate@L98790`, `rule.24.CheckPoison-Use@L98803`, `sem.24.CheckPoisonBehavior@L98819`, `req.24.HostedPoisonStateIsolation@L98832`
- `rule.24.SetPoison-OnInitFail@L98845`, `rule.24.PoisonFlag-Err@L98861`, `rule.24.CheckPoison-Err@L98877`, `rule.24.SetPoison-Err@L98893`, `req.24.OutputBackendDiagnosticsOwnership@L98911`, `diag.24.OutputBackendDiagnostics@L98924`
- `def.24.LLVMHeader@L95823`, `def.24.OpaquePointerModel@L95838`, `def.24.LLVMAttrJudg@L95860`, `rule.24.PtrStateOf-Perm@L95873`, `rule.24.LLVM-PtrAttrs-Valid@L95889`, `rule.24.LLVM-PtrAttrs-Other@L95905`, `rule.24.LLVM-PtrAttrs-RawPtr@L95921`, `rule.24.LLVM-ArgAttrs-Ptr@L95937`
- `rule.24.LLVM-ArgAttrs-RawPtr@L95954`, `rule.24.LLVM-ArgAttrs-NonPtr@L95970`, `def.24.LLVMOptionalArgumentAttrs@L95986`, `def.24.LLVMUBAndPoisonAvoidance@L96004`, `def.24.MemoryIntrinsics@L96031`, `def.24.LLVMToolchain@L96054`, `req.24.HostedCompilerLLVMVersion@L96067`, `def.24.LLVMTyJudg@L96082`
- `def.24.LLVMPrimitiveTypeHelpers@L96095`, `def.24.StructElems@L96131`, `def.24.TaggedElems@L96149`, `rule.24.LLVMTy-Prim@L96166`, `rule.24.LLVMTy-Perm@L96182`, `rule.24.LLVMTy-Refine@L96198`, `rule.24.LLVMTy-Ptr@L96214`, `rule.24.LLVMTy-RawPtr@L96230`
- `rule.24.LLVMTy-Func@L96246`, `rule.24.LLVMTy-Closure@L96262`, `rule.24.LLVMTy-Alias@L96278`, `rule.24.LLVMTy-Record@L96294`, `rule.24.LLVMTy-Tuple@L96310`, `rule.24.LLVMTy-Array@L96326`, `rule.24.LLVMTy-Slice@L96342`, `rule.24.LLVMTy-Range@L96358`
- `rule.24.LLVMTy-RangeInclusive@L96374`, `rule.24.LLVMTy-RangeFrom@L96390`, `rule.24.LLVMTy-RangeTo@L96406`, `rule.24.LLVMTy-RangeToInclusive@L96422`, `rule.24.LLVMTy-RangeFull@L96438`, `rule.24.LLVMTy-Enum@L96453`, `rule.24.LLVMTy-Union-Niche@L96469`, `rule.24.LLVMTy-Union-Tagged@L96485`
- `rule.24.LLVMTy-Modal-Niche@L96501`, `rule.24.LLVMTy-Modal-Tagged@L96517`, `rule.24.LLVMTy-Modal-StringBytes@L96533`, `rule.24.LLVMTy-ModalState@L96551`, `rule.24.LLVMTy-Dynamic@L96567`, `rule.24.LLVMTy-StringView@L96583`, `rule.24.LLVMTy-StringManaged@L96599`, `rule.24.LLVMTy-BytesView@L96615`
- `rule.24.LLVMTy-BytesManaged@L96631`, `rule.24.LLVMTy-Err@L96647`, `def.24.LowerIRJudg@L96665`, `def.24.LLVMInstrHelpers@L96678`, `rule.24.LowerIRInstr-Empty@L96709`, `rule.24.LowerIRInstr-Seq@L96724`, `def.24.MemoryInstructionHelpers@L96740`, `def.24.ConstBytesEncoding@L96757`
- `def.24.StaticTypeBySymbol@L96783`, `def.24.StateRefJudg@L96797`, `rule.24.StateRef-Session@L96811`, `rule.24.StateRef-Global@L96827`, `def.24.CallSignatureHelpers@L96843`, `def.24.ParamInitHelpers@L96862`, `rule.24.LowerIRDecl-Proc-User@L96886`, `rule.24.LowerIRDecl-Proc-Gen@L96902`
- `rule.24.LowerIRDecl-GlobalConst@L96918`, `rule.24.LowerIRDecl-GlobalZero@L96934`, `req.24.HostedStateInitializerTemplates@L96950`, `rule.24.LowerIRDecl-VTable@L96963`, `rule.24.Lower-AllocIR@L96979`, `rule.24.Lower-BindVarIR@L96995`, `rule.24.Lower-ReadVarIR@L97011`, `rule.24.Lower-ReadVarIR-Err@L97027`
- `def.24.ProcSymbol@L97043`, `rule.24.Lower-ReadPathIR-Static-User@L97056`, `rule.24.Lower-ReadPathIR-Static-Gen@L97072`, `rule.24.Lower-ReadPathIR-Proc-User@L97088`, `rule.24.Lower-ReadPathIR-Proc-Gen@L97104`, `rule.24.Lower-ReadPathIR-Runtime@L97120`, `rule.24.Lower-ReadPathIR-Record@L97136`, `rule.24.Lower-StoreVarIR@L97152`
- `rule.24.Lower-StoreVarNoDropIR@L97168`, `rule.24.Lower-MoveStateIR@L97184`, `rule.24.Lower-StoreGlobal@L97200`, `rule.24.Lower-ReadPlaceIR@L97216`, `rule.24.Lower-WritePlaceIR@L97232`, `def.24.PtrType@L97248`, `rule.24.Lower-ReadPtrIR@L97261`, `rule.24.Lower-ReadPtrIR-Raw@L97277`
- `rule.24.Lower-ReadPtrIR-Null@L97293`, `rule.24.Lower-ReadPtrIR-Expired@L97309`, `rule.24.Lower-WritePtrIR@L97325`, `rule.24.Lower-WritePtrIR-Null@L97341`, `rule.24.Lower-WritePtrIR-Expired@L97357`, `rule.24.Lower-WritePtrIR-Raw@L97373`, `rule.24.Lower-WritePtrIR-Raw-Err@L97389`, `rule.24.Lower-AddrOfIR@L97405`
- `def.24.CallLoweringHelpers@L97421`, `rule.24.Lower-CallIR-Func@L97449`, `def.24.DynamicDispatchHelpers@L97465`, `rule.24.Lower-CallVTable@L97483`, `rule.24.LowerIRInstr-ClearPanic@L97499`, `rule.24.LowerIRInstr-PanicCheck@L97515`, `rule.24.LowerIRInstr-CheckPoison@L97531`, `rule.24.LowerIRInstr-LowerPanic@L97547`
- `def.24.IfLoweringHelpers@L97563`, `rule.24.Lower-IfIR@L97580`, `def.24.BlockCleanupLoweringHelpers@L97596`, `rule.24.Lower-BlockIR@L97612`, `def.24.StructuredIRLoweringForms@L97628`, `rule.24.Lower-LoopIR@L97656`, `rule.24.Lower-IfCaseIR@L97672`, `rule.24.Lower-RegionIR@L97688`
- `rule.24.Lower-FrameIR@L97704`, `def.24.BranchLowerForms@L97720`, `rule.24.Lower-BranchIR-Unconditional@L97734`, `rule.24.Lower-BranchIR-Conditional@L97750`, `def.24.PhiLowerForm@L97765`, `rule.24.Lower-PhiIR@L97778`, `rule.24.LowerIRDecl-Err@L97794`, `rule.24.LowerIRInstr-Err@L97810`
- `def.24.BindStorageJudg@L97828`, `def.24.BindRegionTarget@L97851`, `req.24.ResolveTargetNearestLiveAlias@L97870`, `rule.24.BindValid-Sigma@L97883`, `rule.24.BindSlot-Param-ByValue@L97899`, `rule.24.BindSlot-Param-ByRef@L97915`, `rule.24.BindSlot-Region@L97931`, `rule.24.BindSlot-Local@L97947`
- `rule.24.BindSlot-Static@L97963`, `rule.24.UpdateValid-BindVar@L97979`, `rule.24.UpdateValid-StoreVar@L97994`, `rule.24.UpdateValid-StoreVarNoDrop@L98009`, `rule.24.UpdateValid-MoveRoot@L98025`, `rule.24.UpdateValid-PartialMove-Init@L98041`, `rule.24.UpdateValid-PartialMove-Step@L98057`, `def.24.DropOnAssignHelpers@L98073`
- `rule.24.DropOnAssign-NotApplicable@L98089`, `rule.24.DropOnAssign-Record-Valid@L98105`, `rule.24.DropOnAssign-Record-Partial@L98121`, `rule.24.DropOnAssign-Record-Moved@L98137`, `rule.24.DropOnAssign-Aggregate-Ok@L98153`, `rule.24.DropOnAssign-Aggregate-Moved@L98169`, `rule.24.BindSlot-Err@L98185`, `rule.24.BindValid-Err@L98201`
- `rule.24.UpdateValid-Err@L98217`, `rule.24.DropOnAssign-Err@L98233`, `def.24.LLVMCallJudg@L98251`, `def.24.LLVMCallSigFields@L98264`, `rule.24.LLVMArgLower-ByValue-PtrValid@L98280`, `rule.24.LLVMArgLower-ByValue-Other@L98296`, `rule.24.LLVMArgLower-ByRef@L98312`, `rule.24.LLVMRetLower-ByValue-ZST@L98328`
- `rule.24.LLVMRetLower-ByValue@L98344`, `rule.24.LLVMRetLower-SRet@L98360`, `def.24.LLVMCallArgLists@L98376`, `rule.24.LLVMCall-ByValue@L98391`, `rule.24.LLVMCall-SRet@L98407`, `def.24.ByRefAccess@L98423`, `rule.24.LLVMArgLower-Err@L98438`, `rule.24.LLVMRetLower-Err@L98454`
- `rule.24.LLVMCall-Err@L98470`, `def.24.VTableJudg@L98488`, `def.24.VTableEmissionHelpers@L98501`, `rule.24.EmitDropGlue-Decl@L98525`, `rule.24.EmitVTable-Err@L98541`, `def.24.LiteralEmitJudg@L98559`, `def.24.StringBytesAndRawBytes@L98572`, `rule.24.EmitLiteralData-Decl@L98595`
- `rule.24.EmitLiteral-String@L98611`, `req.24.EmitLiteral-String-Utf8Valid@L98627`, `rule.24.EmitLiteral-Bytes@L98640`, `req.24.EmitLiteral-Bytes-UndefinedRawBytes@L98656`, `rule.24.EmitLiteral-Char@L98669`, `rule.24.EmitLiteral-Int@L98685`, `rule.24.EmitLiteral-Float@L98701`, `rule.24.EmitLiteral-Err@L98717`
- `def.24.PoisonJudg@L98735`, `def.24.PoisonSet@L98748`, `rule.24.PoisonFlag-Decl@L98761`, `def.24.PoisonFlagStorage@L98776`, `req.24.HostedPoisonFlagTemplate@L98790`, `rule.24.CheckPoison-Use@L98803`, `sem.24.CheckPoisonBehavior@L98819`, `req.24.HostedPoisonStateIsolation@L98832`
- `rule.24.SetPoison-OnInitFail@L98845`, `rule.24.PoisonFlag-Err@L98861`, `rule.24.CheckPoison-Err@L98877`, `rule.24.SetPoison-Err@L98893`, `req.24.OutputBackendDiagnosticsOwnership@L98911`, `diag.24.OutputBackendDiagnostics@L98924`

### Lowering, Backend, Runtime Interface, And Driver

#### `backend.llvm-target`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L5343-L5373.

- `def.LLVMTargetConstants@L5343`, `def.IsRootModule@L5359`, `def.WithEntry@L5373`

#### `backend.llvm-codegen`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L5389-L6747.

- `CodegenObj-LLVM@L5389`, `CodegenIR-LLVM@L5407`, `AssembleIR-Ok@L6729`, `AssembleIR-Err@L6747`

#### `lowering.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27099-L27308.

- `conformance.AttributeLoweringOwnership@L27101`, `conformance.VendorAttributeLowering@L27310`
- `conformance.AttributeLoweringOwnership@L27101`, `conformance.VendorAttributeLowering@L27310`

#### `lowering.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27595-L27595.

- `conformance.LayoutAttributeLowering@L27597`
- `conformance.LayoutAttributeLowering@L27597`

#### `lowering.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27740-L27740.

- `conformance.OptimizationAttributeLowering@L27742`
- `conformance.OptimizationAttributeLowering@L27742`

#### `lowering.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28221-L28221.

- `conformance.DiagnosticsMetadataLowering@L28223`
- `conformance.DiagnosticsMetadataLowering@L28223`

#### `lowering.permissions`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L28471-L28686.

- `conformance.PermissionLayoutNeutrality@L28758`, `req.PermissionFormsLoweringDiagnostics@L28820`, `conformance.AliasExclusivityLowering@L28973`
- `conformance.PermissionLayoutNeutrality@L28758`, `req.PermissionFormsLoweringDiagnostics@L28820`, `conformance.AliasExclusivityLowering@L28973`

#### `codegen`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L29113-L40918.

- `conformance.PermissionAdmissibilityLowering@L29400`, `conformance.ImportDeclarationLowering@L29635`, `conformance.UsingDeclarationLowering@L30142`, `def.ConstInitJudgementSet@L30435`, `def.ConstInitLiteralEncoding@L30451`, `def.StaticName@L30465`, `def.StaticBindingFunctionSignature@L30509`, `def.StaticSym@L30523`
- `def.InitSym@L30593`, `def.DeinitSym@L30625`, `def.StaticSymPath@L30685`, `def.StaticAddr@L30699`, `def.AddrOfSym@L30727`, `def.SeqIRList@L30769`, `def.StaticStoreIR@L30784`, `def.Rev@L30888`
- `conformance.ExternBlockLowering@L31333`, `conformance.ModuleAggregationEagerGraphLoweringInput@L34530`, `conformance.ModuleAggregationLifecycleLoweringOwnership@L34546`, `def.PrimitiveValueBits@L34877`, `req.PrimitiveLayoutAbiOwnership@L34897`, `def.TupleFields@L35321`, `def.TupleLayoutJudgementSet@L35632`, `def.TupleValueBits@L35737`
- `def.ArrayLen@L36426`, `def.ArrayValueBits@L36440`, `def.SliceValueBits@L36995`, `def.LoweringChecksJudgementSet@L37657`, `def.RangeValueBits@L37673`, `def.RecordLayoutHelpers@L38833`, `def.FieldOffset@L38945`, `def.FieldValueList@L38959`
- `def.StructBits@L38973`, `def.PadBytes@L38987`, `def.RecordValueBits@L39001`, `def.EnumLayoutHelpers@L40011`, `def.EnumPayloadBits@L40112`, `def.EnumValueBits@L40128`, `def.UnionNicheOrderingHelpers@L40481`, `def.UnionTypeOrderingKeys@L40501`
- `def.TypeKey@L40543`, `def.TypeKeyOrdering@L40576`, `def.UnionMemberLayoutSelection@L40598`, `def.UnionLayoutHelpers@L40618`, `def.UnionNicheBits@L40727`, `def.UnionPayloadBits@L40741`, `def.TaggedBits@L40755`, `def.UnionTaggedBits@L40771`
- `def.UnionBits@L40785`, `def.UnionValueBits@L40799`, `def.TypeAliasValueBits@L41205`
- `conformance.PermissionAdmissibilityLowering@L29400`, `conformance.ImportDeclarationLowering@L29635`, `conformance.UsingDeclarationLowering@L30142`, `def.ConstInitJudgementSet@L30435`, `def.ConstInitLiteralEncoding@L30451`, `def.StaticName@L30465`, `def.StaticBindingFunctionSignature@L30509`, `def.StaticSym@L30523`
- `def.InitSym@L30593`, `def.DeinitSym@L30625`, `def.StaticSymPath@L30685`, `def.StaticAddr@L30699`, `def.AddrOfSym@L30727`, `def.SeqIRList@L30769`, `def.StaticStoreIR@L30784`, `def.Rev@L30888`
- `conformance.ExternBlockLowering@L31333`, `conformance.ModuleAggregationEagerGraphLoweringInput@L34530`, `conformance.ModuleAggregationLifecycleLoweringOwnership@L34546`, `def.PrimitiveValueBits@L34877`, `req.PrimitiveLayoutAbiOwnership@L34897`, `def.TupleFields@L35321`, `def.TupleLayoutJudgementSet@L35632`, `def.TupleValueBits@L35737`
- `def.ArrayLen@L36426`, `def.ArrayValueBits@L36440`, `def.SliceValueBits@L36995`, `def.LoweringChecksJudgementSet@L37657`, `def.RangeValueBits@L37673`, `def.RecordLayoutHelpers@L38833`, `def.FieldOffset@L38945`, `def.FieldValueList@L38959`
- `def.StructBits@L38973`, `def.PadBytes@L38987`, `def.RecordValueBits@L39001`, `def.EnumLayoutHelpers@L40011`, `def.EnumPayloadBits@L40112`, `def.EnumValueBits@L40128`, `def.UnionNicheOrderingHelpers@L40481`, `def.UnionTypeOrderingKeys@L40501`
- `def.TypeKey@L40543`, `def.TypeKeyOrdering@L40576`, `def.UnionMemberLayoutSelection@L40598`, `def.UnionLayoutHelpers@L40618`, `def.UnionNicheBits@L40727`, `def.UnionPayloadBits@L40741`, `def.TaggedBits@L40755`, `def.UnionTaggedBits@L40771`
- `def.UnionBits@L40785`, `def.UnionValueBits@L40799`, `def.TypeAliasValueBits@L41205`
