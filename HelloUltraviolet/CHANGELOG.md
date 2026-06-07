# HelloUltraviolet Changelog

## Unreleased

- No unreleased changes.

## 0.3.0-alpha - 2026-06-05

- Corrected `if case` pattern binding ownership lowering so non-move scrutinees do
  not transfer payload responsibility to derived bindings.
- Added target-profile-aware HUV reference execution for Windows and POSIX
  command, artifact, and diagnostic expectations.
- Corrected POSIX runtime file and directory handling for release verification,
  including share-mode conflicts and directory-open rejection.
- Completed rejected-source catalog self-consistency corrections for the
  compile-time procedure contract diagnostic fixture.

## 0.2.0-alpha - 2026-06-04

- Expanded the source-native audit corpus so obligation exercises execute real
  accepted-project, rejected-source, diagnostic-source, artifact-project, and
  output-diagnostic compiler paths.
- Added artifact-project coverage for runtime interface symbols, external ABI
  forms, LLVM IR/bitcode emission, panic behavior, structured parallel lowering,
  module discovery ordering, and shared-library lifecycle checks.
- Corrected target-aware audit expectations for Windows and Linux aggregate ABI
  lowering, runtime call signatures, executable artifact output, and output
  pipeline diagnostics.
- Added compiler-conformance catalog execution entries for compile-time
  execution, key-system behavior, procedures and contracts, structured
  parallelism, type-system core behavior, and lowering evidence.

## 0.1.0-alpha - 2026-05-27

- Documented Apple Silicon macOS verification, build, executable, and source-native
  test commands for the `aarch64-darwin` target profile.
- Recorded the alpha release audit state: 6052 generated obligation entries
  across 603 catalog source paths.
- Recorded passing Linux, macOS, and Windows release payload verification.
- Promoted four historical `Fixtures/BootstrapNonCompliance` projects into the
  active accepted-project catalog surface:
  `ClassDefaultMethodFieldAccess`, `GenericClassBoundMethodLookup`,
  `GenericRecordLiteralExpectedType`, and
  `FreeProcedureOverloadResolution`.
- Added accepted-project source and manifest artifact checks for the promoted
  BootstrapNonCompliance fixtures.
- Regenerated catalog source paths and symbol execution files after the fixture
  promotion.
- Preserved LF checkout behavior for generated CSV and TXT audit inputs so the
  Windows conformance executable observes byte-exact repository data.
- Accepted the Linux SysV `{ i64, i64 }` LLVM IR carrier return used by runtime
  IO handle creation in release artifact audits.
- Documented `Tools/RunHelloVerification.py` as the preferred public-alpha release
  verification path.
