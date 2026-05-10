# Plan: Finish Ultraviolet Self-Hosted Testing Functionality

## Goal

Complete the source-native/self-hosted `[[test]]` capability so `uv test [target]` can discover tests from loaded Ultraviolet projects, validate their conformance metadata, generate and build an ephemeral harness, execute selected tests deterministically, classify results, print useful diagnostics/reports, and return meaningful exit codes.

The intended end state is that the test functionality satisfies the obligations in:

- `SPECIFICATION.md` §9.6 Source-Native Test Attributes
- `Docs/Internal/UltravioletSpecification.obligations.md` lines around `28332-28615`

Core obligations to close:

- `grammar.TestAttribute@L28336`
- `parse.TestAttributeByOrdinaryAttributeParser@L28356`
- `ast.TestProcedureClassification@L28374`
- `def.TestName@L28389`
- `def.TestCoverage@L28404`
- `req.TestAttributeProcedureTarget@L28421`
- `def.TestAttributeArgsOk@L28435`
- `req.TestProcedureShape@L28455`
- `req.TestContextAuthority@L28477`
- `conformance.TestAttributeDynamicSemantics@L28495`
- `lowering.TestHarnessGeneration@L28516`
- `def.TestDiscoveryOrder@L28575`
- `diagnostics.TestAttributes@L28593`

## Current context / assumptions

Current reviewed state:

- The repository is at `C:\Dev\Ultraviolet`.
- The project config is `Ultraviolet.toml`.
- A built executable exists at `Build/UvExecutable/bin/uv.exe`.
- `uv.exe version` works and prints `ultraviolet 0.0.0`.
- `uv.exe test .` currently exits `10` with no stdout/stderr.
- `uv.exe test NoSuchTarget` also exits `10` with no stdout/stderr.
- The source tree already contains many `[[test]]` declarations; a prior scan found 221 occurrences across 99 `.uv` files outside `Build` and `.git`.

Implemented foundation already present:

- Attribute parser/model helpers:
  - `Compiler/Source/Parser/AttributeParser.uv`
  - `Compiler/Source/Ast/Attributes.uv`
- Test attribute semantic helpers:
  - `Compiler/Semantics/Attributes/TestAttributes.uv`
- Test procedure shape helpers:
  - `Compiler/Semantics/Declarations/Procedures.uv`
- Diagnostic code constants:
  - `Compiler/Diagnostics/Codes.uv`
- CLI command skeleton:
  - `Compiler/Driver/CLI/TestCommand.uv`
  - `Compiler/Driver/CLI/Commands.uv`
- Test driver models:
  - `Compiler/Driver/Testing/TestDiscovery.uv`
  - `Compiler/Driver/Testing/TestHarness.uv`
  - `Compiler/Driver/Testing/TestExecution.uv`
  - `Compiler/Driver/Testing/TestResults.uv`
  - `Compiler/Driver/Testing/TestCoverage.uv`
- Test context support:
  - `Compiler/Tests/TestSupport/TestContext.uv`

Known blockers / gaps:

1. `Compiler/Driver/CLI/TestCommand.uv` currently passes `emptyDiscoveredTests()` into `sourceNativeTestExecutionFromProject`.
2. The CLI path currently returns `commandFailedWithDiagnostics(testDriverExecutionDiagnostics(...))` even for a planned/ready execution.
3. `TestHarness.uv` models harness generation but does not appear to write, compile, or invoke a harness.
4. `TestExecution.uv` models lifecycle states but does not appear to drive execution to `@Completed`.
5. `TestCoverage.uv` validates coverage-reference shape, but not actual existence in the obligation ledger.
6. CLI diagnostics are not visible in stdout/stderr for the tested failure paths.

## Proposed approach

Finish the feature in vertical slices, preserving the current obligation-oriented file structure:

1. Make `uv test` produce useful diagnostics first.
2. Implement real project test discovery and wire it into `uv test`.
3. Complete static validation integration for `[[test]]` attributes and procedure shape.
4. Implement obligation-ledger coverage lookup.
5. Implement harness generation.
6. Implement build/invoke/result collection.
7. Add end-to-end fixtures and conformance tests.
8. Harden reporting, exit-code semantics, and target filtering.

