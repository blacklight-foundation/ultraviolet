# Changelog

## Unreleased

- Added source-native diagnostic fixture execution checks so rejected-source and
  diagnostic-source coverage verifies real compiler results, not only fixture
  metadata.
- Added source-native output-diagnostic fixture execution checks for command-line,
  project-manifest, and LLVM output failure diagnostics.
- Fixed unknown `uv` command handling so command-line parsing emits `E-CLI-0001`
  instead of treating the unknown command as a build input path.

## 0.1.0-alpha - 2026-05-27

- Prepared Linux and Windows public-alpha verification around the
  HelloUltraviolet conformance suite.
- Published the `v0.1.0-alpha` public alpha release for Linux x86_64, Apple
  Silicon macOS, and Windows x86_64.
- Recorded passing Linux, macOS, and Windows HelloUltraviolet release payload
  verification.
- Added `Tools/RunHelloVerification.py` to run the alpha conformance gates and emit a
  timestamped, commit-bound, SHA-256-hashed transcript.
- Added contribution, security, code-of-conduct, support, issue-template,
  pull-request-template, and discussion-template guidance for public alpha.
- Added GitHub Actions workflows for Linux and Windows HelloUltraviolet verification
  runs, including transcript artifact upload.
- Added README branding, public-alpha support routing, sponsorship metadata, and
  primary verification-runner conformance instructions.
- Added deterministic Linux host-tool executable-bit repair during CMake
  configure for vendored LLVM tools.
- Promoted BootstrapNonCompliance regression fixtures into the active
  HelloUltraviolet accepted-project catalog surface.
- Fixed Windows output artifact path handling for long filenames and shortened
  source-native test harness build paths.
- Fixed runtime ABI handling for generated IO, memory, string, time, and context
  calls exercised by HelloUltraviolet.
- Fixed macOS verification ordering so release artifact validation runs after
  macOS fixture artifacts are built.
- Fixed Windows installer checksum verification on hosts where `Get-FileHash`
  is unavailable.
- Preserved LF checkouts for release CSV and TXT audit files so Windows
  verification remains byte-exact.
- Accepted the Linux SysV LLVM IR carrier shape emitted for runtime IO handle
  creation.
- Corrected public build and conformance documentation for current CMake output
  paths on Linux and Windows.
