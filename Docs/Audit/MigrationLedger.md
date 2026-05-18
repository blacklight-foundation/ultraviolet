# Ultraviolet Migration Ledger

This ledger records accepted migration evidence for each audited source object.

## Entry Format

Every accepted migration entry must use this format:

- Source object: original C++ path, primary symbols, and current call sites.
- Target object: Ultraviolet module path, file path, and owned responsibility.
- Specification basis: exact `SPECIFICATION.md` sections read and obligation IDs from Appendix A of `CompilerRebuildPlan.md`.
- Implementation summary: canonical behavior implemented and old behavior reconciled.
- Tests: source-native test files, `[[test]]` names, covered obligation IDs, and diagnostic cases.
- Verification commands: exact command lines, target profile, assembly, and summarized output.
- Bootstrap notes: Cursive bootstrap issue found, patch reference, or `none`.
- Non-applicability notes: obligation ID, reason, spec citation, and reviewer approval.
- Acceptance: reviewer, date, and status.

## Entries

### Cursive Ultraviolet Project Support

- Source object: Cursive compiler project/driver support; primary paths
  `cursive/include/01_project/language_profile.h`,
  `cursive/src/01_project/language_profile.cpp`,
  `cursive/src/06_driver/compiler_main.cpp`, and
  `cursive/src/06_driver/cli.cpp`; call sites are normal Cursive check/build
  commands.
- Target object: Cursive bootstrap compiler support for the Ultraviolet repo;
  normal source-language detection from `.uv` file inputs or `Ultraviolet.toml`
  project roots.
- Specification basis: `req.ProjectRootResolveInputPath@L2281`,
  `req.ProjectRootFileInputStartsAtParent@L2296`,
  `req.ProjectRootDirectoryInputStartsAtResolvedPath@L2310`,
  `def.FindProjectRoot@L2324`, `WF-Project-Root@L3491`,
  `Module-Dir@L4306`, `def.CompilationUnits@L4338`,
  `Modules-Ok@L4384`, `def.TargetProfile@L1171`,
  `req.TargetProfileResolution@L1186`, `req.TargetProfileNoHostInference@L1204`,
  `def.SelectedTargetProfile@L3061`, `Select-By-Name@L3887`.
- Implementation summary: Cursive now selects the active language profile from
  the input path or nearest manifest and uses the existing Ultraviolet profile for
  `Ultraviolet.toml`, `.uv`, Ultraviolet runtime names, and Ultraviolet runtime
  library names. The special bootstrap CLI flag was removed from parsing, help,
  and suggestion lists.
- Tests: source-native tests are not introduced in this entry; verification uses the
  normal Cursive command path against the Ultraviolet scaffold and assembly
  selection checks.
- Verification commands: `Cursive.exe --target-profile x86_64-win64 --check
  C:\Dev\Ultraviolet --assembly UltravioletCompiler` succeeded, loading
  Ultraviolet and checking 39 compiler modules; `Cursive.exe --target-profile
  x86_64-win64 --check C:\Dev\Ultraviolet --assembly UltravioletRT` succeeded,
  loading Ultraviolet and checking 11 runtime modules; `Cursive.exe
  --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly uv`
  loaded and parsed the `uv` assembly, then reported `E-MOD-2434` because the
  scaffold entry file has no `main` procedure yet.
- Bootstrap notes: Cursive project support patch built with Visual Studio
  Release target `cursive`; generated registry, diagnostic sync, include
  direction, and AST coverage checks passed during the build.
- Non-applicability notes: final artifact, linker, archive, and fixed-point
  verification are owned by later work after source files contain compiler
  behavior.
- Acceptance: pending user review, 2026-05-05, implemented with fresh command
  output.

### Source-Native Test Attribute Surface

- Source object: Cursive bootstrap compiler attribute and procedure checking;
  primary paths `cursive/src/02_source/attributes/attribute_registry.cpp`,
  `cursive/include/02_source/attributes/attribute_registry.h`,
  `cursive/include/02_source/ast/nodes/ast_items.h`,
  `cursive/src/02_source/parser/item/parse_item.cpp`,
  `cursive/src/02_source/parser/item/procedure_decl.cpp`,
  `cursive/src/04_analysis/typing/item/procedure_decl.cpp`,
  `docs/CursiveSpecification.md`, and generated diagnostic registry files.
- Target object: Ultraviolet source-native conformance testing surface in
  `SPECIFICATION.md`, `Docs/Internal/UltravioletSpecification.obligations.md`,
  `Docs/Audit/UltravioletObligations.csv`, `CompilerRebuildPlan.md`, and
  `Compiler/Tests/Parser/AttributeTests.uv`.
- Specification basis: `def.SpecAttributeRegistry@L26957`,
  `def.SpecAttributeTargets@L26980`,
  `def.AttributeStaticSemantics.Helpers2@L27084`,
  `grammar.TestAttribute@L28336`,
  `parse.TestAttributeByOrdinaryAttributeParser@L28356`,
  `ast.TestProcedureClassification@L28374`, `def.TestName@L28389`,
  `def.TestCoverage@L28404`, `req.TestAttributeProcedureTarget@L28421`,
  `def.TestAttributeArgsOk@L28435`, `req.TestProcedureShape@L28455`,
  `req.TestAuthority@L28477`,
  `conformance.TestAttributeDynamicSemantics@L28495`,
  `lowering.TestHarnessGeneration@L28516`,
  `def.TestDiscoveryOrder@L28575`, and
  `diagnostics.TestAttributes@L28593`.
- Implementation summary: Ultraviolet now specifies `[[test]]` as a
  procedure-target attribute with optional `name: "..."` and `covers("id@Lline")`
  metadata, ordinary contract-based pass/fail semantics, deterministic discovery,
  ephemeral harness lowering, and concrete diagnostics. Cursive recognizes the
  attribute, validates its argument shape, validates represented test procedure
  shape, and resolves the new diagnostics through the generated registry.
- Tests: `Compiler/Tests/Parser/AttributeTests.uv` adds
  `testAttributeAcceptsSourceNativeContract`, covering
  `grammar.TestAttribute@L28336` and `req.TestProcedureShape@L28455`.
- Verification commands: `python3 Tools/ExtractObligationLedger.py --check`
  passed with 5966 obligations; `python3 cursive/tools/generate_diagnostic_registry.py
  --check && python3 cursive/tools/validate_diagnostic_spec_sync.py` passed in
  the Cursive repo; Windows Release build target `cursive` succeeded; `Cursive.exe
  --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
  UltravioletCompiler` succeeded with 39 parsed modules and zero diagnostics.
- Bootstrap notes: Cursive now preserves explicit visibility spelling for
  ordinary procedures so the bootstrap checker can enforce the
  explicit-visibility portion of `req.TestProcedureShape@L28455`.
- Non-applicability notes: `uv test` execution, coverage report emission, unknown
  coverage-reference validation against the ledger, and harness invocation are
  owned by the future Ultraviolet `Compiler/Driver/CLI/TestCommand.uv` and
  `Compiler/Driver/Testing/Test*.uv` migration slices.
