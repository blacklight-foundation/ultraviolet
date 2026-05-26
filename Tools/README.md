# Ultraviolet Tools

This directory contains repository-maintained scripts used by contributors,
release maintainers, and public alpha installers.

## Verification Helpers

`RunHelloVerification.py` drives the public conformance verification for each supported
target profile. Platform release artifact checks run through
`VerifyReleaseArtifacts.py`: all platforms validate the package-stage inputs
declared by `PackageRelease.py`, and `aarch64-darwin` additionally validates
Mach-O arm64 outputs, packaged LLVM sidecar tools, bundled dylib load names, and
required LC_RPATH entries after the macOS HelloUltraviolet build. The macOS
artifact verifier also checks the fixture executable, static archive, hosted
dylib, and external dylib import outputs. For the hosted dylib, it creates a
hosted session, calls the exported fixture procedure, destroys the session, and
verifies the handle is no longer live. Every target verification creates that
platform's public release archive, installs the local archive into a probe
directory, and runs the installed `uv` and `uvc` help commands.

`FetchTargetExterns.py` restores only the extern archive needed by one target
profile. Use it before local verifications when the vendored LLVM and ICU roots
are not present:

```bash
python3 Tools/FetchTargetExterns.py --target-profile aarch64-darwin
```

The archive URLs, optional split archive parts, expected SHA-256 hashes,
extraction roots, and required files are tracked in
`Bootstrap/extern/ExternManifest.json`. Set
`--base-url file:///path/to/archive-directory` to use a local archive mirror.

## Public Installers

- `InstallUltraviolet.sh`: Linux and Apple Silicon macOS installer.
- `InstallUltraviolet.ps1`: Windows-only PowerShell installer.

The root `install.sh` and `install.ps1` files are thin launchers that fetch and
run these Tools-owned installers from the selected repository ref. Set
`ULTRAVIOLET_INSTALL_REF` to install from a branch or tag other than `main`.

Default install behavior:

- Install the compiler command as `uv`.
- Add the shim directory to the user `PATH`.
- Ask before replacing an existing Python `uv` command.
- Use `uvc` for noninteractive conflict installs unless the command mode is
  specified.

Recommended interactive conflict behavior:

- Install Ultraviolet as `uv`.
- Preserve the existing Python `uv` command as `pyuv`.

Useful options:

```bash
Tools/InstallUltraviolet.sh --use-uv
Tools/InstallUltraviolet.sh --no-path
Tools/InstallUltraviolet.sh --version <tag>
Tools/InstallUltraviolet.sh --from-local Build/Release/ultraviolet-linux-x86_64.tar.gz \
  --checksum-file Build/Release/ultraviolet-linux-x86_64.tar.gz.sha256
# macOS local archive:
# Tools/InstallUltraviolet.sh --from-local Build/Release/ultraviolet-macos-aarch64.tar.gz \
#   --checksum-file Build/Release/ultraviolet-macos-aarch64.tar.gz.sha256
```

```powershell
.\Tools\InstallUltraviolet.ps1 -UseUv
.\Tools\InstallUltraviolet.ps1 -NoPath
.\Tools\InstallUltraviolet.ps1 -Version <tag>
.\Tools\InstallUltraviolet.ps1 -FromLocal Build\Release\ultraviolet-windows-x86_64.zip `
  -ChecksumFile Build\Release\ultraviolet-windows-x86_64.zip.sha256
```

## Release Packaging

`PackageRelease.py` creates durable per-platform staging trees and the public
archives consumed by the installers:

```bash
python3 Tools/PackageRelease.py --check-release-assets
python3 Tools/PackageRelease.py --platform all
```

By default, release content is staged under `Build/Staging/<platform>` and then
bundled into archives under `Build/Release`. `PackageRelease.py` copies only its
manifest-listed payload entries, so stale build outputs, debug helpers, and
foreign-platform files in the CMake `out` tree are not release content. Archive
metadata is normalized so unchanged payload bytes produce stable release hashes.
The release asset check verifies that installer asset names still match the
package manifest before the archives are staged and bundled.
Local archive install probes pass the generated `.sha256` sidecars into the
installers, so verification covers both public release assets for each platform.
The staging platform directory is the release payload root for inspection. It
does not contain an extra `ultraviolet` wrapper directory, and platform support
content is not nested under a second platform directory.

macOS release packaging expects the staged package to contain tools and dylibs
copied from the extern archive roots under
`Bootstrap/extern/llvm/llvm-21.1.8-aarch64-darwin` and
`Bootstrap/extern/icu/macos`.

Expected staging roots:

- `Build/Staging/linux`
- `Build/Staging/macos`
- `Build/Staging/windows`

Expected outputs:

- `Build/Release/ultraviolet-linux-x86_64.tar.gz`
- `Build/Release/ultraviolet-linux-x86_64.tar.gz.sha256`
- `Build/Release/ultraviolet-macos-aarch64.tar.gz`
- `Build/Release/ultraviolet-macos-aarch64.tar.gz.sha256`
- `Build/Release/ultraviolet-windows-x86_64.zip`
- `Build/Release/ultraviolet-windows-x86_64.zip.sha256`
