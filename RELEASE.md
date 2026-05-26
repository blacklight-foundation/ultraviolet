# Public Alpha Release Checklist

Use this checklist before publishing a public alpha release.

## v0.1.0-alpha Published Release Evidence

- Release URL:
  <https://github.com/blacklight-foundation/ultraviolet/releases/tag/v0.1.0-alpha>
- Release commit: the `v0.1.0-alpha` tag target.
- Release tag: `v0.1.0-alpha`
- Extern payload tag: `externs-llvm21.1.8-icu72`
- Release maturity: public alpha, not production-stable.
- Supported target profiles: `x86_64-sysv`, `aarch64-darwin`, and
  `x86_64-win64`.

Final HelloUltraviolet verification:

These runs verified the release compiler/package payload before the final
documentation-only amend that records this release evidence.

- Linux workflow run `26482687993`: `Verification result: PASS`, transcript
  SHA-256 `c3b3544cea9289890be4465248e7260776a777de3a89d92264e1171f307de61c`
- macOS workflow run `26482688947`: `Verification result: PASS`, transcript
  SHA-256 `68bb3c5b56481409b521cf98750ae4c6d3ab9e5c0b81e95b548826307d0b2a2a`
- Windows workflow run `26482688029`: `Verification result: PASS`, transcript
  SHA-256 `00887373775295ce98bbd75b93da46b6a7629fead8631ec3251855d537cdec17`

Attached release assets:

- `ultraviolet-linux-x86_64.tar.gz`
  SHA-256 `8b95882a9c2e938fd9bdbaa0a1a714ab484ced0a5095275d65410eab055f3ee3`
- `ultraviolet-linux-x86_64.tar.gz.sha256`
- `ultraviolet-macos-aarch64.tar.gz`
  SHA-256 `859e084c128de1b0ca9fb40d1e7a18d2de2a49f7a5023103df311d32d19c11f5`
- `ultraviolet-macos-aarch64.tar.gz.sha256`
- `ultraviolet-windows-x86_64.zip`
  SHA-256 `ea6694d0c35e85a7cc74da8b7514f905e240da08f80c24c4084ee2b52ced503a`
- `ultraviolet-windows-x86_64.zip.sha256`

Known limitations:

- This alpha is intended for early evaluation, compiler bring-up, and
  HelloUltraviolet conformance work; APIs and package layout may change.
- Binary release coverage is limited to Linux x86_64, Apple Silicon macOS 14+,
  and Windows x86_64.
- macOS users must have Xcode Command Line Tools installed so the packaged
  `clang++` driver can resolve the active SDK.
- Windows source verification must run from an actual Windows Visual Studio
  Developer PowerShell.
- Only the files attached to the GitHub release are supported distribution
  archives; arbitrary CMake build-tree outputs are not release payloads.

Third-party notices:

- Release archives include vendored toolchain payloads. Preserve the bundled
  `LICENSE.md` and `THIRD_PARTY_NOTICES.md` files when redistributing an
  archive or derived package.

## Repository State

- All intended release files are committed.
- `.offices/` remains untracked and ignored.
- Extern archives are published, reachable, and match
  `Bootstrap/extern/ExternManifest.json`.
- Generated HelloUltraviolet catalog files are current.
- Root and HelloUltraviolet changelogs describe the release contents.

## Verification

Run final verifications from the commit being released.

Linux:

```bash
python3 Tools/RunHelloVerification.py --target-profile x86_64-sysv
```

Apple Silicon macOS:

```bash
python3 Tools/RunHelloVerification.py --target-profile aarch64-darwin
```

Windows, from actual Windows Visual Studio Developer PowerShell:

```powershell
py -3 Tools\RunHelloVerification.py --target-profile x86_64-win64
```

Record the final `Verification result: PASS` lines and transcript SHA-256 values in
the GitHub release notes.

## GitHub Release Notes

Release notes should state:

- Public alpha status and non-production maturity.
- Supported target profiles: `x86_64-sysv`, `aarch64-darwin`, and
  `x86_64-win64`.
- Supported host verification used for the release.
- macOS support statement: Apple Silicon macOS 14+ is supported when Xcode
  Command Line Tools are installed.
- Known limitations.
- Artifact paths or attached binary artifacts.
- Verification transcript SHA-256 values.
- Third-party notice requirements for artifacts containing vendored LLVM tools.

## Artifacts

If binary artifacts are attached:

- Verify installer asset names before generating archives:

  ```bash
  python3 Tools/PackageRelease.py --check-release-assets
  ```

- Generate installer archives:

  ```bash
  python3 Tools/PackageRelease.py --platform all
  ```

- Inspect release staging under `Build/Staging/<platform>` when reviewing
  archive contents. The staging platform directory is the release payload root;
  it must not contain an extra platform wrapper directory.
- Include the generated `.sha256` sidecar for every attached archive.

- Include Linux, macOS, and Windows package outputs separately:
  - `ultraviolet-linux-x86_64.tar.gz`
  - `ultraviolet-macos-aarch64.tar.gz`
  - `ultraviolet-windows-x86_64.zip`
- Include SHA-256 checksums for each artifact:
  - `ultraviolet-linux-x86_64.tar.gz.sha256`
  - `ultraviolet-macos-aarch64.tar.gz.sha256`
  - `ultraviolet-windows-x86_64.zip.sha256`
- Preserve applicable third-party license and notice material.

## Installer Smoke Tests

Before publishing the release, verify installer behavior against the generated
local archives.

Linux:

```bash
Tools/InstallUltraviolet.sh \
    --from-local Build/Release/ultraviolet-linux-x86_64.tar.gz \
    --checksum-file Build/Release/ultraviolet-linux-x86_64.tar.gz.sha256 \
    --install-dir /tmp/ultraviolet-install/current \
    --bin-dir /tmp/ultraviolet-install/bin \
    --command both \
    --no-path \
    --yes
/tmp/ultraviolet-install/bin/uv --help
/tmp/ultraviolet-install/bin/uvc --help
```

Apple Silicon macOS:

```bash
Tools/InstallUltraviolet.sh \
    --from-local Build/Release/ultraviolet-macos-aarch64.tar.gz \
    --checksum-file Build/Release/ultraviolet-macos-aarch64.tar.gz.sha256 \
    --install-dir /tmp/ultraviolet-install/current \
    --bin-dir /tmp/ultraviolet-install/bin \
    --command both \
    --no-path \
    --yes
/tmp/ultraviolet-install/bin/uv --help
/tmp/ultraviolet-install/bin/uvc --help
```

Windows PowerShell:

```powershell
.\Tools\InstallUltraviolet.ps1 `
    -FromLocal Build\Release\ultraviolet-windows-x86_64.zip `
    -ChecksumFile Build\Release\ultraviolet-windows-x86_64.zip.sha256 `
    -InstallDir $env:TEMP\ultraviolet-install\current `
    -BinDir $env:TEMP\ultraviolet-install\bin `
    -Command both `
    -NoPath `
    -Yes
& "$env:TEMP\ultraviolet-install\bin\uv.cmd" --help
& "$env:TEMP\ultraviolet-install\bin\uvc.cmd" --help
```