- Acceptance: pending user review, 2026-05-05, implemented with fresh command
  output.

### Positional `uv test` Target Surface

- Source object: Ultraviolet source-native test command design in
  `SPECIFICATION.md` §9.6.6 and `CompilerRebuildPlan.md`; no C++ bootstrap
  implementation object applies because the Cursive bootstrap compiler does not
  own the future `uv test` CLI surface.
- Target object: `Compiler/Driver/CLI/TestCommand.uv`,
  `Compiler/Driver/Testing/TestDiscovery.uv`,
  `Compiler/Driver/Testing/TestHarness.uv`,
  `Compiler/Driver/Testing/TestExecution.uv`,
  `Compiler/Driver/Testing/TestResults.uv`, and
  `Compiler/Driver/Testing/TestCoverage.uv` implementation contract updated to
  use positional test targets.
- Specification basis: `lowering.TestHarnessGeneration@L28516`,
  `def.TestDiscoveryOrder@L28575`, `WF-Assembly@L3511`,
  `Select-By-Name@L3887`, `Module-Dir@L4306`, and
  `def.FindProjectRoot@L2324`.
- Implementation summary: the normative test command surface is now
  `uv test [target]`. The target resolves as absent target, assembly name,
  module path, source file path, or directory path. The rebuild plan no longer
  requires a test-specific assembly or project-root flag.
- Tests: no source-native test body changed in this entry; this entry corrects the
  driver command contract that future `Compiler/Driver/CLI/TestCommand.uv` and
  `Compiler/Driver/Test*.uv` tests must exercise.
- Verification commands: `python3 Tools/ExtractObligationLedger.py --check`
  passed with 5966 obligations; targeted Ultraviolet and Cursive `git diff
  --check` passed; `python3 cursive/tools/generate_diagnostic_registry.py
  --check && python3 cursive/tools/validate_diagnostic_spec_sync.py` passed in
  the Cursive repo; Windows Release build target `cursive` succeeded; `Cursive.exe
  --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
  UltravioletCompiler` succeeded with 39 parsed modules and zero diagnostics.
- Bootstrap notes: none.
- Non-applicability notes: harness execution and result rendering remain owned
  by the next `Compiler/Driver/Test*.uv` implementation slice.
- Acceptance: pending user review, 2026-05-05, implemented with fresh command
  output.

### Core Path Resolution Surface

- Source object: Cursive bootstrap path-resolution implementation; primary paths
  `cursive/include/00_core/path.h`, `cursive/src/00_core/path.cpp`, and
  `cursive/src/01_project/project_validate.cpp`; current call sites include
  project validation, source-root loading, module discovery, deterministic order,
  compile-time files, output paths, and conformance trace path handling.
- Target object: `UltravioletCompiler::Core::Path` in
  `Compiler/Core/Path/{Path,Text,Roots,Components,ComponentLists,Storage,Algebra,Resolution}.uv`;
  tests live in `Compiler/Tests/Core/Path`.
- Specification basis: `def.PathRootPredicates@L3146`,
  `def.PathRootTagAndTail@L3165`, `def.PathSegments@L3188`,
  `def.PathComponents@L3202`, `def.JoinComp@L3218`, `def.Join@L3237`,
  `def.AbsPath@L3253`, `def.PathFunctionTypes@L3268`,
  `def.PathPrefix@L3284`, `def.Normalize@L3298`, `def.Under@L3312`,
  `def.Canon@L3326`, `def.Drop@L3341`,
  `def.RelativePathComputation@L3355`, `def.Basename@L3369`,
  `def.FileExt@L3386`, `Resolve-Canonical@L3405`,
  `Resolve-Canonical-Err@L3423`, `WF-RelPath@L3441`, and
  `WF-RelPath-Err@L3459`.
- Implementation summary: Path values now use materialized region-backed
  component lists, explicit backing-storage modal states, component views with
  slice-bound contracts, canonical root classification, component-based algebra,
  validated canonical/relative/resolved path records, canonical resolution, and
  relative-path well-formedness result states. Component-list traversal is
  iterative. The old lazy component-sequence representation was removed and its
  responsibilities were reconciled into `ComponentLists.uv`, `Storage.uv`,
  `Algebra.uv`, and `Resolution.uv`.
- Tests: `Compiler/Tests/Core/Path/*Tests.uv` covers every path-resolution
  obligation. This pass added `ResolveCanonicalTests.uv` and `WFRelPathTests.uv`
  for `Resolve-Canonical@L3405`, `Resolve-Canonical-Err@L3423`,
  `WF-RelPath@L3441`, and `WF-RelPath-Err@L3459`, and extended
  `Expectations.uv` with state-specific assertions for resolution and
  well-formedness results.
- Verification commands: `python3 cursive/tools/generate_diagnostic_registry.py
  --repo-root /mnt/c/dev/cursive --spec-path /mnt/c/dev/cursive/docs/CursiveSpecification.md
  --output-registry-path /mnt/c/dev/cursive/cursive/src/00_core/generated/diag_registry.inc
  --output-typecheck-map-path /mnt/c/dev/cursive/cursive/src/04_analysis/typing/item/typecheck_diag_map.inc
  --check` passed with 493 rows and 512 map entries; Windows Release build
  target `cursive` succeeded and ran diagnostic sync, static-rule registry sync,
  include-direction, and AST coverage validation; `Cursive.exe
  --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
  UltravioletCompiler` succeeded with 42 compiler modules and zero diagnostics.
- Bootstrap notes: patched Cursive diagnostic registry generation so
  `ValueUse-NonBitcopyPlace` maps to `E-UNS-0107`, matching the expression
  diagnostics table instead of reaching an internal unknown-diagnostic path.
- Non-applicability notes: source-native `[[test]]` execution remains owned by
  the future `uv test` driver and harness implementation; this pass verifies the
  tests by parsing and typechecking them through the bootstrap compiler.
- Acceptance: complete for the executable command surface, 2026-05-06, with
  fresh bootstrap semantic-check evidence. Runtime `uv test` execution remains
  owned by the source-native test driver completion entry.

### Executable Command Surface

- Source object: `uv` executable entrypoint and driver CLI command surface.
- Target object: `Tools/Uv/Main.uv`,
  `Compiler/Driver/CLI/{Api,BuildCommand,CheckCommand,CleanCommand,Commands,InitCommand,OptionValues,Options,Positionals,RunCommand,TestCommand,Text,Version}.uv`
  `Compiler/Driver/Pipeline.uv`, and `Compiler/Project/OutputArtifacts.uv`.
- Specification basis: `def.15.MainEntryPointDefinitions@L52806`,
  `rule.15.Main-Ok@L52827`, and the positional `uv test` command shape in
  `lowering.TestHarnessGeneration@L28516`.
