#!/usr/bin/env sh
set -eu

REPOSITORY="blacklight-foundation/ultraviolet"
ASSET_NAME=""
DEFAULT_INSTALL_DIR="${HOME}/.ultraviolet/current"
DEFAULT_BIN_DIR="${HOME}/.ultraviolet/bin"

version="${ULTRAVIOLET_INSTALL_VERSION:-latest}"
install_dir="${ULTRAVIOLET_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
bin_dir="${ULTRAVIOLET_BIN_DIR:-$DEFAULT_BIN_DIR}"
command_mode="${ULTRAVIOLET_INSTALL_COMMAND:-uv}"
command_was_set=0
modify_path=1
yes=0
dry_run=0
source_url="${ULTRAVIOLET_INSTALL_SOURCE_URL:-}"
checksum_url="${ULTRAVIOLET_INSTALL_CHECKSUM_URL:-}"
checksum_file="${ULTRAVIOLET_INSTALL_CHECKSUM_FILE:-}"
from_local=""

if [ "${ULTRAVIOLET_INSTALL_COMMAND:-}" != "" ]; then
    command_was_set=1
fi

if [ "${ULTRAVIOLET_INSTALL_NO_PATH:-}" = "1" ]; then
    modify_path=0
fi

if [ "${ULTRAVIOLET_INSTALL_YES:-}" = "1" ]; then
    yes=1
fi

usage() {
    cat <<'EOF'
Install the Ultraviolet compiler for Linux or Apple Silicon macOS.

Usage:
  InstallUltraviolet.sh [options]

Options:
  --version <tag>       GitHub release tag to install. Default: latest.
  --install-dir <dir>   Package install directory. Default: ~/.ultraviolet/current.
  --bin-dir <dir>       Directory for command shims. Default: ~/.ultraviolet/bin.
  --command <mode>      Command shims to install: uv, uvc, or both. Default: uv.
  --use-uv              Equivalent to --command uv.
  --both                Equivalent to --command both.
  --no-path             Do not add the shim directory to the user shell profile.
  --yes                 Noninteractive mode. Uses uvc if Python uv conflicts.
  --source-url <url>    Override the compiler archive URL.
  --checksum-url <url>  Override the SHA-256 checksum URL.
  --checksum-file <path>
                        Verify a local archive with this SHA-256 checksum file.
  --from-local <path>   Install from a local package directory or .tar.gz archive.
  --dry-run             Print planned actions without changing files.
  -h, --help            Show this help text.

The default command is uv. When an existing Python uv command is detected,
interactive installs ask before taking over uv. The recommended choice is uv:
Ultraviolet owns uv and the existing command is exposed as pyuv. Noninteractive
conflict installs use uvc to avoid replacing Python uv without consent.
EOF
}

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

log() {
    printf '%s\n' "$1"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            [ "$#" -ge 2 ] || fail "--version requires an argument"
            version="$2"
            shift 2
            ;;
        --install-dir)
            [ "$#" -ge 2 ] || fail "--install-dir requires an argument"
            install_dir="$2"
            shift 2
            ;;
        --bin-dir)
            [ "$#" -ge 2 ] || fail "--bin-dir requires an argument"
            bin_dir="$2"
            shift 2
            ;;
        --command)
            [ "$#" -ge 2 ] || fail "--command requires an argument"
            command_mode="$2"
            command_was_set=1
            shift 2
            ;;
        --use-uv)
            command_mode="uv"
            command_was_set=1
            shift
            ;;
        --both)
            command_mode="both"
            command_was_set=1
            shift
            ;;
        --no-path)
            modify_path=0
            shift
            ;;
        --yes)
            yes=1
            shift
            ;;
        --source-url)
            [ "$#" -ge 2 ] || fail "--source-url requires an argument"
            source_url="$2"
            shift 2
            ;;
        --checksum-url)
            [ "$#" -ge 2 ] || fail "--checksum-url requires an argument"
            checksum_url="$2"
            shift 2
            ;;
        --checksum-file)
            [ "$#" -ge 2 ] || fail "--checksum-file requires an argument"
            checksum_file="$2"
            shift 2
            ;;
        --from-local)
            [ "$#" -ge 2 ] || fail "--from-local requires an argument"
            from_local="$2"
            shift 2
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
done

case "$command_mode" in
    uvc|uv|both) ;;
    *) fail "--command must be one of: uvc, uv, both" ;;
esac

