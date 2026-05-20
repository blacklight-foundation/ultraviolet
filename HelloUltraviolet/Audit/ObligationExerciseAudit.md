# Obligation Exercise Audit

This audit records the evidence used to treat HelloUltraviolet as the alpha release
test surface after removing standalone Bootstrap compiler/runtime tests.

## Scope

The generated HelloUltraviolet catalog contains 6047 obligation entries across 598
source paths.

| Catalog kind | Entries | Source paths | Exercise surface |
| --- | ---: | ---: | --- |
| `accepted` | 5414 | 147 under `Source/Reference` plus shared fixtures | Compiled source references and executable symbol checks |
| `referenceModel` | 104 | 2 under `Source/Audit` | Audit reference-model procedures |
| `acceptedProject` | 50 | 7 under `Fixtures/AcceptedProjects` | Project fixtures and catalog metadata |
| `rejectedSource` | 442 | 409 under `Fixtures/RejectedSource` | Rejected source plus expected diagnostic artifacts |
| `diagnosticSource` | 26 | 25 under `Fixtures/DiagnosticSource` | Diagnostic source plus expected diagnostic artifacts |
| `artifactBehavior` | 11 | 7 under `Fixtures/ArtifactProjects` and `Fixtures/OutputDiagnostics` | Output/artifact checks |
| `#test` source | 9 | 1 under `Source/Tests` | Source-native `uv test` procedures |

The catalog has no entries under `Fixtures/BootstrapNonCompliance`. Four exact
BootstrapNonCompliance fixtures were manually check-built during this audit and are
listed in `StandaloneTestReconciliation.md`.

## Validation Method

- `Tools/ExtractObligationLedger.py --check` verifies the public/internal spec
  obligation ledger before catalog validation.
- `Tools/GenerateHelloCatalog.py --check` verifies generated catalog source is
  current.
- `HelloUltraviolet/Audit/CatalogSourcePaths.txt` is the generated inventory of
  catalog source paths. The runtime audit reads those paths through
  `catalogSourcePathsExist`.
- `catalogCompiledSymbolTargetsAreIndexed` verifies catalog symbol membership, and
  `catalogCompiledSymbolsExecute(context)` invokes the compiled reference/audit
  symbol surface rather than treating source paths as coverage.
- Rejected, diagnostic, accepted-project, and artifact entries are tied to fixture
  source and expected artifacts through the generated fixture catalog.
- The reference-source placeholder scan covered all 155 files under
  `Source/Reference`: no `run*Reference` procedure is a direct `return true`.
  The only direct `-> bool { return true }` helper is `ModalReference@Open.isOpen`,
  which is a state predicate exercised by `runModalTypesModalDeclarationsReference`.
- Low-signal candidates and direct `return true` sites were opened and inspected.
  The only coverage fix required by that inspection was `Authority/IO.uv`, which
  now performs real `Context.io` operations.

## Fixes From This Audit

- `Source/Reference/Authority/IO.uv` now exercises restricted paths, invalid path
  diagnostics, missing-path diagnostics, file writes, append, flush, read handles,
  byte reads, kind checks, directory iteration, EOF, snapshot-after-removal, and
  cleanup through `Context.io`.
- The IO source exercise exposed and fixed a runtime ABI mismatch: the compiler
  emits value receivers for `File@Read`, `File@Write`, `File@Append`, and
  `DirIter@Open` const receiver methods, while the runtime implementations still
  expected pointer receivers. The runtime ABI now accepts the handle values used
  by generated source code.
- The IO reference uses isolated directories for file-handle and directory-entry
  assertions, and removes its owned paths before creating handles so reruns are
  deterministic.
- `Source/Tests/SourceNativeTests.uv` now includes ordered side-effect `#test`
  procedures covering `def.TestDiscoveryOrder@L28866`.
- `Tools/GenerateHelloCatalog.py` and generated
  `Source/Audit/SymbolExecutions/ReferenceAuthority.uv` now call
  `runAuthorityIOReference(context)` so IO behavior is executed with the release
  context.
- `Source/Api.uv` routes the updated IO reference through the public audit surface.
- `StandaloneTestReconciliation.md` maps all 32 removed standalone test files to
  HelloUltraviolet source, fixtures, diagnostic artifacts, artifact checks,
  source-native `#test` procedures, or implementation-internal probes.

## Reconciliation Outcome

Completed:

- All 32 removed standalone test files are classified in
  `StandaloneTestReconciliation.md`.
- All source-visible assertions from the removed tests have a HelloUltraviolet
  source/fixture/diagnostic/artifact/source-native target.
- Runtime C API details, parser helper index probes, LLVM IR shape probes, and
  backend carrier-layout checks are classified as implementation-internal when
  they do not correspond to a source-level spec obligation.

Verified gates:

- `python3 Tools/ExtractObligationLedger.py --check`: pass, 6047 obligations.
- `python3 Tools/GenerateHelloCatalog.py --check`: pass.
- Windows `uv_out` target: pass; staged fixed
  `Bootstrap/Ultraviolet/build/windows/out/UltravioletRT.lib`.
- `uv.exe build HelloUltraviolet --check`: pass, 14 warnings and 10 infos.
- `uv.exe build HelloUltraviolet`: pass, 14 warnings and 10 infos.
- `HelloUltraviolet.exe`: pass, exit code 0.
- `HelloUltraviolet.exe --audit`: pass, exit code 0.
- `uv.exe test HelloUltraviolet`: pass.
- `uv.exe test HelloUltraviolet --test "named source-native reference"`: pass.
- `uv.exe test HelloUltraviolet --coverage def.TestDiscoveryOrder@L28866`: pass.
- `uv.exe test HelloUltraviolet/Source/Tests/SourceNativeTests.uv`: pass.

Remaining release-hardening item:

- The four exact `Fixtures/BootstrapNonCompliance` projects listed in
  `StandaloneTestReconciliation.md` check-build manually, but they are not yet
  generated catalog entries. Promote them into an active generated fixture
  catalog if release policy requires those exact historical paths to be proven by
  the normal HelloUltraviolet executable gate.