- Implementation summary: `Tools/Uv/Main.uv` now defines the executable
  `public procedure main(context: Context) -> i32`. The driver CLI source now
  contains a narrow `DriverHost` authority projection record, a state-specific
  `Command` modal for `build`, `check`, `clean`, `init`, `run`, `test`, and
  `version`, command-specific entry procedures, typed pipeline request
  submission, modal-owned result conversion, command failure reason states, a
  `CommandResult` modal, command argument records, positional target states for
  `uv test [target]`, and version output through the `FileSystem.write_stdout`
  authority. `Compiler/Driver/Pipeline.uv` owns the driver-level
  `PipelineCommand`, `PipelineTargetProfile`, `PipelineOutputMode`,
  `PipelineRequest`, `PipelineSubmission`, and `PipelineUnavailableReason`
  boundary. CLI option parsing converts command-line target-profile and
  output-mode input into those driver-owned pipeline values before project
  command submission. Project command-output obligations are assigned to
  `Compiler/Project/OutputArtifacts.uv` and are implemented with the
  project/output artifact migration slice.
- Tests: bootstrap semantic checks exercise the real `main` declaration,
  command modal, command-result modal, command-specific entry procedures,
  pipeline request construction, driver-owned target-profile and output-mode
  request fields, typed pipeline unavailable states, positional target
  construction, target-profile and output-mode input conversion, and version
  output result classification through the `uv` assembly.
- Verification commands:
  - `python3 Tools/ExtractObligationLedger.py --check` passed with 5977
    obligations.
  - `git diff --check` passed.
  - `/mnt/c/dev/cursive/cursive/build/windows/Release/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly uv`
    passed with 47 resolved modules and zero diagnostics.
  - `/mnt/c/dev/cursive/cursive/build/windows/Release/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly UltravioletCompiler`
    passed with 45 resolved modules and zero diagnostics.
  - `/mnt/c/dev/cursive/cursive/build/windows/Release/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly UltravioletRT`
    passed with 11 resolved modules and zero diagnostics.
- Bootstrap notes: the command surface depends on the approved normalized
  `System` process invocation methods recorded in the following ledger entry.
- Acceptance: complete for the executable command surface, 2026-05-06, with
  fresh bootstrap semantic-check evidence. Runtime `uv test` execution remains
  owned by the source-native test driver completion entry.

### Executable Process Invocation Surface

- Source object: approved Ultraviolet `System` process invocation surface used
  by the `uv` executable command parser.
- Target object: `SPECIFICATION.md`,
  `Docs/Internal/UltravioletSpecification.obligations.md`,
  `Docs/Audit/{UltravioletObligations.csv,FileObligationMap.csv}`,
  `Compiler/Driver/CLI/{Api,Commands,OptionValues,Options,Positionals,TestCommand,Text,Version}.uv`,
  `Tools/Uv/Main.uv`, `Compiler/Semantics/Capabilities/SystemCapabilities.uv`,
  `Compiler/Backend/IR/RuntimeSymbols.uv`, `Runtime/Host/{Startup,Platform}.uv`,
  and `Runtime/System/{Environment,Process}.uv`.
- Specification basis: `def.14.SystemDecl@L51462`,
  `Prim-System-ExecutablePath@L14353`,
  `Prim-System-ArgumentCount@L14371`, `Prim-System-Argument@L14389`,
  `Prim-System-CurrentDirectory@L14407`,
  `def.24.ProcessInvocationNormalization@L93519`,
  `req.24.ProcessInvocationPlatformIsolation@L93538`, and
  `lowering.TestHarnessGeneration@L28516`.
- Implementation summary: `System` now has an explicit normalized invocation
  surface in the Ultraviolet specification. The CLI entrypoint delegates to the
  driver, `DriverHost` projects executable path and current directory from
  `Context.sys`, and command arguments are parsed from `System.argument_count()`
  and `System.argument(index)`.
- Tests: added source-native CLI command-input tests for absent `uv test`
  target, one target, extra target rejection, empty-command defaulting, unknown
  command rejection, target-profile input parsing, unsupported target-profile
  preservation, quiet output mode, and machine-readable output mode.
- Verification commands:
  - `python3 Tools/ExtractObligationLedger.py --check` passed with 5977
    obligations.
  - `git diff --check` passed.
  - `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass -File \\wsl.localhost\Ubuntu\home\crow\.codex\skills\vsdev-cmake-build\scripts\run_vsdev_cmake_build.ps1 -RepoRoot C:\dev\cursive -SourceDir C:\dev\cursive\cursive -BuildDir C:\dev\cursive\cursive\build\windows -Config Release -Target cursive`
    passed and produced `Cursive.exe`.
  - `/mnt/c/dev/cursive/cursive/build/windows/Release/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly uv`
    passed with 47 resolved modules and zero diagnostics.
  - `/mnt/c/dev/cursive/cursive/build/windows/Release/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly UltravioletCompiler`
    passed with 45 resolved modules and zero diagnostics.
  - `/mnt/c/dev/cursive/cursive/build/windows/Release/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly UltravioletRT`
    passed with 11 resolved modules and zero diagnostics.
- Bootstrap notes: updated the Cursive bootstrap specification, `System`
  method lookup, runtime symbol declarations, intrinsic runtime signatures,
  runtime ABI required-symbol list, and hosted runtime process/platform
  implementation so the bootstrap accepts and can lower the approved
  `System.executable_path`, `System.argument_count`, `System.argument`, and
  `System.current_directory` methods.
- Acceptance: pending user review, 2026-05-06, implemented with fresh command
  output.

### Source-Native Test Driver Completion

- Source object: Ultraviolet `uv test` execution surface defined by
  `SPECIFICATION.md` §9.6.5 through §9.6.7 and the driver responsibility map in
  `CompilerRebuildPlan.md`.
- Target object: `Tools/Uv/Main.uv`,
  `Compiler/Driver/CLI/{Api,Commands,Options,TestCommand,Version}.uv`,
  `Compiler/Driver/Pipeline.uv`,
  `Compiler/Driver/Testing/{TestDiscovery,TestCoverage,TestHarness,TestExecution,TestResults}.uv`,
  and the project, source, semantic, lowering, runtime, and backend files needed
  to execute source-native tests through the ordinary compiler path.
- Specification basis: `conformance.TestAttributeDynamicSemantics@L28495`,
  `lowering.TestHarnessGeneration@L28516`, `def.TestDiscoveryOrder@L28575`,
  `WF-Assembly@L3511`, `Select-By-Name@L3887`, `Module-Dir@L4306`,
  `def.FindProjectRoot@L2324`, and `diagnostics.TestAttributes@L28593`.
- Implementation summary: the `uv` executable entrypoint and positional
  `uv test` command coordination are implemented through the CLI command
  surface. `Compiler/Driver/Testing` now defines source-native target
  candidate and scope states, deterministic discovered-test records and
  ordering, ordered discovered-test list construction, coverage-reference
  validation data, harness generation lifecycle states, test execution lifecycle
  states, and result summary records. The Project module now defines the
  manifest, validation, root-discovery, module-discovery, assembly-selection,
  deterministic-ordering, build-config, toolchain-config, assembly-graph, and
  project-load lifecycle surfaces needed by test target selection. Project
  loading separates manifest text parsing, parse validation,
  discovered-assembly completion, and module-discovery assembly construction.
  Source-native execution planning sorts selected tests before creating the
  planned test run.