Each slice should include self-hosted `.uv` tests plus at least one host-level smoke check using `Build/UvExecutable/bin/uv.exe` once the executable is rebuilt.

## Step-by-step plan

### Phase 0: Establish observable baseline and reporting

Goal: ensure failures from `uv test` are visible before changing deeper behavior.

Tasks:

1. Trace command-result emission for failed commands.
   - Inspect command host / entrypoint code under:
     - `Tools/Uv/`
     - `Compiler/Driver/CLI/`
     - any diagnostic emission modules under `Compiler/Diagnostics/`
2. Find where `CommandResult@Failed.diagnostics` should be printed.
3. Add or fix diagnostic emission so failed commands print code + message to stderr or the compiler's standard diagnostic stream.
4. Add a minimal test that unknown `uv test` target emits `E-TST-0108`.

Likely files:

- `Tools/Uv/*`
- `Compiler/Driver/CLI/Commands.uv`
- `Compiler/Driver/CLI/TestCommand.uv`
- `Compiler/Diagnostics/*`
- Existing or new CLI tests under `Compiler/Tests/Driver/CLI/`

Validation:

- `Build/UvExecutable/bin/uv.exe test NoSuchTarget` should print an `E-TST-0108` diagnostic.
- Exit code should remain nonzero for unknown target.

### Phase 1: Implement real test discovery from loaded project

Goal: replace the placeholder `emptyDiscoveredTests()` in `uv test` with discovered procedures from the loaded project.

Tasks:

1. Identify the loaded-project representation for assemblies, modules, source files, parsed declarations, symbols, attributes, spans, and semantic data.
   - Likely under `Compiler/Project/`, `Compiler/Source/`, and `Compiler/Semantics/`.
2. Add a discovery API, likely in `Compiler/Driver/Testing/TestDiscovery.uv`, such as:
   - `discoverTestsFromProject(diagnostics_region, project) -> TestDiscoveryResultValue`
   - or `discoverTestsFromAssembly(...)`
3. For each procedure declaration:
   - detect `[[test]]` via `attributeIsSourceNativeTest` / ordinary attribute lookup,
   - verify ordinary source procedure target,
   - compute fully-qualified procedure path,
   - evaluate `checkTestAttributeArguments`,
   - evaluate `checkTestProcedureShape`,
   - capture `requires_context`, `display_name`, `coverage_references`, source file order, declaration span, module path, and owning assembly.
4. Emit/accumulate diagnostics rather than stopping at the first invalid test when possible.
5. Return discovered tests in an unsorted list; use existing `sortDiscoveredTests` at selection/execution time.
6. Wire `executeLoadedTestProject` to pass the discovered list instead of `emptyDiscoveredTests()`.

Likely files:

- `Compiler/Driver/CLI/TestCommand.uv`
- `Compiler/Driver/Testing/TestDiscovery.uv`
- `Compiler/Semantics/Attributes/TestAttributes.uv`
- `Compiler/Semantics/Declarations/Procedures.uv`
- `Compiler/Project/*`
- `Compiler/Source/Ast/*`
- `Compiler/Semantics/*`

Tests to add/update:

- New discovery integration tests under `Compiler/Tests/Driver/Testing/`, for example:
  - `Compiler/Tests/Driver/Testing/DiscoverProjectTests.uv`
- Cases:
  - discovers one valid test under `Assembly::Tests::*`,
  - ignores or rejects `[[test]]` outside an ordinary procedure,
  - carries stable identity and display name,
  - carries coverage references in source order,
  - captures source file order and declaration span,
  - rejects invalid procedure shape with the expected diagnostic code.

Validation:

- Unit tests for discovery pass.
- `uv test .` should no longer fail because discovery was empty or execution was merely planned; it may still fail in later phases until harness execution is completed, but diagnostics should explain that.

### Phase 2: Complete static validation integration

Goal: ensure the semantic pass enforces all static `[[test]]` rules on real source declarations.

Tasks:

