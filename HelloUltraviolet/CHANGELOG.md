# HelloUltraviolet Changelog

## Unreleased

- No unreleased changes.

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