- Tests: each source-native testing obligation from `grammar.TestAttribute`
  through `diagnostics.TestAttributes` has exactly one dedicated owning test
  file under the parent assembly `Tests` subtree. The project-loading and
  deterministic-ordering obligations currently implemented in this slice also
  have dedicated owning test files under `Compiler/Tests/Project`. The manifest
  parser tests cover `def.ManifestParsing@L2168`, `Parse-Manifest-Ok@L2183`,
  and `Parse-Manifest-Err@L2219` with the accepted Ultraviolet assembly-table
  manifest shape, comment and ignored-table traversal, malformed key-value
  syntax, and duplicate-field parse failure. The project-loader parse tests
  cover `Step-Parse@L3558` and `Step-Parse-Err@L3576` through the explicit
  `ManifestParsed` and `Failed` loader states. The validation-transition tests
  cover `Step-Validate@L3594` and `Step-Validate-Err@L3612` through the
  `ManifestValidated` and `Failed` loader states. Manifest validation tests now
  cover top-level key validation, assembly key validation, required assembly
  fields, assembly name validity, assembly kind validity, relative root and
  output-directory path validity, `emit_ir` validity, and `link_kind` validity.
  Assembly-selection tests cover absent sole assembly selection, absent sole
  executable selection, named selection, and ambiguous absent-target selection.
  Root-discovery tests cover command-input resolution gating, file-input parent
  search starts, directory-input resolved search starts, nearest-manifest root
  selection, search-start fallback, and project-root well-formedness.
  Deterministic-ordering tests cover folded path comparison, file key
  tiebreaking, file ordering, fold comparison, directory key tiebreaking, and
  directory ordering. Module-discovery tests cover source-root directory
  inclusion, source-root well-formedness, source-root failure diagnostics,
  module-directory classification, `.uv` source filtering, compilation-unit
  ordering, relative derivation failure, module-path consumption from module
  discovery rules, successful module discovery, and failed module discovery.
  Assembly-ownership tests cover source-root projection, deepest unique owner
  root selection, owned-module filtering, successful ownership completion, and
  ownership failure diagnostics for modules with no unique source-root owner.
- Verification commands:
  - `python3 Tools/ExtractObligationLedger.py --check` passed with 5977
    obligations.
  - `git diff --check` passed.
  - `/mnt/c/dev/cursive/cursive/build/out/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly UltravioletCompiler`
    passed with 110 parsed compiler modules and zero diagnostics.
  - `/mnt/c/dev/cursive/cursive/build/out/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly UltravioletRT`
    passed with 11 resolved modules and zero diagnostics.
  - `/mnt/c/dev/cursive/cursive/build/out/Cursive.exe --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly uv`
    passed with 112 resolved modules and zero diagnostics.
  - local source-native obligation scan passed with 95 obligations assigned to
    exactly one owning test file each.
- Bootstrap notes: repaired Cursive bootstrap modal-state subtyping,
  modal-state record-literal path typing, record-literal diagnostic detail
  propagation, and unique moved-value checking so spec-valid transition bodies
  in the source-native driver check through the canonical semantic path.
- Remaining implementation work: host filesystem traversal for module
  discovery, source metadata extraction, harness source emission, backend
  artifact generation, and harness process execution are owned by subsequent
  project, source, driver, and backend implementation slices.
- Acceptance: pending harness execution connection and review.

### Bootstrap Pattern Narrowing Trace

- Source object: Cursive bootstrap compiler if-case modal-pattern narrowing;
  primary path `LLVMBootstrap/cursive/src/05_codegen/lower/expr/if_case_expr.cpp`
  with regression coverage in
  `LLVMBootstrap/cursive/src/tests/codegen_diagnostic_path_test.cpp`.
- Target object: bootstrap codegen conformance tracing for modal union
  if-case lowering.
- Specification basis: `def.17.CaseScopeJudgements@L66375`,
  `def.17.UnionOrSingle@L66404`,
  `rule.17.PatternNarrow-ModalState@L66450`,
  `rule.17.PatternNarrow-Union@L66466`, and
  `rule.17.CaseScope-Narrow@L66482`.
- Implementation summary: codegen now records `PatternNarrow-Union` whenever
  modal-pattern narrowing over a union produces at least one matched member,
  including the single-member result case. `Docs/Audit/FileObligationMap.csv`
  was refreshed to match the current 5,986-row obligation ledger and to assign
  the new case-scope narrowing obligations to
  `Compiler/Semantics/Patterns/PatternModel.uv`.
- Tests: `cursive_codegen_diagnostic_path_test` now compiles its fixture with
  `--conformance diagnostic_path.conformance.log` and fails if the codegen
  phase omits `PatternNarrow-Union` for modal union if-case lowering.
- Verification commands:
  - `python3 Tools/ExtractObligationLedger.py --check` passed with 5,986
    obligations.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after.json --strict
    --check` passed with 3,131 rules.
  - `python3 LLVMBootstrap/cursive/tools/validate_ast_phase_coverage.py
    --source-root LLVMBootstrap/cursive` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_diagnostic_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md
    --output-registry-path
    LLVMBootstrap/cursive/src/00_core/generated/diag_registry.inc
    --output-typecheck-map-path
    LLVMBootstrap/cursive/src/04_analysis/typing/item/typecheck_diag_map.inc
    --check && python3
    LLVMBootstrap/cursive/tools/validate_diagnostic_spec_sync.py --repo-root
    LLVMBootstrap/cursive --source-root LLVMBootstrap/cursive/src --spec-path
    SPECIFICATION.md --registry-path
    LLVMBootstrap/cursive/src/00_core/generated/diag_registry.inc` passed.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_codegen_diagnostic_path_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_codegen_diagnostic_path_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the fixed probe emits two typecheck-phase and two
  codegen-phase `PatternNarrow-Union` records for the modal union narrowing
  fixture.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Diagnostic And Manifest Validation Trace

- Source object: Cursive bootstrap diagnostic id emission and project manifest
  validation; primary paths
  `LLVMBootstrap/cursive/src/00_core/diagnostic_messages.cpp`,
  `LLVMBootstrap/cursive/src/01_project/project_validate.cpp`,
  `LLVMBootstrap/cursive/src/tests/lexer_diagnostics_test.cpp`,
  `LLVMBootstrap/cursive/src/tests/project_manifest_validation_test.cpp`, and
  `LLVMBootstrap/cursive/src/CMakeLists.txt`.
- Target object: required trace coverage for diagnostic-id code selection and
  accepted optional assembly manifest field typing.
- Obligation basis: `DiagId-Code@L1722`,
  `WF-Assembly-EmitIRType@L2856`, and
  `WF-Assembly-LinkKindType@L2891` from
  `Docs/Audit/UltravioletObligations.csv`. `DiagId-Code` records the
  diagnostic id to code-bearing diagnostic boundary. The two manifest labels
  record the accepted `emit_ir` and `link_kind` omitted-or-string judgments.
