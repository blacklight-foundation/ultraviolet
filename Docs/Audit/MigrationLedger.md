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

### W1 - Cursive Ultraviolet Project Support

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
- Tests: source-native tests are not introduced in W1; verification uses the
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

### W2 - Source-Native Test Attribute Surface

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
- Specification basis: `def.SpecAttributeRegistry@L26885`,
  `def.SpecAttributeTargets@L26908`,
  `def.AttributeStaticSemantics.Helpers2@L27012`,
  `grammar.TestAttribute@L28264`,
  `parse.TestAttributeByOrdinaryAttributeParser@L28284`,
  `ast.TestProcedureClassification@L28302`, `def.TestName@L28317`,
  `def.TestCoverage@L28332`, `req.TestAttributeProcedureTarget@L28349`,
  `def.TestAttributeArgsOk@L28363`, `req.TestProcedureShape@L28383`,
  `req.TestContextAuthority@L28405`,
  `conformance.TestAttributeDynamicSemantics@L28423`,
  `lowering.TestHarnessGeneration@L28444`,
  `def.TestDiscoveryOrder@L28503`, and
  `diagnostics.TestAttributes@L28521`.
- Implementation summary: Ultraviolet now specifies `[[test]]` as a
  procedure-target attribute with optional `name: "..."` and `covers("id@Lline")`
  metadata, ordinary contract-based pass/fail semantics, deterministic discovery,
  ephemeral harness lowering, and concrete diagnostics. Cursive recognizes the
  attribute, validates its argument shape, validates represented test procedure
  shape, and resolves the new diagnostics through the generated registry.
- Tests: `Compiler/Tests/Parser/AttributeTests.uv` adds
  `testAttributeAcceptsSourceNativeContract`, covering
  `grammar.TestAttribute@L28264` and `req.TestProcedureShape@L28383`.
- Verification commands: `python3 Tools/ExtractObligationLedger.py --check`
  passed with 5966 obligations; `python3 cursive/tools/generate_diagnostic_registry.py
  --check && python3 cursive/tools/validate_diagnostic_spec_sync.py` passed in
  the Cursive repo; Windows Release build target `cursive` succeeded; `Cursive.exe
  --target-profile x86_64-win64 --check C:\Dev\Ultraviolet --assembly
  UltravioletCompiler` succeeded with 39 parsed modules and zero diagnostics.
- Bootstrap notes: Cursive now preserves explicit visibility spelling for
  ordinary procedures so the bootstrap checker can enforce the
  explicit-visibility portion of `req.TestProcedureShape@L28383`.
- Non-applicability notes: `uv test` execution, coverage report emission, unknown
  coverage-reference validation against the ledger, and harness invocation are
  owned by the future Ultraviolet `Compiler/Driver/CLI/TestCommand.uv` and
  `Compiler/Driver/Test*.uv` migration slices.
- Acceptance: pending user review, 2026-05-05, implemented with fresh command
  output.

### W3 - Positional `uv test` Target Surface

- Source object: Ultraviolet source-native test command design in
  `SPECIFICATION.md` §9.6.6 and `CompilerRebuildPlan.md`; no C++ bootstrap
  implementation object applies because the Cursive bootstrap compiler does not
  own the future `uv test` CLI surface.
- Target object: `Compiler/Driver/CLI/TestCommand.uv`, `TestDiscovery.uv`,
  `TestHarness.uv`, `TestExecution.uv`, `TestResults.uv`, and `TestCoverage.uv`
  implementation contract updated to use positional test targets.
- Specification basis: `lowering.TestHarnessGeneration@L28444`,
  `def.TestDiscoveryOrder@L28503`, `WF-Assembly@L3511`,
  `Select-By-Name@L3887`, `Module-Dir@L4306`, and
  `def.FindProjectRoot@L2324`.
- Implementation summary: the normative test command surface is now
  `uv test [target]`. The target resolves as absent target, assembly name,
  module path, source file path, or directory path. The rebuild plan no longer
  requires a test-specific assembly or project-root flag.
- Tests: no source-native test body changed in W3; this entry corrects the
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
