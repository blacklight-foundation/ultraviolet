# HelloUltraviolet Conformance Suite

HelloUltraviolet is the reference corpus and conformance suite for validating
Ultraviolet language behavior against the canonical repo obligation ledger at
[`Docs/Internal/UltravioletObligations.csv`](../Docs/Internal/UltravioletObligations.csv).

---

## Layout

- `Source/Reference/`: Contains direct Ultraviolet source exercises for language constructs, runtime behavior, authority, memory ownership, typestates/modal types, and project semantics.
- `Source/Audit/FixtureCatalog/`: Contains compiled fixture indexes and artifact verifiers used by the executable corpus.
- `Source/Audit/Catalog/`: Maps each obligation row in `UltravioletObligations.csv` to the source exercise, fixture, or reference surface that covers it.
- `Source/Audit/SymbolExecutions/`: Groups compiled symbol execution checks by reference or fixture responsibility.
- `Source/Audit/`: Runtime audit checks that prove the generated catalog, fixture references, and compiled symbol surface are correctly wired.
- `Fixtures/`: Physical fixture projects, rejected source files, diagnostics, and expected compiler outputs read by the corpus at runtime.
- `Audit/`: Project-local manifests, non-compliance notes, and clarification ledgers.

The entry point is `Source/Main.uv`. A clean run exits `0`. Failing checks print the failing reference or missing artifact path before returning a non-zero exit code.

---

## Running the Conformance Suite

HelloUltraviolet is the release test surface for the Ultraviolet compiler and
runtime. You can build it, run the executable checks, and run source-native unit
tests.

For public-alpha release verification, prefer the repository-root verification
runner:

```bash
python3 Tools/RunHelloVerification.py --target-profile x86_64-sysv
```

On Windows, run from an actual Windows Visual Studio Developer PowerShell:

```powershell
py -3 Tools\RunHelloVerification.py --target-profile x86_64-win64
```

On Apple Silicon macOS:

```bash
python3 Tools/RunHelloVerification.py --target-profile aarch64-darwin
```

The macOS verification builds the macOS artifact fixture projects, then includes
`file`, `otool -L`, and `otool -l` checks for Mach-O arm64 artifacts, packaged
LLVM sidecar tools, bundled dylib load names, required LC_RPATH entries, and
the macOS fixture executable, static archive, hosted dylib, and external dylib
import outputs. For the hosted dylib, it creates a hosted session, calls the
exported fixture procedure, destroys the session, and verifies the handle is no
longer live. It also packages the macOS release archive, installs that local
archive into a probe directory, and runs the installed `uv` and `uvc` help
commands.

### 1. Build the Suite
Build the project using the bootstrap `uv` compiler, specifying the target profile:
```bash
uv build HelloUltraviolet --target-profile x86_64-sysv
# macOS: uv build HelloUltraviolet --target-profile aarch64-darwin
# Windows: uv build HelloUltraviolet --target-profile x86_64-win64
```

### 2. Run the Corpus Executable
The built executable is emitted under `HelloUltraviolet/Build/Binary/` when the
suite is built from the workspace root.

> [!IMPORTANT]
> **Working Directory Restriction**: You must run the compiled executable from the **workspace root directory** (the parent directory of `HelloUltraviolet/`). If run from inside the binary output directory or from within `HelloUltraviolet/`, the relative path fixture and log checks will fail.

```bash
# Run from the workspace root
./HelloUltraviolet/Build/Binary/HelloUltraviolet
# macOS: ./HelloUltraviolet/Build/Binary/HelloUltraviolet
# Windows: .\HelloUltraviolet\Build\Binary\HelloUltraviolet.exe

# Run with full audit validation
./HelloUltraviolet/Build/Binary/HelloUltraviolet --audit
# macOS: ./HelloUltraviolet/Build/Binary/HelloUltraviolet --audit
# Windows: .\HelloUltraviolet\Build\Binary\HelloUltraviolet.exe --audit
```

### 3. Run Source-Native Tests
You can execute source-native unit tests (defined with `#test`) directly using the `uv test` command:
```bash
# Run all tests
uv test HelloUltraviolet --target-profile x86_64-sysv
# macOS: uv test HelloUltraviolet --target-profile aarch64-darwin

# Run specific tests matching a name pattern
uv test HelloUltraviolet --target-profile x86_64-sysv --test <name>

# Run tests covering a specific obligation anchor
uv test HelloUltraviolet --target-profile x86_64-sysv --coverage <anchor>

# Run tests in a specific file
uv test HelloUltraviolet/Source/Tests/SourceNativeTests.uv --target-profile x86_64-sysv
```

---

## Adding Coverage

Standalone Bootstrap compiler or runtime test programs should not be added for behavioral coverage. New behavioral coverage belongs in this corpus as reference source files, fixtures, diagnostics, artifact checks, or source-native `#test` procedures.

* Modifications, additions, and removals from this testing suite should be recorded in `CHANGELOG.md`
* The obligation exercise audit is recorded in `Audit/ObligationExerciseAudit.md`.