- Implementation summary: `MakeDiagnosticById` now records `DiagId-Code` after
  a diagnostic id resolves to a code and before the diagnostic is built.
  `ValidateManifest` now records `WF-Assembly-EmitIRType` and
  `WF-Assembly-LinkKindType` after each optional field passes its existing type
  check. `cursive_lexer_diagnostics_test` asserts the diagnostic trace, and the
  new `cursive_project_manifest_validation_test` directly calls
  `ValidateManifest` with accepted optional field values and asserts both
  project-system conformance records.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_project_optional_types.json
    --strict` passed with 3,250 rules.
  - `cmd.exe /c cmake --build --preset windows-release --target
    cursive_project_manifest_validation_test` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_project_manifest_validation_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_lexer_diagnostics_test.exe`
    passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`
    passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --check --strict` passed with 3,250 rules.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:/Dev/Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
  - `git -c filter.lfs.process= -c filter.lfs.smudge= -c filter.lfs.clean=
    -c filter.lfs.required=false diff --check -- <touched files>` passed
    with existing CRLF normalization warnings on Windows-source files.
- Bootstrap notes: the normalized formal-rule comparison now reports
  `required_formal_rules=2940`, `trace_labels=3250`, `missing=962`,
  `missing_diagnostics.code-selection=0`, and
  `missing_project.manifest-validation=0`. The global missing count dropped
  from 965 to 962 in this slice.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Leading-Zero Literal Trace

- Source object: Cursive bootstrap lexer integer literal diagnostics; primary
  path `LLVMBootstrap/cursive/src/02_source/lexer/lexer_literals.cpp` with
  regression coverage in
  `LLVMBootstrap/cursive/src/tests/lexer_diagnostics_test.cpp`.
- Target object: bootstrap parse-phase conformance tracing for decimal integer
  literals that emit the leading-zero warning.
- Specification basis: `def.DecimalLeadingZeros@L9117`,
  `Warn-DecimalLeadingZero@L9133`, and diagnostic `W-SRC-0301`.
- Implementation summary: the lexer now records `Warn-DecimalLeadingZero`
  when decimal integer literal scanning emits `W-SRC-0301`. The generated
  static rule registry was refreshed and now contains 3,132 mapped rules.
- Tests: `cursive_lexer_diagnostics_test` builds a fixture containing
  `return 001`, runs the rebuilt compiler with
  `--conformance lexer_diagnostics.conformance.log`, and fails if the compile
  output omits `warning[W-SRC-0301]` or the conformance log omits the
  parse-phase `Warn-DecimalLeadingZero` record.
- Verification commands:
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_lexer_diagnostics_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_lexer_diagnostics_test.exe`
    passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_lexer_check.json
    --strict --check` passed with 3,132 rules.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: behavior already emitted the authoritative warning; this
  repair connects that existing semantic path to its formal conformance rule.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Missing Terminator Trace

- Source object: Cursive bootstrap parser statement-terminator diagnostics;
  primary path `LLVMBootstrap/cursive/src/02_source/parser/parser_terminator.cpp`
  with regression coverage in
  `LLVMBootstrap/cursive/src/tests/parser_terminator_diagnostics_test.cpp`.
- Target object: bootstrap parse-phase conformance tracing for required
  statement terminators that emit the missing-terminator diagnostic.
- Specification basis: `Missing-Terminator-Err@L7579`,
  `ConsumeTerminatorOpt-Req-No@L11614`, `ConsumeTerminatorReq-No@L11686`,
  and diagnostic `E-SRC-0510`.
- Implementation summary: the shared missing-terminator diagnostic helper now
  records `Missing-Terminator-Err` when it emits `E-SRC-0510`. The static rule
  registry scanner now recognizes `SPEC_RULE_AT(...)` calls with span
  arguments, so span-bearing rule traces are included in generated audit
  metadata. The generated registry was refreshed and now contains 3,135 mapped
  rules.
- Tests: `cursive_parser_terminator_diagnostics_test` builds a fixture with a
  required terminator omitted after `let x = 1`, expects the rebuilt compiler
  to reject the source with `error[E-SRC-0510]`, and fails if the conformance
  log omits the parse-phase `Missing-Terminator-Err` record.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_terminator_check.json
    --strict --check` passed with 3,135 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_terminator_diagnostics_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_terminator_diagnostics_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now includes
  `SPEC_RULE_AT` calls and reports 1,082 required formal-rule labels still
  absent from bootstrap trace sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Token Consumption Trace

- Source object: Cursive bootstrap parser token-consumption helpers; primary
  path `LLVMBootstrap/cursive/src/02_source/parser/parser_consume.cpp` with
  direct regression coverage in
  `LLVMBootstrap/cursive/src/tests/parser_token_consumption_test.cpp`.
- Target object: bootstrap parse-phase conformance tracing for successful
  token consumption by kind, keyword, operator, and punctuator.
- Specification basis: `Tok-Consume-Kind@L11248`,
  `Tok-Consume-Keyword@L11266`, `Tok-Consume-Operator@L11284`, and
  `Tok-Consume-Punct@L11302`.
- Implementation summary: token-consumption helpers now record each
  `Tok-Consume-*` rule only after the canonical consume state advances the
  parser. Rule labels are emitted as literal `SPEC_RULE(...)` calls, which
  makes the generated static registry include all four obligations. The
  generated registry was refreshed and now contains 3,139 mapped rules.
- Tests: `cursive_parser_token_consumption_test` directly exercises matching
  kind, keyword, operator, and punctuator consumption, verifies each successful
  trace is recorded exactly once, and verifies a failed keyword match leaves
  the parser at the original token without recording `Tok-Consume-Keyword`.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_token_consume_check.json
    --strict --check` passed with 3,139 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_token_consumption_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_token_consumption_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports 1,078
  required formal-rule labels still absent from bootstrap trace sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Parse Phase File Trace

- Source object: Cursive bootstrap top-level file parser; primary path
  `LLVMBootstrap/cursive/src/02_source/parser/parser.cpp` with driver
  regression coverage in
  `LLVMBootstrap/cursive/src/tests/lexer_diagnostics_test.cpp`.
- Target object: bootstrap parse-phase conformance tracing for the invariant
  that successful `ParseFile` produces the parse-phase file result.
- Specification basis: `Phase1-File@L2683`.
- Implementation summary: `ParseFile` now records `Phase1-File` after it has
  produced an `ASTFile` and unsafe-span metadata. The generated static rule
  registry was refreshed and now contains 3,140 mapped rules.
- Tests: `cursive_lexer_diagnostics_test` now also fails if the compiler
  driver conformance log omits the parse-phase `Phase1-File` record while
  compiling the leading-zero fixture.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_phase1_file_check.json
    --strict --check` passed with 3,140 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target cursive_lexer_diagnostics_test"`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_lexer_diagnostics_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports 1,077
  required formal-rule labels still absent from bootstrap trace sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Generic Parser Trace Metadata