1. Ensure `[[test]]` applied outside ordinary source procedures emits `E-MOD-2452`.
2. Ensure malformed arguments emit:
   - `E-TST-0101` for unknown/malformed argument kind,
   - `E-TST-0102` for duplicate `name`,
   - `E-TST-0103` for malformed `covers(...)` shape.
3. Ensure invalid procedure shape emits:
   - `E-TST-0104` for missing body, generic, missing explicit visibility, missing explicit return type,
   - `E-TST-0105` for invalid parameter shape,
   - `E-TST-0106` for missing postcondition.
4. Prefer diagnostic constants from `Compiler/Diagnostics/Codes.uv` instead of literal strings in semantic files.
5. Add source-level negative tests for each diagnostic.

Likely files:

- `Compiler/Semantics/Attributes/TestAttributes.uv`
- `Compiler/Semantics/Declarations/Procedures.uv`
- `Compiler/Diagnostics/Codes.uv`
- `Compiler/Diagnostics/Emission.uv` or equivalent diagnostic rendering module
- Tests under:
  - `Compiler/Tests/Semantics/Attributes/TestAttributes/`
  - `Compiler/Tests/Semantics/Declarations/TestProcedures/`
  - `Compiler/Tests/Diagnostics/TestAttributes/`

Validation:

- Existing semantic helper tests still pass.
- New source-driven negative tests prove real AST/semantic integration, not just helper function behavior.

### Phase 3: Implement obligation-ledger validation for `covers(...)`

Goal: satisfy the spec requirement that each coverage reference names one actual obligation ledger row.

Tasks:

1. Decide how the compiler should access the obligation ledger:
   - parse `Docs/Internal/UltravioletSpecification.obligations.md` at build/test time,
   - use a generated manifest/ledger artifact,
   - or embed a compact ledger index produced by the build.
2. Create a ledger lookup API, likely under a new or existing module such as:
   - `Compiler/Driver/Testing/TestCoverage.uv`
   - `Compiler/Conformance/ObligationLedger.uv`
   - `Compiler/Docs/Obligations.uv`
3. Validate references of the form `obligation-id@Linternal_spec_line` by both shape and existence.
4. Emit `E-TST-0107` when a syntactically valid reference does not exist in the ledger.
5. Keep `E-TST-0103` for malformed shape.
6. Add tests for:
   - valid obligation reference,
   - malformed reference,
   - syntactically valid but unknown reference,
   - line mismatch for known id.

Likely files:

- `Compiler/Driver/Testing/TestCoverage.uv`
- `Compiler/Semantics/Attributes/TestAttributes.uv`
- `Compiler/Diagnostics/Codes.uv`
- New obligation ledger support module if needed
- `Docs/Internal/UltravioletSpecification.obligations.md`
- Tests under `Compiler/Tests/Driver/Testing/` or `Compiler/Tests/Diagnostics/TestAttributes/`

Validation:

- `covers("def.TestDiscoveryOrder@L28575")` succeeds.
- `covers("def.TestDiscoveryOrder@L1")` or `covers("missing.Obligation@L1")` emits `E-TST-0107`.
- malformed `covers("missing")` emits `E-TST-0103`.

### Phase 4: Generate ephemeral test harnesses

Goal: turn selected tests into a generated harness source file under the selected assembly's build output directory.

Tasks:

1. Define the generated harness source format.
   - It should import or reference selected test procedures.
   - It should call each selected test in deterministic order.
   - It should capture pass/fail/error in a machine-readable format.
2. Add a harness writer API in `Compiler/Driver/Testing/TestHarness.uv`, for example:
   - `generateHarnessForSelection(host, diagnostics_region, selection) -> HarnessGenerationValue`
3. Decide generated path convention, for example:
   - `Build/<AssemblyName>/TestHarness.uv`
   - `Build/<AssemblyName>/Generated/uv_test_harness.uv`
4. Ensure harness files are ephemeral/test-only and do not affect production artifacts.
5. Add support for `requires_context` tests:
   - construct one `TestContext` per run or per test,
   - pass it only to tests that require it.
6. Add tests for generated source shape without necessarily compiling it yet.

