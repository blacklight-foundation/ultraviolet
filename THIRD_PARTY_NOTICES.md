# Third-Party Notices

This repository vendors prebuilt LLVM toolchain payloads used by the bootstrap
compiler build and release package staging.

## LLVM

Vendored LLVM payloads are located under:

- `Bootstrap/extern/llvm/llvm-21.1.8-x86_64`
- `Bootstrap/extern/llvm/llvm-21.1.8-x86_64-sysv`

LLVM files are part of the LLVM Project and use the Apache License v2.0 with
LLVM Exceptions. The vendored payloads include LLVM license text at:

- `Bootstrap/extern/llvm/llvm-21.1.8-x86_64/include/llvm/Support/LICENSE.TXT`
- `Bootstrap/extern/llvm/llvm-21.1.8-x86_64-sysv/include/llvm/Support/LICENSE.TXT`

Release artifacts that include LLVM sidecar tools or libraries must preserve
the applicable LLVM license and notice material.