- Source object: Cursive bootstrap parser conformance trace metadata for
  payload-carrying rule helpers; primary paths
  `LLVMBootstrap/cursive/tools/generate_static_rule_registry.py`,
  `LLVMBootstrap/cursive/src/02_source/parser/item/generic_params.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/type/type_path.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/item/where_clause.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/item/contract_clause.cpp`, and
  `LLVMBootstrap/cursive/src/02_source/parser/type/refinement_clause.cpp`,
  with driver regression coverage in
  `LLVMBootstrap/cursive/src/tests/parser_generic_conformance_test.cpp`.
- Target object: generated static rule metadata and parse-phase conformance
  tracing for generic parameters, generic arguments, class bounds, and
  predicate requirement tails, contract clauses, and type-refinement
  disambiguation.
- Specification basis: `Parse-GenericArgs@L47221`,
  `Parse-GenericArgsOpt-None@L47237`,
  `Parse-GenericArgsOpt-Yes@L47253`,
  `Parse-GenericParams@L47301`,
  `Parse-TypeParamTail-End@L47317`,
  `Parse-TypeParamTail-Cons@L47333`,
  `Parse-TypeBoundsOpt-None@L47365`,
  `Parse-TypeBoundsOpt-Yes@L47381`,
  `Parse-ClassBoundList-Cons@L47397`,
  `Parse-ClassBoundListTail-End@L47413`,
  `Parse-ClassBoundListTail-Cons@L47429`,
  `Parse-ClassBound@L47445`,
  `Parse-TypeDefaultOpt-None@L47461`,
  `Parse-TypeDefaultOpt-Yes@L47477`,
  `Parse-PredicateClauseOpt-None@L47493`,
  `Parse-PredicateClauseOpt-Yes@L47509`,
  `Parse-PredicateReqList-Cons@L47525`,
  `Parse-PredicateReqListTail-End@L47541`,
  `Parse-PredicateReqListTail-TrailingTerminator@L47557`,
  `Parse-PredicateReqListTail-Cons@L47573`,
  `Parse-ClassItemList-End@L48458`,
  `Parse-AssocTypeOpt-None@L49789`,
  `Parse-AssocTypeOpt-Yes@L49805`,
  `Parse-AssocTypeDefaultOpt@L49821`,
  `Parse-RefinementOpt-None@L50952`,
  `Parse-RefinementOpt-Yes@L50968`,
  `ParsePredicateExpr@L50984`,
  `Parse-ContractClauseOpt-None@L54465`,
  `Parse-ContractClauseOpt-Yes@L54481`,
  `Parse-ContractBody-PostOnly@L54497`,
  `Parse-ContractBody-PrePost@L54513`,
  `Parse-ContractBody-PreOnly@L54529`, and
  `ParseLoopInvariantOpt@L55685`.
- Implementation summary: the static rule registry scanner now extracts
  literal rule IDs from `Record*Rule("...")` helper calls in addition to
  `SPEC_RULE(...)` and `SPEC_RULE_AT(...)`. This preserves the existing
  payload-rich runtime traces while making those trace sites visible to
  generated audit metadata. The generic parser now also records type-bound
  options, class-bound list steps, type-default options, predicate-clause
  options, predicate-list construction, class item-list termination, and
  associated-type defaults. Predicate-expression parsing and loop-invariant
  delegation now also record their specification rule labels. Contract-clause
  parsing now records the optional-clause and body-shape labels. Type
  refinement parsing now consumes `|:` only for the `|: { ... }` refinement
  form, which leaves procedure contracts for `ParseContractClauseOpt`. The
  generated registry was refreshed and now contains 3,173 mapped rules.
- Tests: `cursive_parser_generic_conformance_test` compiles a parse-only
  fixture containing generic parameters, type defaults, type bounds, a
  type-parameter tail, class-bound list tails, class and record associated
  types, generic type arguments, a predicate requirement tail, a type
  refinement, a type invariant, a loop invariant, and pre-only, post-only, and
  pre/post procedure contracts. The test fails if the real compiler-driver
  conformance log omits the representative generic, predicate, refinement,
  invariant, or contract parse records.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_contract_clause_check.json
    --strict --check` passed with 3,173 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_generic_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now recognizes
  `Record*Rule("...")` trace helpers and reports 1,049 required formal-rule
  labels still absent from bootstrap trace sites before the contract-clause
  slice, and 1,044 after it.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Closure Type Parser Trace

- Source object: Cursive bootstrap function and closure type parsers; primary
  paths `LLVMBootstrap/cursive/src/02_source/parser/type/function_type.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/type/closure_type.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/type/type_common.cpp`, and
  `LLVMBootstrap/cursive/src/02_source/parser/expr/closure_expr.cpp`, with
  driver regression coverage in
  `LLVMBootstrap/cursive/src/tests/parser_generic_conformance_test.cpp`.
- Target object: parse-phase conformance tracing for function parameter-list
  continuation and closure type parameter-list, dependency, and empty-closure
  forms.
- Specification basis: `Parse-ParamTypeListTail-Cons@L46414`,
  `Parse-Closure-Type@L46675`,
  `Parse-Closure-Type-Empty@L46691`,
  `Parse-ClosureParamType-Grouped@L46707`,
  `Parse-ClosureParamType-Plain@L46723`,
  `Parse-ClosureParamTypeList-Empty@L46739`,
  `Parse-ClosureParamTypeList-Cons@L46755`,
  `Parse-ClosureParamTypeListTail-End@L46771`,
  `Parse-ClosureParamTypeListTail-TrailingComma@L46787`,
  `Parse-ClosureParamTypeListTail-Comma@L46803`,
  `Parse-ClosureDepsOpt-None@L46819`,
  `Parse-ClosureDepsOpt-Some@L46835`,
  `Parse-Closure-Expr-Empty@L63310`, and
  `Parse-ClosureParam-MoveTyped@L63390`,
  `Parse-ClosureParam-MoveUntyped@L63406`,
  `Parse-ClosureParam-Typed@L63422`,
  `Parse-ClosureParam-Untyped@L63438`,
  `Parse-ClosureRetOpt-Some@L63454`,
  `Parse-ClosureRetOpt-None@L63470`, and
  `Parse-ClosureBody-Expr@L63502`.
- Implementation summary: function type parameter-list continuation now records
  `Parse-ParamTypeListTail-Cons`, matching the required formal rule name.
  Closure type parameter-list tracing now uses the closure-specific list and
  tail labels. Empty closure type parsing now accepts the lexer token `||` as
  the empty delimiter pair and records the empty parameter-list trace before
  constructing the empty closure type. Closure expression parsing also accepts
  `||` as the empty parameter-list form, preserving the same surface spelling
  in expression context. Closure parameter parsing now records the specific
  move/typed shape used by the source, and closure expression return parsing
  records whether an explicit return type was present. The generated registry
  was refreshed and now contains 3,184 mapped rules.
- Tests: `cursive_parser_generic_conformance_test` now compiles a parse-only
  fixture containing a multi-parameter function type, `|| -> R`, closure type
  dependencies, grouped union closure parameters, and a multiline trailing
  comma before the closing closure delimiter, plus closure expressions covering
  empty, typed, untyped, move-typed, move-untyped, return-annotated, and
  inferred-return forms. The test fails if the real compiler-driver conformance
  log omits the representative function type, closure type, or closure
  expression parse records.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_closure_expr_check.json
    --strict --check` passed with 3,184 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_generic_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports 1,032
  required formal-rule labels still absent from bootstrap trace sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Range Tail Parser Trace