Likely files:

- `Compiler/Driver/Testing/TestHarness.uv`
- `Compiler/Driver/Testing/TestExecution.uv`
- `Compiler/Tests/TestSupport/TestContext.uv`
- Build-output path helpers under `Compiler/Project/` or `Compiler/Driver/`
- Tests:
  - `Compiler/Tests/Driver/Testing/LoweringTestHarnessGenerationTests.uv`

Validation:

- Harness plan transitions to `@Generated` after a file is written.
- Generated harness contains selected tests in deterministic order.
- Generated harness has correct function signatures for tests with and without `TestContext`.

### Phase 5: Compile and invoke the generated harness

Goal: complete the test execution lifecycle through `@Completed`.

Tasks:

1. Add a driver operation to compile selected assembly with the generated harness entrypoint.
2. Transition `TestRun@Planned -> @HarnessBuilt -> @ExecutableBuilt -> @Running -> @Completed`.
3. Invoke the harness executable through the host process API.
4. Define a stable protocol for harness output, for example:
   - line-oriented records,
   - JSON-like records if supported,
   - or a compact compiler-native result format.
5. Parse harness output into `TestResultListValue`.
6. Map outcomes:
   - normal return and satisfied postcondition -> `Passed`,
   - postcondition violation -> `Failed`,
   - panic/unavailable authority/harness invocation/build failure -> `Error`.
7. Ensure unavailable `TestContext` authority becomes an error outcome, not a compiler crash.

Likely files:

- `Compiler/Driver/Testing/TestExecution.uv`
- `Compiler/Driver/Testing/TestResults.uv`
- `Compiler/Driver/Testing/TestHarness.uv`
- `Compiler/Driver/CLI/TestCommand.uv`
- Pipeline/build modules under `Compiler/Driver/` and `Compiler/Project/`
- Runtime contract/postcondition failure reporting modules, if separate

Tests:

- Extend `Compiler/Tests/Driver/Testing/ConformanceTestAttributeDynamicSemanticsTests.uv`.
- Add fixture tests for pass/fail/error classification.

Validation:

- A tiny fixture with one passing test produces one passed result.
- Fixture with violated postcondition produces one failed result.
- Fixture with panic or harness invocation failure produces one error result.

### Phase 6: Implement user-facing reports and exit-code semantics

Goal: make `uv test` usable from the command line and CI.

Tasks:

1. Define summary format, for example:
   - `N tests: P passed, F failed, E errors`
2. Print each failure/error with stable identity, display name, diagnostic code, source location where available.
3. Decide exit-code semantics:
   - `0` when all selected tests pass,
   - nonzero when any fail/error or command/target/discovery diagnostic occurs,
   - optional distinct exit codes later if desired.
4. Support `--output` modes if `OutputModeValue` already has normal/quiet/json-like modes.
5. Ensure empty selected test behavior is explicit:
   - either success with `0 tests`, or nonzero if the project has no tests, depending on desired policy.
   - Document and test the chosen policy.

Likely files:

- `Compiler/Driver/CLI/TestCommand.uv`
- `Compiler/Driver/Testing/TestResults.uv`
- Diagnostic/reporting modules
- CLI command tests under `Compiler/Tests/Driver/CLI/`

Validation:

- `uv test .` prints a summary.
- `uv test NoSuchTarget` prints `E-TST-0108`.
- `uv test <source-file>` filters to that file.
- `uv test <directory>` filters to that directory.
- `uv test <assembly>` filters to that assembly's tests.
- `uv test <module>` filters to that module subtree.

### Phase 7: End-to-end conformance fixtures

Goal: prove the full system satisfies the spec, not only helper-level tests.

Tasks:

1. Create one or more fixture projects under the test tree, for example:
   - `Compiler/Tests/Fixtures/TestAttributes/PassingProject/`
   - `Compiler/Tests/Fixtures/TestAttributes/FailureProject/`
   - or follow existing fixture conventions in the repo.
