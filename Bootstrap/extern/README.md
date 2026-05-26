# Extern Payloads

LLVM and ICU payloads are distributed as versioned release archives, not as Git
or Git LFS content. Restore only the target profile needed by the current host:

```bash
python3 Tools/FetchTargetExterns.py --target-profile x86_64-sysv
python3 Tools/FetchTargetExterns.py --target-profile aarch64-darwin
```

```powershell
py -3 Tools\FetchTargetExterns.py --target-profile x86_64-win64
```

The archive names, optional split archive parts, SHA-256 hashes, install roots,
and required files are tracked in `Bootstrap/extern/ExternManifest.json`.
Split archives are reassembled locally and then verified against the logical
archive SHA before extraction.