- Source object: Cursive bootstrap range expression parser; primary path
  `LLVMBootstrap/cursive/src/02_source/parser/expr/range.cpp` with driver
  regression coverage in
  `LLVMBootstrap/cursive/src/tests/parser_generic_conformance_test.cpp`.
- Target object: parse-phase conformance tracing for bounded exclusive and
  inclusive range-expression tails.
- Specification basis: `Parse-RangeTail-Exclusive@L37280` and
  `Parse-RangeTail-Inclusive@L37298`.
- Implementation summary: bounded range-tail parsing now records the
  authoritative `Parse-RangeTail-Exclusive` and
  `Parse-RangeTail-Inclusive` labels instead of the older abbreviated labels.
  The generated registry was refreshed and still contains 3,184 mapped rules,
  because this replaces two non-authoritative trace labels with the required
  formal rule names.
- Tests: `cursive_parser_generic_conformance_test` now compiles a parse-only
  fixture containing both `0..1` and `0..=1`, and fails if the real
  compiler-driver conformance log omits either bounded range-tail record.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_range_tail_check.json
    --strict --check` passed with 3,184 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_generic_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports 1,030
  required formal-rule labels still absent from bootstrap trace sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap If-Case List Parser Trace

- Source object: Cursive bootstrap `if ... is { ... }` parser; primary path
  `LLVMBootstrap/cursive/src/02_source/parser/expr/if_expr.cpp` with driver
  regression coverage in
  `LLVMBootstrap/cursive/src/tests/parser_generic_conformance_test.cpp`.
- Target object: parse-phase conformance tracing for case-list construction,
  recursive case tails, else tails, and tail termination.
- Specification basis: `Parse-IfCases-Cons@L66265`,
  `Parse-IfCasesTail-End@L66297`,
  `Parse-IfCasesTail-Else@L66313`, and
  `Parse-IfCasesTail-Cons@L66329`.
- Implementation summary: `ParseIfCaseList` now records the formal list-shape
  labels at the existing parser control points: the first case establishes
  `Parse-IfCases-Cons`, subsequent cases record `Parse-IfCasesTail-Cons`, an
  else arm records `Parse-IfCasesTail-Else`, and a non-empty case list ending
  at `}` records `Parse-IfCasesTail-End`. The generated registry was refreshed
  and now contains 3,188 mapped rules.
- Tests: `cursive_parser_generic_conformance_test` now compiles parse-only
  fixtures for a multi-case `if ... is { ... else ... }` expression and a
  non-empty no-else case list. The test fails if the real compiler-driver
  conformance log omits any of the four case-list rule records.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_if_cases_check.json
    --strict --check` passed with 3,188 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_generic_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports 1,026
  required formal-rule labels still absent from bootstrap trace sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Extern Block Parser Trace

- Source object: Cursive bootstrap extern block parser; primary paths
  `LLVMBootstrap/cursive/src/02_source/parser/item/extern_block.cpp` and
  `LLVMBootstrap/cursive/src/02_source/parser/item/parse_item.cpp`, with
  driver regression coverage in
  `LLVMBootstrap/cursive/src/tests/parser_generic_conformance_test.cpp`.
- Target object: parse-phase conformance tracing and parsing behavior for
  extern block shells and optional ABI specifiers.
- Specification basis: `Parse-ExternBlock@L31172`,
  `Parse-ExternAbiOpt-String@L31208`, and
  `Parse-ExternAbiOpt-Ident@L31226`.
- Implementation summary: extern block parsing now records the authoritative
  `Parse-ExternBlock` label at both entry points. String ABI parsing now
  records `Parse-ExternAbiOpt-String`, and identifier ABI parsing now accepts
  `extern C { ... }` as `ExternAbiIdent` with the required
  `Parse-ExternAbiOpt-Ident` trace. The generated registry was refreshed and
  now contains 3,189 mapped rules.
- Tests: `cursive_parser_generic_conformance_test` now compiles parse-only
  fixtures for string ABI, identifier ABI, and default ABI extern blocks. The
  test fails if the real compiler-driver conformance log omits the extern
  block, string ABI, identifier ABI, or default ABI records.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_extern_parser_check.json
    --strict --check` passed with 3,189 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_generic_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports 1,023
  required formal-rule labels still absent from bootstrap trace sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Foreign Ensures Parser Trace

- Source object: Cursive bootstrap foreign contract parser; primary path
  `LLVMBootstrap/cursive/src/02_source/parser/item/foreign_contract_clause.cpp`
  with driver regression coverage in
  `LLVMBootstrap/cursive/src/tests/parser_generic_conformance_test.cpp`.
- Target object: parse-phase conformance tracing for plain, `@error`, and
  `@null_result` foreign ensures predicates.
- Specification basis: `Parse-EnsuresPredicate-Error@L88268`,
  `Parse-EnsuresPredicate-NullResult@L88284`, and
  `Parse-EnsuresPredicate-Plain@L88300`.
- Implementation summary: `ParseEnsuresPredicate` now records the formal rule
  label for each branch it already parsed: `@error: predicate`,
  `@null_result: predicate`, and a plain predicate expression. The generated
  registry was refreshed and now contains 3,192 mapped rules.
- Tests: `cursive_parser_generic_conformance_test` now compiles parse-only
  extern procedure fixtures with `@foreign_ensures(true)`,
  `@foreign_ensures(@error: true)`, and
  `@foreign_ensures(@null_result: true)`. The test fails if the real
  compiler-driver conformance log omits any of the three ensures-predicate
  rule records.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_ensures_predicate_check.json
    --strict --check` passed with 3,192 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_generic_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports 1,020
  required formal-rule labels still absent from bootstrap trace sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Compile-Time Primary Parser Trace

- Source object: Cursive bootstrap parsers for compile-time primary forms and
  derive targets; primary paths
  `LLVMBootstrap/cursive/src/02_source/parser/expr/comptime_expr.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/expr/type_literal.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/expr/quote_expr.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/expr/identifier.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/expr/expr_common.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/stmt/expr_stmt.cpp`, and
  `LLVMBootstrap/cursive/src/02_source/parser/item/derive_target_decl.cpp`.
- Target object: parse-phase conformance tracing for compile-time expression
  blocks, compile-time capability identifiers, reflection type literals, raw
  quote forms, type quote forms, pattern quote forms, and derive target
  declarations.
- Specification basis: `Parse-CtExpr@L82622`,
  `Parse-CtCapRef@L83484`, `Parse-TypeLiteral@L84090`,
  `Parse-Quote-Raw@L84563`, `Parse-Quote-Type@L84579`,
  `Parse-Quote-Pattern@L84595`, and `Parse-DeriveTargetDecl@L85019`.
