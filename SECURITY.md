# Security Policy

## Reporting Security Vulnerabilities

Report suspected security vulnerabilities through GitHub Private Vulnerability
Reporting for this repository.

Do not open a public issue, pull request, discussion, or chat message that
contains vulnerability details. Public channels are appropriate for ordinary
bugs, build failures, diagnostics, documentation issues, and feature requests,
but not for exploit details or reports that need coordinated disclosure.

Maintainers handle accepted reports through GitHub repository security
advisories.

## Public Alpha Scope

Ultraviolet is in public alpha. This policy is an intake and coordinated
disclosure policy for repository and toolchain vulnerabilities. It is not a
general security guarantee for the language, compiler, runtime, or programs
built with them.

Security reports are in scope when they concern vulnerabilities in this
repository or its published alpha artifacts, including:

- Compiler behavior triggered by malicious `.uv` source, manifests, or project
  layouts that can execute unintended code, escape intended filesystem roots, or
  corrupt files outside expected build outputs.
- Runtime C API, ABI, memory-safety, authority-boundary, or filesystem behavior
  in the shipped runtime.
- Build, package, release, or vendored-toolchain behavior that can cause the
  wrong tool or payload to be trusted or executed.
- Secrets, credentials, private keys, or sensitive release material committed to
  the repository or emitted by project tooling.
- Denial-of-service inputs that make the compiler, runtime, or test harness
  consume unreasonable time, memory, or disk space.

Out of scope for this security policy:

- Ordinary alpha language-design changes, diagnostics, or compiler correctness
  bugs that do not create a security boundary violation.
- Claims that arbitrary untrusted Ultraviolet projects are sandboxed.
- Vulnerabilities in downstream applications that use Ultraviolet unless the
  root cause is in this repository.

## Supported Versions

During public alpha, only the current public repository state and current
published alpha artifacts are considered for security triage. Older snapshots,
forks, local modifications, and unpublished builds are not supported unless a
maintainer explicitly marks them as supported.

Include the affected commit, tag, build artifact, host operating system, target
profile, and reproduction steps in the private report when possible.
