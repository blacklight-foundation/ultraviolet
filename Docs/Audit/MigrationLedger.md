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
  `req.TestContextAuthority@L28477`,
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
