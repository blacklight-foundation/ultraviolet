## Summary

Describe the change and why it is needed.

## Change Type

- [ ] Documentation only
- [ ] Build or packaging
- [ ] Runtime
- [ ] Compiler behavior
- [ ] Compiler performance
- [ ] HelloUltraviolet conformance coverage
- [ ] Specification change

## Specification Impact

State whether this changes normative specification behavior.

## Compiler Change Support

Every compiler change must identify the specification support for the new
behavior. Existing implementation behavior is not a substitute for a
specification rule.

For compiler behavior changes, include:

1. Exact current compiler behavior being changed.
2. Specification rule, inference, judgement, obligation ID, diagnostic rule, or
   normative text that requires or permits the new behavior.
3. Exact implementation behavior changing.
4. How the change restores or preserves specification conformance.
5. HelloUltraviolet evidence proving the resulting behavior is conformant.

For compiler bug fixes, explain what area of the specification the current
behavior violates. That specification non-conformance is what makes it a compiler
bug.

## Compiler Performance Evidence

For compiler performance changes, include:

- Work being optimized.
- Why the optimization is semantics-preserving under the specification.
- Baseline commit.
- Optimized commit.
- Host operating system.
- Target profile, when relevant.
- Exact command or workload.
- Timing, profiling, or size method.
- Number of runs or sampling method.
- Result summary.

Performance evidence does not replace the HelloUltraviolet conformance verification.

## Specification Change Justification

For normative specification changes, explain:

- Intended Ultraviolet design goal served.
- How this improves the language for code written, generated, or reviewed by AI.
- Research, experiments, examples, prototypes, comparative study, user evidence,
  or implementation experience supporting the change.
- Alternatives considered and why the proposed rule is the better fit.

## Cascade Inventory

For each affected area, state one of: `completed`, `intentionally unchanged`,
`remaining`, `blocked`, or `proposal-only`.

- [ ] Specification text
- [ ] Obligation ledger
- [ ] Compiler
- [ ] Runtime / ABI
- [ ] Diagnostics
- [ ] HelloUltraviolet reference coverage
- [ ] HelloUltraviolet fixtures
- [ ] Generated catalog / symbol execution files
- [ ] Documentation / changelog / release notes

Inventory details:

```text
area: status - evidence or reason
```

Implementation pull requests cannot merge with required cascade work still marked
`remaining`, `blocked`, or `proposal-only`.

## HelloUltraviolet Verification

Command:

```text
paste command here
```

Result:

```text
Verification result: PASS
Verification transcript SHA256: paste hash here
```

- [ ] Linux `x86_64-sysv` verification passed.
- [ ] macOS `aarch64-darwin` verification passed on Apple Silicon.
- [ ] Windows `x86_64-win64` verification passed from actual Windows Visual Studio
      Developer PowerShell.
- [ ] Any skipped gate is explained.
- [ ] Dirty or unknown verification worktree state is explained, or the verification was
      rerun after committing the relevant files.

## Generated Files

- [ ] Generated files are updated where required.
- [ ] No unrelated generated-output churn is included.

## Security

- [ ] This pull request does not disclose a security vulnerability publicly.
- [ ] Security-sensitive behavior has been reviewed, if applicable.

## Additional Notes

Add any reviewer context that does not fit above.
