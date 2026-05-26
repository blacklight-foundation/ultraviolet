#!/usr/bin/env sh
set -eu

ref="${ULTRAVIOLET_INSTALL_REF:-main}"
url="https://raw.githubusercontent.com/blacklight-foundation/ultraviolet/${ref}/Tools/InstallUltraviolet.sh"

if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" | sh -s -- "$@"
elif command -v wget >/dev/null 2>&1; then
    wget -qO- "$url" | sh -s -- "$@"
else
    printf 'error: curl or wget is required to fetch the Ultraviolet installer\n' >&2
    exit 1
fi
