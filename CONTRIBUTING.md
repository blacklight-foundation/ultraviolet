# Contributing to Ultraviolet

Ultraviolet is in public alpha. Contributions should preserve alignment between
the language specification, bootstrap compiler, runtime, generated obligation
catalog, and HelloUltraviolet conformance suite.

## Sources Of Truth

- `Docs/SPECIFICATION.md` is the normative language specification.
- `Docs/Internal/UltravioletObligations.csv` is the generated obligation ledger.
- `HelloUltraviolet` is the release conformance and test suite for compiler and
  runtime behavior.
- The bootstrap compiler and runtime implement the specification. Existing
  implementation behavior is not a substitute for a specification rule.

## Contribution Scope

Documentation fixes, diagnostic improvements, conformance additions, packaging
fixes, runtime fixes, compiler fixes, and compiler performance improvements are
welcome.

Compiler performance optimizations are allowed and encouraged when they preserve
the observable behavior required by the specification. An optimization must not
change parsing, name resolution, type checking, diagnostics, lowering, runtime
authority, ABI behavior, output behavior, or program execution in a way that
violates the specification. If an optimization exposes a needed semantic change,
resolve the specification and conformance coverage first.

During public alpha, feature proposals and semantic changes should start as
GitHub Discussions before implementation. Narrow bug fixes, conformance coverage,
diagnostic improvements, documentation fixes, packaging fixes, and
semantics-preserving implementation changes may be proposed directly as pull
requests.

## Specification Change Requirements

Specification changes have stricter review requirements than compiler-only
updates. A normative specification change usually cascades into the bootstrap
compiler, runtime, generated obligation ledger, and HelloUltraviolet conformance
surface. Do not treat a semantic spec edit as complete until the implementation
and conformance consequences are accounted for.

Every normative specification change must include a justification that explains:

1. What intended Ultraviolet design goal the change better serves.
2. How the change materially improves Ultraviolet for code written, generated, or
   reviewed by AI systems.
3. What research, experiments, examples, prototypes, comparative language study,
   user evidence, or implementation experience supports those claims.
4. What alternatives were considered and why the proposed rule is the better
   fit for Ultraviolet.

Every normative specification change must also include an explicit cascade
inventory. The inventory must list every compiler, runtime, tool, diagnostic,
and HelloUltraviolet area that needs to change or be verified. Mark each item as
one of:

- `completed`: changed or verified in the pull request.
- `intentionally unchanged`: inspected and left unchanged with a stated reason.
- `remaining`: required before the change can merge as an implementation.
- `blocked`: blocked by a concrete dependency or maintainer decision.
- `proposal-only`: not part of an implementation pull request.

At minimum, consider whether the change affects:

- Public specification text in `Docs/SPECIFICATION.md`.
- Internal obligation markers and `Docs/Internal/UltravioletObligations.csv`.
- Lexing, parsing, AST representation, name resolution, type checking,
  contracts, lowering, code generation, output behavior, or CLI/project model.
- Runtime C API behavior, ABI rules, authority boundaries, FFI, or target
  profile support.
- Target, platform, ABI, object-format, or linker changes that require
  coordinated Linux, macOS, Windows, compiler, runtime, package tooling, and
  HelloUltraviolet updates.
- Diagnostic codes, diagnostic wording, warning/error severity, or diagnostic
  fixture expectations.
- HelloUltraviolet reference source, source-native `#test` coverage, accepted
  project fixtures, rejected/diagnostic fixtures, artifact checks, generated
  catalog entries, source path inventory, and symbol execution files.
- Documentation, examples, changelogs, migration notes, or public-alpha release
  notes.

If the spec change is exploratory, keep it in a GitHub Discussion or proposal
document until the cascade inventory is clear. A pull request that changes
normative semantics without the corresponding compiler and HelloUltraviolet work
is a design proposal, not a complete implementation change.

Implementation pull requests cannot merge with required cascade work still marked
`remaining`, `blocked`, or `proposal-only`.

## Compiler Change Requirements

Every compiler change pull request must include a HelloUltraviolet verification from
`Tools/RunHelloVerification.py`. The verification shows the commit, UTC timestamp, host
platform, target profile, command exit codes, PASS/FAIL result, and SHA-256 hash
of the transcript. It also proves conformance is still maintained and all
generated specification obligations are still represented.

For compiler behavior changes, the pull request must state:

1. The exact current behavior being changed.
2. The specification rule, inference, judgement, or normative requirement that
   necessitates or permits the change.
3. The exact implementation behavior that is changing.
4. The HelloUltraviolet coverage or verification proving the resulting behavior is
   conformant.

For compiler performance changes, the pull request must state:

1. What work is being optimized.
2. Why the optimization is semantics-preserving under the specification.
3. The `Tools/RunHelloVerification.py` result after the change.
4. Any benchmark, profiling, or size evidence used to justify the optimization.

Performance evidence must include the baseline commit, optimized commit, host
operating system, target profile when relevant, exact command or workload,
timing/profiling method, number of runs or sampling method, and result summary.
Performance evidence never replaces the HelloUltraviolet conformance verification.