2. Fixture cases:
   - valid `[[test]]` without args,
   - valid `[[test(name: "...")]]`,
   - valid `[[test(covers("...") )]]`,
   - valid `TestContext` parameter,
   - duplicate name argument,
   - malformed covers,
   - unknown coverage reference,
   - missing postcondition,
   - invalid parameter,
   - target filtering by file/dir/module/assembly.
3. Add host-level or self-hosted tests invoking the driver pipeline against fixtures.
4. Preserve deterministic ordering assertions.

Likely files:

- New fixture directories under `Compiler/Tests/Fixtures/` or existing equivalent.
- Tests under:
  - `Compiler/Tests/Driver/Testing/`
  - `Compiler/Tests/Driver/CLI/`
  - `Compiler/Tests/Diagnostics/TestAttributes/`

Validation:

- Full `uv test` fixture run passes/fails exactly as expected.
- Diagnostics contain expected codes.
- Output order is deterministic.

### Phase 8: Cleanup and hardening

Goal: reduce technical debt introduced during completion.

Tasks:

1. Replace test diagnostic literal strings with constants from `Compiler/Diagnostics/Codes.uv` where practical.
2. Ensure ownership comments in touched files list the obligations they implement.
3. Add clear comments where functionality is intentionally test-only or ephemeral.
4. Review edge cases:
   - multiple assemblies with tests,
   - no test-bearing assemblies,
   - tests outside `Assembly::Tests` subtree,
   - path normalization on Windows vs POSIX-style paths,
   - duplicate stable identities,
   - display-name collisions,
   - generic procedures,
   - invalid or unavailable `TestContext` authority.
5. Confirm generated harness artifacts are ignored/cleaned as intended.

Validation:

- Run all relevant self-hosted compiler tests.
- Run `uv test .` from the repository root.
- Run target-specific commands:
  - `uv test UltravioletCompiler`
  - `uv test UltravioletCompiler::Tests::Driver::Testing`
  - `uv test Compiler/Tests/Driver/Testing`
  - `uv test Compiler/Tests/Driver/Testing/DefTestDiscoveryOrderTests.uv`
  - `uv test NoSuchTarget`

## Files likely to change

Primary implementation files:

- `Compiler/Driver/CLI/TestCommand.uv`
- `Compiler/Driver/CLI/Commands.uv`
- `Compiler/Driver/Testing/TestDiscovery.uv`
- `Compiler/Driver/Testing/TestHarness.uv`
- `Compiler/Driver/Testing/TestExecution.uv`
- `Compiler/Driver/Testing/TestResults.uv`
- `Compiler/Driver/Testing/TestCoverage.uv`
- `Compiler/Semantics/Attributes/TestAttributes.uv`
- `Compiler/Semantics/Declarations/Procedures.uv`
- `Compiler/Diagnostics/Codes.uv`
- diagnostic emission/rendering files under `Compiler/Diagnostics/`
- driver/pipeline/build invocation files under `Compiler/Driver/`
- project/module/source traversal files under `Compiler/Project/` and `Compiler/Source/`

Likely test files to add/update:

- `Compiler/Tests/Driver/Testing/ConformanceTestAttributeDynamicSemanticsTests.uv`
- `Compiler/Tests/Driver/Testing/DefTestDiscoveryOrderTests.uv`
- `Compiler/Tests/Driver/Testing/LoweringTestHarnessGenerationTests.uv`
- new `Compiler/Tests/Driver/Testing/DiscoverProjectTests.uv`
- `Compiler/Tests/Driver/CLI/TestCommandInputTests.uv`
- `Compiler/Tests/Driver/CLI/CommandOptionTests.uv`
- `Compiler/Tests/Diagnostics/TestAttributes/DiagnosticsTestAttributesTests.uv`
- `Compiler/Tests/Semantics/Attributes/TestAttributes/*.uv`
- `Compiler/Tests/Semantics/Declarations/TestProcedures/*.uv`
- fixture files under the repository's existing fixture convention

Possibly affected support files:

- `Compiler/Tests/TestSupport/TestContext.uv`
- `Ultraviolet.toml` only if new test fixture assemblies are needed; avoid unless required.
- `.gitignore` if generated harness artifacts appear in the working tree.