host_system="$(uname -s)"
host_machine="$(uname -m)"
case "${host_system}:${host_machine}" in
    Linux:x86_64|Linux:amd64)
        ASSET_NAME="ultraviolet-linux-x86_64.tar.gz"
        ;;
    Darwin:arm64)
        ASSET_NAME="ultraviolet-macos-aarch64.tar.gz"
        command -v xcode-select >/dev/null 2>&1 || fail "xcode-select is required on macOS"
        xcode-select -p >/dev/null 2>&1 || fail "Xcode Command Line Tools are required on macOS"
        xcrun --find clang++ >/dev/null 2>&1 || fail "xcrun cannot locate clang++"
        xcrun --show-sdk-path >/dev/null 2>&1 || fail "xcrun cannot locate the macOS SDK"
        ;;
    Darwin:*)
        fail "macOS installs require Apple Silicon arm64; this host is ${host_machine}"
        ;;
    *)
        fail "this installer supports Linux x86_64 and Apple Silicon macOS; use Tools\\InstallUltraviolet.ps1 on Windows"
        ;;
esac

if [ -z "$source_url" ] && [ -z "$from_local" ]; then
    if [ "$version" = "latest" ]; then
        source_url="https://github.com/${REPOSITORY}/releases/latest/download/${ASSET_NAME}"
    else
        source_url="https://github.com/${REPOSITORY}/releases/download/${version}/${ASSET_NAME}"
    fi
fi

if [ -z "$checksum_url" ] && [ -z "$from_local" ]; then
    checksum_url="${source_url}.sha256"
fi

if [ -n "$checksum_file" ] && [ -z "$from_local" ]; then
    fail "--checksum-file requires --from-local"
fi

is_inside_dir() {
    case "$1" in
        "$2"/*|"$2") return 0 ;;
        *) return 1 ;;
    esac
}

existing_uv=""
existing_uv_version=""
if command -v uv >/dev/null 2>&1; then
    existing_uv="$(command -v uv)"
    existing_uv_version="$("$existing_uv" --version 2>/dev/null || true)"
fi

python_uv_detected=0
if [ -n "$existing_uv" ] && ! is_inside_dir "$existing_uv" "$bin_dir"; then
    case "$existing_uv_version" in
        uv\ [0-9]*|uv\ 0.*|uv\ 1.*) python_uv_detected=1 ;;
    esac
fi

if [ "$python_uv_detected" -eq 1 ] &&
   [ "$command_was_set" -eq 0 ] &&
   [ "$yes" -eq 0 ] &&
   [ -r /dev/tty ]; then
    {
        printf '\nDetected existing Python uv: %s\n' "$existing_uv"
        printf 'Version: %s\n' "$existing_uv_version"
        printf 'Recommended: install Ultraviolet as uv and expose Python uv as pyuv.\n'
        printf 'Choose command mode [uv/uvc/both] (default: uv): '
    } > /dev/tty
    IFS= read -r answer < /dev/tty || answer=""
    case "$answer" in
        "") command_mode="uv" ;;
        uvc|uv|both) command_mode="$answer" ;;
        *) fail "invalid command mode: $answer" ;;
    esac
fi

if [ "$python_uv_detected" -eq 1 ] &&
   [ "$command_was_set" -eq 0 ] &&
   [ "$yes" -eq 1 ]; then
    command_mode="uvc"
fi

download() {
    url="$1"
    output="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$output"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$output" "$url"
    else
        fail "curl or wget is required to download release assets"
    fi
}

sha256_file() {
    path="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$path" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$path" | awk '{print $1}'
    else
        fail "sha256sum or shasum is required to verify release assets"
    fi
}

verify_checksum() {
    archive="$1"
    checksum_file="$2"
    expected="$(
        sed -n 's/^\([A-Fa-f0-9][A-Fa-f0-9]*\).*/\1/p' "$checksum_file" |
        awk 'length($0) == 64 { print; exit }'
    )"
    [ -n "$expected" ] || fail "checksum file did not contain a SHA-256 hash"
    actual="$(sha256_file "$archive")"
    [ "$actual" = "$expected" ] || fail "checksum mismatch for $archive"
}

find_package_root() {
    extract_dir="$1"
    for candidate in \
        "$extract_dir/ultraviolet" \
        "$extract_dir/ultraviolet-linux-x86_64" \
        "$extract_dir/ultraviolet-macos-aarch64" \
        "$extract_dir"; do
        if [ -x "$candidate/uv" ] || [ -x "$candidate/uvc" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    found="$(
        find "$extract_dir" -maxdepth 3 -type f \( -name uv -o -name uvc \) \
            -perm -u+x 2>/dev/null | head -n 1
    )"
    [ -n "$found" ] || fail "could not find uv or uvc in package"
    dirname "$found"
}