Do not change compiler behavior because the current implementation seems
inconvenient or because a different behavior seems preferable. If the
specification is unclear, open a discussion or spec clarification before changing
the compiler.

## Design Expectations

Ultraviolet code should express correctness in the language surface where
possible. Prefer types, modal state, contracts, invariants, and narrow
capabilities before weaker runtime-only validation.

Keep authority narrow. Pass only the capabilities and data that a procedure or
method actually uses. Avoid broad context objects except at real subsystem
boundaries where a narrow projected context type is justified.

Treat `unsafe` and `#dynamic` as deliberate boundary tools, not convenience
escapes. APIs should stay small, explicit, stable, and easy to review.

## Conformance Coverage

Standalone Bootstrap compiler or runtime test programs should not be added for
language behavior coverage. New behavioral coverage belongs in HelloUltraviolet
as one of:

- Reference source under `HelloUltraviolet/Source/Reference`.
- Source-native `#test` procedures.
- Accepted project fixtures.
- Rejected or diagnostic source fixtures.
- Output or artifact checks.
- Generated catalog and symbol execution coverage.

When changing the conformance surface, update generated catalog files and record
the change in `HelloUltraviolet/CHANGELOG.md`.

## Required Gates

Do not use `ctest` as a release conformance gate. HelloUltraviolet is the test
suite for compiler and runtime behavior.

Before submitting compiler, runtime, specification, or conformance changes, run
the verification runner from the repository root. On Linux:

```bash
python3 Tools/RunHelloVerification.py --target-profile x86_64-sysv
```

On Apple Silicon macOS:

```bash
python3 Tools/RunHelloVerification.py --target-profile aarch64-darwin
```

On Windows, run the verification from an actual Windows Visual Studio Developer
PowerShell, not from WSL:

```powershell
py -3 Tools\RunHelloVerification.py --target-profile x86_64-win64
```

The verification runner executes:

- `Tools/ExtractObligationLedger.py --check`
- `Tools/GenerateHelloCatalog.py --check`
- The platform release package build.
- `uvc build HelloUltraviolet --check`.
- `uvc build HelloUltraviolet`.
- `HelloUltraviolet`.
- `HelloUltraviolet --audit`.
- `uvc test HelloUltraviolet`.

The verification must end with `Verification result: PASS` and a
`Verification transcript SHA256:` line. Include the verification text or CI artifact URL in
the pull request.

The verification must be from the commit under review. The `Commit:` line must match
the pull request head commit. `Worktree status:` should be `clean`; if it is
`dirty` or `unknown`, explain why the verification is still valid or rerun after
committing the relevant files. CI verifications are preferred when available because
the hosted run binds the transcript to the pull request metadata.

The equivalent Windows gates, if run manually for diagnosis, are:

```powershell
cd C:\Dev\Ultraviolet\Bootstrap\Ultraviolet
cmake --preset windows-release
cmake --build --preset windows-release-package
cd C:\Dev\Ultraviolet

.\Bootstrap\Ultraviolet\build\windows\out\uvc.exe build .\HelloUltraviolet `
    --target-profile x86_64-win64 --check
.\Bootstrap\Ultraviolet\build\windows\out\uvc.exe build .\HelloUltraviolet `
    --target-profile x86_64-win64
.\HelloUltraviolet\Build\Binary\HelloUltraviolet.exe
.\HelloUltraviolet\Build\Binary\HelloUltraviolet.exe --audit
.\Bootstrap\Ultraviolet\build\windows\out\uvc.exe test .\HelloUltraviolet `
    --target-profile x86_64-win64
```

If you cannot run a relevant platform gate, state that explicitly in the pull
request. Public-alpha release readiness cannot be claimed until the relevant
Linux, macOS, and Windows gates pass.

## Pull Request Expectations

Keep pull requests focused. Avoid unrelated formatting, generated-output churn,
vendored payload changes, or line-ending churn.

Include the generated files that correspond to any generator input change. For
catalog changes, include the updated HelloUltraviolet generated catalog sources,
source path inventory, and symbol execution files.

For bug reports and pull requests, include the host operating system, target
profile, compiler commit, exact commands, observed behavior, expected behavior,
and relevant diagnostic output.

Compiler bug reports should also include a minimal `.uv` reproduction or the
exact HelloUltraviolet fixture path that reproduces the issue. If a minimized
reproduction is not available, include the smallest project or source file known
to trigger the behavior.

Before submitting a pull request, check that:

- Specification impact is stated.
- Specification cascade inventory is included when normative semantics change.
- Compiler behavior or performance evidence is included when compiler behavior
  changes.
- `Tools/RunHelloVerification.py` output or CI artifact URL is attached.
- Dirty or unknown verification worktree state is explained.
- Generated files are updated when generator inputs or catalog coverage change.
- Platform gates that were not run are explicitly listed.
- No unrelated formatting, line-ending, vendored-payload, or generated-output
  churn is included.

## Security And Conduct

Report suspected security vulnerabilities through GitHub Private Vulnerability
Reporting. Do not open public issues or discussions containing vulnerability
details. See `SECURITY.md`.

Participants are expected to follow `CODE_OF_CONDUCT.md`. Conduct reports go to
`@cursivecrow`.