## Tests / validation strategy

Because this is the Ultraviolet repository, use the project's own build/test flow. If there is a canonical script or build command, prefer that. Avoid direct ad-hoc test commands once a wrapper exists.

Validation levels:

1. Helper/model tests
   - Existing `.uv` tests for parser, semantics, diagnostics, discovery, harness models, result models.

2. Integration tests
   - Discovery from loaded project.
   - Target resolution and filtering with real project/module/source paths.
   - Coverage ledger validation.

3. CLI tests
   - `uv test` command parsing.
   - unknown target diagnostics.
   - output/report format.
   - exit code behavior.

4. End-to-end smoke tests after rebuilding executable
   - `Build/UvExecutable/bin/uv.exe version`
   - `Build/UvExecutable/bin/uv.exe test .`
   - `Build/UvExecutable/bin/uv.exe test NoSuchTarget`
   - targeted tests by assembly/module/directory/file.

5. Regression checks
   - Ensure normal `uv build`, `uv check`, and production lowering are unaffected by `[[test]]`.
   - Confirm `[[test]]` does not lower into production program artifacts.

## Risks, tradeoffs, and open questions

### Risks

- The current implementation is strongly model-oriented; wiring it to real project AST/semantic data may expose missing APIs in project/source/semantic modules.
- Harness generation may require nontrivial build-system support to compile an assembly with a test-only entrypoint.
- Runtime postcondition classification may not currently expose enough structured information to distinguish failed postcondition from panic/error.
- Windows path handling may be tricky because the repo and current environment use both `C:\...` and POSIX/MSYS-style paths.
- Obligation ledger validation may be expensive or awkward if the compiler cannot read the docs file directly at runtime.

### Tradeoffs

- Start with model-to-integration wiring before optimizing harness output or report format.
- It may be better to generate a compact obligation ledger artifact at build time rather than parse the huge obligations markdown during each `uv test` run.
- For initial completion, a simple line-oriented harness result protocol may be easier and more robust than a rich structured format.
- Display-name collisions should probably be allowed because stable identity is the fully-qualified procedure path; warn only if the spec later requires uniqueness.

### Open questions

1. What is the intended exact exit-code policy for:
   - zero tests selected,
   - test failures,
   - test errors,
   - discovery diagnostics,
   - harness build failure?
2. Should `uv test .` with no tests be success or failure?
3. Should coverage validation read `Docs/Internal/UltravioletSpecification.obligations.md` directly, or consume a generated obligation index?
4. Where should generated harness files live exactly, and should they be deleted after execution?
5. Is `TestContext` intended to be available only for compiler tests, or for all assemblies that define tests?
6. Should `[[test]]` procedures be allowed outside `Assembly::Tests::*` but simply not selected by `uv test`, or should that be a diagnostic?
7. What report format should be stable for CI consumers?

## Suggested implementation order

1. Fix visible diagnostics for failed `uv test` commands.
2. Add real discovery API and wire it into `TestCommand.uv`.
3. Add source-driven static validation tests for all `E-TST-*` diagnostics.
4. Add obligation ledger lookup for `covers(...)`.
5. Generate harness source files and test their content/order.
6. Compile and invoke harnesses.
7. Parse results and implement pass/fail/error summary reporting.
8. Add end-to-end fixtures and smoke tests.
9. Harden path handling, cleanup generated artifacts, and update comments/docs.

## Definition of done

The work is complete when:

- `uv test .` discovers the repository's source-native tests and prints a deterministic summary.
- `uv test <assembly|module|directory|file>` filters tests according to the spec.
- `uv test NoSuchTarget` emits `E-TST-0108` visibly.
- Invalid `[[test]]` declarations emit the specified diagnostics.
- Valid `covers(...)` references are checked against the obligation ledger; unknown references emit `E-TST-0107`.
- Tests with and without `TestContext` execute correctly.
- Passing, failing, and erroring tests are classified according to `conformance.TestAttributeDynamicSemantics@L28495`.
- Generated harnesses are ephemeral and do not affect production build artifacts.
- The relevant self-hosted conformance tests pass.
