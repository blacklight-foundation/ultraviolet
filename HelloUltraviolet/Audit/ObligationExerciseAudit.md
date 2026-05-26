# Obligation Exercise Audit

This audit records the evidence used to treat HelloUltraviolet as the alpha release
test surface after removing standalone Bootstrap compiler/runtime tests.

## Scope

The generated HelloUltraviolet catalog contains 6052 obligation entries across 603
source paths.

| Catalog exercise kind | Entries | Source paths | Exercise surface |
| --- | ---: | ---: | --- |
| `accepted` | 5413 | `Source/Reference`, `Source/Tests`, and shared fixtures | Compiled source references, source-native test references, and executable symbol checks |
| `referenceModel` | 104 | 2 under `Source/Audit` | Audit reference-model procedures |
| `acceptedProject` | 54 | 7 under `Fixtures/AcceptedProjects` plus 4 under `Fixtures/BootstrapNonCompliance` | Project fixtures and catalog metadata |
| `rejectedSource` | 442 | 409 under `Fixtures/RejectedSource` | Rejected source plus expected diagnostic artifacts |
| `diagnosticSource` | 26 | 25 under `Fixtures/DiagnosticSource` | Diagnostic source plus expected diagnostic artifacts |
| `artifactBehavior` | 13 | 7 under `Fixtures/ArtifactProjects` and `Fixtures/OutputDiagnostics` | Output/artifact checks |

The catalog now includes four exact `Fixtures/BootstrapNonCompliance` project
source paths as `acceptedProject` entries. Their manifests and source files are
also checked by the accepted-project fixture catalog.

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
- `Source/Reference/Authority/IO.uv` now exercises nonrecursive `IO::remove`
  semantics for file removal, empty-directory removal, and
  `IoError::DirectoryNotEmpty` on nonempty directories.
- The IO source exercise exposed and fixed a runtime ABI mismatch: the compiler
  emits value receivers for `File@Read`, `File@Write`, `File@Append`, and
  `DirIter@Open` const receiver methods, while the runtime implementations still
  expected pointer receivers. The runtime ABI now accepts the handle values used
  by generated source code.
- The IO reference uses isolated directories for file-handle and directory-entry
  assertions, and removes its owned paths before creating handles so reruns are
  deterministic.
- `Source/Tests/SourceNativeTests.uv` now includes ordered side-effect `#test`
  procedures covering `def.TestDiscoveryOrder@L28962`.
- `Tools/GenerateHelloCatalog.py` and generated
  `Source/Audit/SymbolExecutions/ReferenceAuthority.uv` now call
  `runAuthorityIOReference(context)` so IO behavior is executed with the release
  context.
- `Source/Api.uv` routes the updated IO reference through the public audit surface.
- The four exact `Fixtures/BootstrapNonCompliance` projects are now generated
  catalog entries, source-path inventory entries, and accepted-project artifact
  checks.

## Reconciliation Outcome

Completed:

- All source-visible assertions from the removed tests have a HelloUltraviolet
  source/fixture/diagnostic/artifact/source-native target.
- The four historical `Fixtures/BootstrapNonCompliance` project paths are active
  generated catalog entries instead of manual-only check-builds.
- Runtime C API details, parser helper index probes, LLVM IR shape probes, and
  backend carrier-layout checks are classified as implementation-internal when
  they do not correspond to a source-level spec obligation.

Verified gates:

- `python3 Tools/ExtractObligationLedger.py --check`: pass, 6052 obligations.
- `python3 Tools/GenerateHelloCatalog.py --check`: pass.
- Linux `uv_out` package target: pass.
- `uv build HelloUltraviolet --target-profile x86_64-sysv --check`: pass,
  14 warnings and 10 infos.
- `uv build HelloUltraviolet --target-profile x86_64-sysv`: pass, 14 warnings
  and 10 infos.
- `HelloUltraviolet`: pass, exit code 0.
- `HelloUltraviolet --audit`: pass, exit code 0.
- `uv test HelloUltraviolet --target-profile x86_64-sysv`: pass.
- Windows `windows-release-package` target through Visual Studio Developer
  PowerShell: pass.
- Windows `uv.exe build HelloUltraviolet --target-profile x86_64-win64 --check`:
  pass, 14 warnings and 10 infos.
- Windows `uv.exe build HelloUltraviolet --target-profile x86_64-win64`: pass,
  14 warnings and 10 infos.
- Windows `HelloUltraviolet.exe`: pass, exit code 0.
- Windows `HelloUltraviolet.exe --audit`: pass, exit code 0.
- Windows `uv.exe test HelloUltraviolet --target-profile x86_64-win64`: pass.

Remaining release-hardening item from this audit:

- None.