- Implementation summary: the existing successful parse branches now record the
  exact formal rule labels. Identifier primary parsing records
  `Parse-CtCapRef` when the identifier lexeme is one of the compile-time
  capability names. Expression-start predicates now admit `quote`, matching the
  primary-expression grammar. The generated registry was refreshed and now
  contains 3,199 mapped rules.
- Tests: `cursive_parser_generic_conformance_test` now compiles parse-only
  fixtures covering `Type::<i32>`, `quote { ... }`, `quote type { ... }`,
  `quote pattern { ... }`, `comptime { introspect }`, and a derive target
  declaration with `requires` and `emits` clauses. The test fails if the real
  compiler-driver conformance log omits any of the new rule records.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_parse_meta_final_check.json
    --strict --check` passed with 3,199 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_generic_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_lexer_diagnostics_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_terminator_diagnostics_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_token_consumption_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports 1,013
  required formal-rule labels still absent from bootstrap trace sites, and no
  parse-shaped required formal-rule labels remain absent.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Compile-Time Execution Trace

- Source object: Cursive bootstrap compile-time analysis and execution paths;
  primary paths `LLVMBootstrap/cursive/src/03_comptime/pass.cpp`,
  `LLVMBootstrap/cursive/src/03_comptime/rewrite.cpp`,
  `LLVMBootstrap/cursive/src/03_comptime/eval.cpp`,
  `LLVMBootstrap/cursive/src/03_comptime/derive.cpp`,
  `LLVMBootstrap/cursive/src/03_comptime/reflect.cpp`,
  `LLVMBootstrap/cursive/src/04_analysis/typing/type_expr.cpp`,
  `LLVMBootstrap/cursive/src/04_analysis/typing/expr/loop_iter.cpp`, and
  `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck.cpp`.
- Target object: required `spec.comptime` formal-rule coverage for compile-time
  expression typing, compile-time module execution, item and statement
  expansion, compile-time expression expansion, quote evaluation, Type literal
  evaluation, diagnostics and emit builtins, reflection builtins, and derive
  target execution.
- Specification basis: `T-CtExpr@L82900`, `T-CtIf@L82916`,
  `T-CtLoopIter@L82932`, `T-CtProc@L82948`,
  `ComptimePass-Empty@L83099`, `ComptimePass-Cons@L83114`,
  `CtExecModule@L83148`, `CtExpandItemSeq-Empty@L83164`,
  `CtExpandItemSeq-Cons@L83179`, `CtExpandItem-CtProc@L83221`,
  `CtExpandStmtSeq-Empty@L83237`, `CtExpandStmtSeq-Cons@L83252`,
  `CtExpandBlock@L83268`, `CtExpandStmt-CtStmt@L83284`,
  `CtExpandExpr-CtExpr@L83300`, `CtExpandExpr-CtIf-True@L83316`,
  `CtExpandExpr-CtIf-False@L83332`,
  `CtExpandExpr-CtLoopIter@L83348`,
  `CtLoopIterUnroll-Empty@L83364`,
  `CtLoopIterUnroll-Cons@L83379`, `CtBuiltin-Emit@L83773`,
  diagnostics builtin rules at `L83933-L83997`, `T-TypeLiteral@L84265`,
  `CtEval-TypeLiteral@L84340`, reflection builtin rules at `L84356-L84452`,
  `CtEval-Quote@L84893`, and derive expansion rules at `L85326-L85402`.
- Implementation summary: the existing successful compile-time execution,
  rewrite, builtin, quote, reflection, derive, and typechecking branches now
  record the exact formal rule labels. A new
  `cursive_comptime_conformance_test` driver regression compiles a `.uv`
  fixture through `--check`, records a conformance log, and fails if the log
  omits the module execution, block/statement expansion, compile-time
  expression, true/false compile-time if, loop unroll, compile-time statement,
  or quote-evaluation records that the fixture exercises.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_comptime.json
    --strict` passed with 3,240 rules.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_comptime_check.json
    --strict --check` passed with 3,240 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_comptime_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_comptime_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_lexer_diagnostics_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_terminator_diagnostics_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_token_consumption_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports zero
  missing required `spec.comptime` formal-rule labels. The global comparison now
  reports 972 required formal-rule labels still absent from bootstrap trace
  sites.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.

### Bootstrap Parser Family Trace

- Source object: Cursive bootstrap expression parser family entry points;
  primary paths `LLVMBootstrap/cursive/src/02_source/parser/expr/call.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/expr/range.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/expr/binary.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/expr/transmute_expr.cpp`,
  `LLVMBootstrap/cursive/src/02_source/parser/expr/record_literal.cpp`,
  and `LLVMBootstrap/cursive/src/02_source/parser/expr/if_expr.cpp`.
- Target object: required parse-phase family trace coverage for argument-list,
  range, left-associative binary chain, power, transmute, construction-list and
  shorthand, and remaining control-expression parsing obligations.
- Obligation basis: `ArgumentListParsingFamily@L58538`,
  `ParseRangeFamily@L59239`, `ParseLeftChainFamily@L59252`,
  `ParsePowerFamily@L59265`, `ParseTransmuteExprFamily@L60009`,
  `ConstructionListAndShorthandParsingFamily@L60785`, and
  `ControlExpressionParsingRemainderFamily@L61376` from
  `Docs/Audit/UltravioletObligations.csv`. These are obligations CSV family
  labels over existing parser component rules.
- Implementation summary: the owning parser entry points now record one family
  label for each obligations CSV family. The existing component traces remain
  unchanged. `cursive_parser_generic_conformance_test` now drives accepted
  parser forms for empty and moved calls, full/to/from/exclusive/inclusive
  ranges, left-chain and power expressions, tuple and field construction,
  normal and nested transmute syntax, if-case/else parsing, and iterator and
  conditional loop tails.
- Verification commands:
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_report_after_parsing.json
    --strict` passed with 3,247 rules.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --self-test` passed.
  - `python3 LLVMBootstrap/cursive/tools/generate_static_rule_registry.py
    --repo-root LLVMBootstrap/cursive --source-root
    LLVMBootstrap/cursive/src --spec-path SPECIFICATION.md --mapping-path
    LLVMBootstrap/cursive/tools/static_rule_mapping.json --output-path
    LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc
    --report-path /tmp/uv_static_rule_registry_check_final.json --strict
    --check` passed with 3,247 rules.
  - `cmd.exe /c "cd /d C:\Dev\Ultraviolet\LLVMBootstrap\cursive && cmake
    --build --preset windows-release --target
    cursive_parser_generic_conformance_test"` passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_generic_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_comptime_conformance_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_lexer_diagnostics_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_terminator_diagnostics_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/Release/cursive_parser_token_consumption_test.exe`
    passed.
  - `/mnt/c/Dev/Ultraviolet/LLVMBootstrap/cursive/build/windows/out/Cursive.exe
    --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
    UltravioletRT` passed with zero diagnostics.
- Bootstrap notes: the normalized formal-rule comparison now reports
  `required_formal_rules=2940`, `trace_labels=3247`, `missing=965`, and
  `missing_parsing=0`. The global missing count dropped from 972 to 965 in this
  slice.
- Acceptance: pending user review, 2026-05-11, implemented with fresh command
  output.