copy_package() {
    package_root="$1"
    stage="$2"
    mkdir -p "$stage"
    cp -R "$package_root"/. "$stage"/
    chmod +x "$stage"/uv "$stage"/uvc 2>/dev/null || true
    chmod +x "$stage"/linux/tools/* 2>/dev/null || true
    chmod +x "$stage"/macos/tools/* 2>/dev/null || true
}

create_link() {
    name="$1"
    target="$2"
    [ -e "$target" ] || fail "cannot create $name shim; target missing: $target"
    mkdir -p "$bin_dir"
    ln -sfn "$target" "$bin_dir/$name"
}

profile_path() {
    shell_name="$(basename "${SHELL:-}")"
    case "$shell_name" in
        zsh) printf '%s\n' "${HOME}/.zshrc" ;;
        bash) printf '%s\n' "${HOME}/.bashrc" ;;
        *) printf '%s\n' "${HOME}/.profile" ;;
    esac
}

ensure_path() {
    case ":${PATH:-}:" in
        *":$bin_dir:"*) return 0 ;;
    esac

    profile="$(profile_path)"
    block_start="# >>> ultraviolet >>>"
    block_end="# <<< ultraviolet <<<"
    mkdir -p "$(dirname "$profile")"
    touch "$profile"

    if grep -F "$block_start" "$profile" >/dev/null 2>&1; then
        return 0
    fi

    {
        printf '\n%s\n' "$block_start"
        if [ "$bin_dir" = "$DEFAULT_BIN_DIR" ]; then
            printf 'export PATH="$HOME/.ultraviolet/bin:$PATH"\n'
        else
            printf 'export PATH="%s:$PATH"\n' "$bin_dir"
        fi
        printf '%s\n' "$block_end"
    } >> "$profile"

    log "Added $bin_dir to PATH in $profile."
}

log "Ultraviolet installer"
log "Install directory: $install_dir"
log "Shim directory: $bin_dir"
log "Command mode: $command_mode"
if [ "$modify_path" -eq 0 ]; then
    log "PATH update: disabled"
else
    log "PATH update: enabled"
fi

if [ "$dry_run" -eq 1 ]; then
    [ -n "$from_local" ] && log "Would install from local package: $from_local"
    [ -n "$source_url" ] && log "Would download: $source_url"
    [ -n "$checksum_url" ] && log "Would verify checksum: $checksum_url"
    [ -n "$checksum_file" ] && log "Would verify local checksum: $checksum_file"
    if [ "$python_uv_detected" -eq 1 ]; then
        log "Detected Python uv: $existing_uv"
    fi
    log "Dry run complete."
    exit 0
fi

tmp_dir="$(mktemp -d)"
cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

extract_dir="$tmp_dir/extract"
mkdir -p "$extract_dir"

if [ -n "$from_local" ]; then
    if [ -d "$from_local" ]; then
        [ -z "$checksum_file" ] ||
            fail "--checksum-file cannot be used with a local package directory"
        package_root="$from_local"
    else
        if [ -n "$checksum_file" ]; then
            [ -f "$checksum_file" ] ||
                fail "checksum file does not exist: $checksum_file"
            verify_checksum "$from_local" "$checksum_file"
        fi
        tar -xzf "$from_local" -C "$extract_dir"
        package_root="$(find_package_root "$extract_dir")"
    fi
else
    archive="$tmp_dir/$ASSET_NAME"
    checksum_file="$tmp_dir/${ASSET_NAME}.sha256"
    log "Downloading $source_url"
    download "$source_url" "$archive"
    log "Downloading $checksum_url"
    download "$checksum_url" "$checksum_file"
    verify_checksum "$archive" "$checksum_file"
    tar -xzf "$archive" -C "$extract_dir"
    package_root="$(find_package_root "$extract_dir")"
fi

stage="${install_dir}.stage"
backup="${install_dir}.previous"
rm -rf "$stage"
copy_package "$package_root" "$stage"

rm -rf "$backup"
if [ -e "$install_dir" ]; then
    mv "$install_dir" "$backup"
fi
mkdir -p "$(dirname "$install_dir")"
mv "$stage" "$install_dir"

compiler_uv="$install_dir/uv"
compiler_uvc="$install_dir/uvc"
[ -x "$compiler_uvc" ] || compiler_uvc="$compiler_uv"

case "$command_mode" in
    uvc)
        create_link "uvc" "$compiler_uvc"
        ;;
    uv)
        if [ "$python_uv_detected" -eq 1 ]; then
            create_link "pyuv" "$existing_uv"
        fi
        create_link "uv" "$compiler_uv"
        ;;
    both)
        if [ "$python_uv_detected" -eq 1 ]; then
            create_link "pyuv" "$existing_uv"
        fi
        create_link "uv" "$compiler_uv"
        create_link "uvc" "$compiler_uvc"
        ;;
esac

if [ "$modify_path" -eq 1 ]; then
    ensure_path
else
    log "PATH not modified. Add this directory manually when needed:"
    log "  $bin_dir"
fi

log "Installed Ultraviolet."
log "Open a new shell, then run:"
case "$command_mode" in
    uvc) log "  uvc --help" ;;
    uv) log "  uv --help" ;;
    both) log "  uv --help"; log "  uvc --help" ;;
esac
if [ "$python_uv_detected" -eq 1 ] &&
   { [ "$command_mode" = "uv" ] || [ "$command_mode" = "both" ]; }; then
    log "Python uv is available as:"
    log "  pyuv --version"
fi
