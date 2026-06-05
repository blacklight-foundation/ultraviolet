#!/usr/bin/env python3
"""Run HelloUltraviolet gates and emit a hashed conformance verification."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import os
import platform
import shlex
import shutil
import subprocess
import sys
import time
import uuid
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

import PackageRelease


ROOT = Path(__file__).resolve().parents[1]
MACOS_LLVM_EXTERN_ROOT = (
    ROOT / "Bootstrap" / "extern" / "llvm" / "llvm-21.1.8-aarch64-darwin"
)
MACOS_ICU_EXTERN_ROOT = ROOT / "Bootstrap" / "extern" / "icu" / "macos"


@dataclass(frozen=True)
class TargetConfig:
    profile: str
    host_system: str
    configure_preset: str
    package_preset: str
    release_platform: str
    compiler_path: Path
    executable_path: Path


@dataclass(frozen=True)
class VerificationGate:
    label: str
    cwd: Path
    command: tuple[str, ...] = ()
    allowed_exit_codes: tuple[int, ...] = (0,)
    internal_action: str | None = None


TARGETS = {
    "x86_64-sysv": TargetConfig(
        profile="x86_64-sysv",
        host_system="Linux",
        configure_preset="linux-release",
        package_preset="linux-release-package",
        release_platform="linux",
        compiler_path=ROOT / "Bootstrap" / "Ultraviolet" / "build" / "linux" / "out" / "uv",
        executable_path=ROOT / "HelloUltraviolet" / "Build" / "Binary" / "HelloUltraviolet",
    ),
    "x86_64-win64": TargetConfig(
        profile="x86_64-win64",
        host_system="Windows",
        configure_preset="windows-release",
        package_preset="windows-release-package",
        release_platform="windows",
        compiler_path=ROOT
        / "Bootstrap"
        / "Ultraviolet"
        / "build"
        / "windows"
        / "out"
        / "uv.exe",
        executable_path=ROOT
        / "HelloUltraviolet"
        / "Build"
        / "Binary"
        / "HelloUltraviolet.exe",
    ),
    "aarch64-darwin": TargetConfig(
        profile="aarch64-darwin",
        host_system="Darwin",
        configure_preset="macos-release",
        package_preset="macos-release-package",
        release_platform="macos",
        compiler_path=ROOT / "Bootstrap" / "Ultraviolet" / "build" / "macos" / "out" / "uv",
        executable_path=ROOT / "HelloUltraviolet" / "Build" / "Binary" / "HelloUltraviolet",
    ),
}

REQUIRED_EXTERN_PAYLOADS = {
    "aarch64-darwin": (
        MACOS_LLVM_EXTERN_ROOT / "bin" / "clang++",
        MACOS_LLVM_EXTERN_ROOT / "bin" / "llvm-ar",
        MACOS_LLVM_EXTERN_ROOT / "bin" / "llvm-as",
        MACOS_LLVM_EXTERN_ROOT / "lib" / "cmake" / "llvm" / "LLVMConfig.cmake",
        MACOS_ICU_EXTERN_ROOT / "include" / "unicode" / "utypes.h",
        MACOS_ICU_EXTERN_ROOT / "lib" / "libicui18n.72.dylib",
        MACOS_ICU_EXTERN_ROOT / "lib" / "libicuuc.72.dylib",
        MACOS_ICU_EXTERN_ROOT / "lib" / "libicudata.72.dylib",
    ),
}
MACOS_ARTIFACT_PROJECT_NAMES = (
    "MacOSExecutableOutput",
    "MacOSStaticLibraryArchive",
    "MacOSHostedSharedLibrary",
    "MacOSDylibLibrary",
)
EXECUTABLE_AUDIT_ARTIFACT_PROJECTS = (
    ("ExecutableOutput", ()),
    ("EmitLlLibrary", ()),
    ("FlowProofRuntimeErasure", ("--target-profile", "x86_64-win64")),
    ("AArch64DependencyObject", ()),
    ("EmitBcLibrary", ()),
)
RUNNABLE_EXECUTABLE_AUDIT_ARTIFACT_PROJECTS = frozenset({
    "ExecutableOutput",
})
CURRENT_TARGET_EXECUTABLE_AUDIT_ARTIFACT_PROJECTS = frozenset({
    "ExecutableOutput",
    "EmitBcLibrary",
    "EmitLlLibrary",
})
EXECUTABLE_AUDIT_OUTPUT_DIAGNOSTICS = (
    (
        "LlvmToolResolveOwnership",
        "Lowering/LlvmToolResolveOwnership",
        "LlvmToolResolveOwnership.conformance.log",
    ),
    (
        "EmitLLVMRenderFailure",
        "Lowering/EmitLLVMRenderFailure",
        "EmitLLVMRenderFailure.conformance.log",
    ),
    (
        "ManifestParseError",
        "Projects/ManifestParseError",
        "ManifestParseError.conformance.log",
    ),
)
PACKAGED_TOOL_PLATFORM = {
    "x86_64-sysv": "linux",
    "x86_64-win64": "windows",
    "aarch64-darwin": "macos",
}
RELEASE_PLATFORM_LABELS = {
    "linux": "Linux",
    "windows": "Windows",
    "macos": "macOS",
}
EXECUTABLE_EXTERN_PAYLOADS = frozenset(
    {
        MACOS_LLVM_EXTERN_ROOT / "bin" / "clang++",
        MACOS_LLVM_EXTERN_ROOT / "bin" / "llvm-ar",
        MACOS_LLVM_EXTERN_ROOT / "bin" / "llvm-as",
    }
)


class Transcript:
    def __init__(self) -> None:
        self._digest = hashlib.sha256()

    def write(self, text: str, *, hashed: bool = True) -> None:
        sys.stdout.write(text)
        sys.stdout.flush()
        if hashed:
            self._digest.update(text.encode("utf-8", errors="replace"))

    def line(self, text: str = "", *, hashed: bool = True) -> None:
        self.write(f"{text}\n", hashed=hashed)

    def hexdigest(self) -> str:
        return self._digest.hexdigest()


def infer_target_profile() -> str | None:
    system = platform.system()
    if system == "Windows":
        return "x86_64-win64"
    if system == "Linux":
        return "x86_64-sysv"
    if system == "Darwin":
        machine = platform.machine().lower()
        if machine in {"arm64", "aarch64"}:
            return "aarch64-darwin"
    return None


def parse_args() -> argparse.Namespace:
    default_target = infer_target_profile()
    parser = argparse.ArgumentParser(
        description=(
            "Run the HelloUltraviolet public-alpha conformance gates and print "
            "a transcript with a final SHA-256 hash."
        )
    )
    parser.add_argument(
        "--target-profile",
        choices=sorted(TARGETS),
        default=default_target,
        help="target profile to validate; defaults to the host platform target",
    )
    parser.add_argument(
        "--pr",
        default=os.environ.get("GITHUB_REF_NAME", ""),
        help="pull request, branch, or review identifier to include in the verification",
    )
    parser.add_argument(
        "--nonce",
        default="",
        help="caller-supplied token to bind the verification to a review request",
    )
    parser.add_argument(
        "--cmake",
        default="cmake",
        help="CMake executable to use for configure and package gates",
    )
    parser.add_argument(
        "--cmake-configure-arg",
        action="append",
        default=[],
        help="extra argument to append to the compiler package CMake configure command",
    )
    parser.add_argument(
        "--skip-package",
        action="store_true",
        help="skip CMake package rebuild; not valid for public-alpha release verifications",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print the verification header and planned commands without executing gates",
    )
    parser.add_argument(
        "--stop-before-gate",
        default="",
        help=(
            "stop after the gate immediately before this exact label; intended "
            "for diagnostic workflows that run the final command under a debugger"
        ),
    )
    return parser.parse_args()


def command_text(command: Sequence[str]) -> str:
    if platform.system() == "Windows":
        return subprocess.list2cmdline(command)
    return shlex.join(command)


def gate_command_text(gate: VerificationGate) -> str:
    if gate.internal_action is not None:
        return f"<internal: {gate.internal_action}>"
    return command_text(gate.command)


def allowed_exit_codes_text(exit_codes: Sequence[int]) -> str:
    return ", ".join(str(exit_code) for exit_code in exit_codes)


def relative_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def git_text(args: list[str], timeout_seconds: float = 10.0) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"unknown ({exc})"

    text = result.stdout.strip()
    if result.returncode != 0:
        error = result.stderr.strip()
        if error:
            return f"unknown ({error})"
        return f"unknown (git exited {result.returncode})"
    return text


def git_paths(args: list[str], timeout_seconds: float = 10.0) -> list[str] | str:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"unknown ({exc})"

    if result.returncode != 0:
        error = result.stderr.strip()
        if error:
            return f"unknown ({error})"
        return f"unknown (git exited {result.returncode})"

    text = result.stdout.strip()
    if not text:
        return []
    return text.splitlines()


def git_worktree_state() -> str:
    staged_paths = git_paths(["diff", "--cached", "--name-only"])
    if isinstance(staged_paths, str):
        return staged_paths

    untracked_paths = git_paths(["ls-files", "--others", "--exclude-standard"])
    if isinstance(untracked_paths, str):
        if staged_paths:
            return (
                f"dirty (at least {len(set(staged_paths))} changed path(s); "
                f"untracked scan {untracked_paths})"
            )
        return untracked_paths

    known_paths = set(staged_paths)
    known_paths.update(untracked_paths)
    if known_paths:
        return (
            f"dirty (at least {len(known_paths)} changed path(s); "
            "unstaged scan skipped after staged/untracked changes found)"
        )

    unstaged_paths = git_paths(["diff", "--name-only"])
    if isinstance(unstaged_paths, str):
        return unstaged_paths
    if not unstaged_paths:
        return "clean"
    return f"dirty ({len(set(unstaged_paths))} changed path(s))"


def artifact_project_executable_path(
    project_root: Path,
    config: TargetConfig,
) -> Path:
    suffix = ".exe" if config.profile == "x86_64-win64" else ""
    return project_root / "Build" / "Binary" / f"{project_root.name}{suffix}"


def executable_audit_artifact_project_args(
    project_name: str,
    extra_args: tuple[str, ...],
    config: TargetConfig,
) -> tuple[str, ...]:
    if project_name in CURRENT_TARGET_EXECUTABLE_AUDIT_ARTIFACT_PROJECTS:
        return ("--target-profile", config.profile, *extra_args)
    return extra_args


def release_platform_label(config: TargetConfig) -> str:
    return RELEASE_PLATFORM_LABELS[config.release_platform]


def release_archive_path(config: TargetConfig) -> Path:
    package_config = PackageRelease.DEFAULT_PACKAGES[config.release_platform]
    return ROOT / "Build" / "Release" / package_config.archive_name


def release_checksum_path(config: TargetConfig) -> Path:
    release_archive = release_archive_path(config)
    return release_archive.with_suffix(release_archive.suffix + ".sha256")


def install_probe_root(config: TargetConfig) -> Path:
    return ROOT / "Build" / "InstallProbe" / config.release_platform


def install_script_probe_command(config: TargetConfig) -> tuple[str, ...]:
    release_archive = release_archive_path(config)
    release_checksum = release_checksum_path(config)
    probe_root = install_probe_root(config)
    if config.release_platform == "windows":
        return (
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(ROOT / "Tools" / "InstallUltraviolet.ps1"),
            "-FromLocal",
            str(release_archive),
            "-ChecksumFile",
            str(release_checksum),
            "-InstallDir",
            str(probe_root / "current"),
            "-BinDir",
            str(probe_root / "bin"),
            "-Command",
            "both",
            "-Yes",
            "-NoPath",
        )

    return (
        "sh",
        str(ROOT / "Tools" / "InstallUltraviolet.sh"),
        "--from-local",
        str(release_archive),
        "--checksum-file",
        str(release_checksum),
        "--install-dir",
        str(probe_root / "current"),
        "--bin-dir",
        str(probe_root / "bin"),
        "--command",
        "both",
        "--yes",
        "--no-path",
    )


def installed_command_path(config: TargetConfig, command_name: str) -> Path:
    suffix = ".cmd" if config.release_platform == "windows" else ""
    return install_probe_root(config) / "bin" / f"{command_name}{suffix}"


def release_package_probe_gates(
    python: str,
    config: TargetConfig,
) -> list[VerificationGate]:
    label = release_platform_label(config)
    return [
        VerificationGate(
            f"{label} release asset contract check",
            ROOT,
            (
                python,
                str(ROOT / "Tools" / "PackageRelease.py"),
                "--check-release-assets",
            ),
        ),
        VerificationGate(
            f"{label} release archive package",
            ROOT,
            (
                python,
                str(ROOT / "Tools" / "PackageRelease.py"),
                "--platform",
                config.release_platform,
            ),
        ),
        VerificationGate(
            f"{label} install script probe",
            ROOT,
            install_script_probe_command(config),
        ),
        VerificationGate(
            f"{label} installed uv help",
            ROOT,
            (str(installed_command_path(config, "uv")), "--help"),
        ),
        VerificationGate(
            f"{label} installed uvc help",
            ROOT,
            (str(installed_command_path(config, "uvc")), "--help"),
        ),
    ]


def planned_commands(
    args: argparse.Namespace,
    config: TargetConfig,
) -> list[VerificationGate]:
    python = sys.executable
    bootstrap_root = ROOT / "Bootstrap" / "Ultraviolet"
    hello_root = ROOT / "HelloUltraviolet"
    gates: list[VerificationGate] = [
        VerificationGate(
            "obligation ledger check",
            ROOT,
            (python, str(ROOT / "Tools" / "ExtractObligationLedger.py"), "--check"),
        ),
        VerificationGate(
            "HelloUltraviolet catalog check",
            ROOT,
            (python, str(ROOT / "Tools" / "GenerateHelloCatalog.py"), "--check"),
        ),
    ]
    if not args.skip_package:
        gates.extend(
            [
                VerificationGate(
                    "compiler package configure",
                    bootstrap_root,
                    (
                        args.cmake,
                        "--preset",
                        config.configure_preset,
                        *args.cmake_configure_arg,
                    ),
                ),
                VerificationGate(
                    "compiler package build",
                    bootstrap_root,
                    (args.cmake, "--build", "--preset", config.package_preset),
                ),
            ]
        )
    gates.extend(
        [
            VerificationGate(
                "HelloUltraviolet check build",
                ROOT,
                (
                    str(config.compiler_path),
                    "build",
                    str(hello_root),
                    "--target-profile",
                    config.profile,
                    "--check",
                ),
            ),
            VerificationGate(
                "HelloUltraviolet full build",
                ROOT,
                (
                    str(config.compiler_path),
                    "build",
                    str(hello_root),
                    "--target-profile",
                    config.profile,
                ),
            ),
        ]
    )

    artifact_project_root = hello_root / "Fixtures" / "ArtifactProjects"
    output_diagnostic_root = hello_root / "Fixtures" / "OutputDiagnostics"
    gates.append(
        VerificationGate(
            "HelloUltraviolet executable audit fixture tool setup",
            ROOT,
            internal_action="stage-executable-audit-fixture-tools",
        )
    )
    for project_name, extra_args in EXECUTABLE_AUDIT_ARTIFACT_PROJECTS:
        project_root = artifact_project_root / project_name
        project_args = executable_audit_artifact_project_args(
            project_name,
            extra_args,
            config,
        )
        gates.append(
            VerificationGate(
                f"HelloUltraviolet executable audit artifact fixture build: {project_name}",
                ROOT,
                (
                    str(config.compiler_path),
                    "build",
                    str(project_root),
                    *project_args,
                ),
            )
        )
        if project_name in RUNNABLE_EXECUTABLE_AUDIT_ARTIFACT_PROJECTS:
            gates.append(
                VerificationGate(
                    (
                        "HelloUltraviolet executable audit artifact fixture run: "
                        f"{project_name}"
                    ),
                    ROOT,
                    (str(artifact_project_executable_path(project_root, config)),),
                )
            )
    for project_name, relative_project_root, conformance_log in (
        EXECUTABLE_AUDIT_OUTPUT_DIAGNOSTICS
    ):
        project_root = output_diagnostic_root / relative_project_root
        gates.append(
            VerificationGate(
                f"HelloUltraviolet executable audit diagnostic fixture build: {project_name}",
                ROOT,
                (
                    str(config.compiler_path),
                    "build",
                    str(project_root),
                    "--conformance",
                    conformance_log,
                ),
                allowed_exit_codes=(1,),
            )
        )

    if config.profile == "aarch64-darwin":
        for project_name in MACOS_ARTIFACT_PROJECT_NAMES:
            project_root = artifact_project_root / project_name
            gates.append(
                VerificationGate(
                    f"macOS artifact fixture build: {project_name}",
                    ROOT,
                    (
                        str(config.compiler_path),
                        "build",
                        str(project_root),
                        "--target-profile",
                        config.profile,
                    ),
                )
            )
        gates.append(
            VerificationGate(
                "macOS Mach-O artifact validation",
                ROOT,
                (
                    python,
                    str(ROOT / "Tools" / "VerifyReleaseArtifacts.py"),
                    "--platform",
                    "macos",
                    "--compiler",
                    str(config.compiler_path),
                    "--hello-executable",
                    str(config.executable_path),
                    "--object-root",
                    str(hello_root / "Build" / "Intermediate" / "Obj"),
                    "--support-lib-dir",
                    str(
                        ROOT
                        / "Bootstrap"
                        / "Ultraviolet"
                        / "build"
                        / "macos"
                        / "out"
                        / "macos"
                        / "lib"
                    ),
                    "--support-tool-dir",
                    str(
                        ROOT
                        / "Bootstrap"
                        / "Ultraviolet"
                        / "build"
                        / "macos"
                        / "out"
                        / "macos"
                        / "tools"
                    ),
                    "--artifact-project-root",
                    str(hello_root / "Fixtures" / "ArtifactProjects"),
                ),
            )
        )
    else:
        gates.append(
            VerificationGate(
                f"{release_platform_label(config)} release artifact validation",
                ROOT,
                (
                    python,
                    str(ROOT / "Tools" / "VerifyReleaseArtifacts.py"),
                    "--platform",
                    config.release_platform,
                ),
            )
        )
    gates.extend(release_package_probe_gates(python, config))
    gates.extend(
        [
            VerificationGate(
                "HelloUltraviolet executable",
                ROOT,
                (str(config.executable_path),),
            ),
            VerificationGate(
                "HelloUltraviolet audit executable",
                ROOT,
                (str(config.executable_path), "--audit"),
            ),
            VerificationGate(
                "HelloUltraviolet source-native tests",
                ROOT,
                (
                    str(config.compiler_path),
                    "test",
                    str(hello_root),
                    "--target-profile",
                    config.profile,
                ),
            ),
        ]
    )
    return gates


def gates_before_stop_label(
    gates: list[VerificationGate],
    stop_before_gate: str,
) -> tuple[list[VerificationGate], bool]:
    if not stop_before_gate:
        return gates, True

    for index, gate in enumerate(gates):
        if gate.label == stop_before_gate:
            return gates[:index], True
    return gates, False


def required_extern_payloads(config: TargetConfig) -> tuple[Path, ...]:
    return REQUIRED_EXTERN_PAYLOADS.get(config.profile, ())


def extern_payload_problem(path: Path) -> str | None:
    if not path.is_file():
        return "missing"
    if path in EXECUTABLE_EXTERN_PAYLOADS and not os.access(path, os.X_OK):
        return "not executable"
    return None


def invalid_required_extern_payloads(config: TargetConfig) -> list[Path]:
    return [
        path
        for path in required_extern_payloads(config)
        if extern_payload_problem(path) is not None
    ]


def write_planned_extern_payload_gate(
    transcript: Transcript,
    config: TargetConfig,
) -> None:
    payloads = required_extern_payloads(config)
    if not payloads:
        return

    transcript.line()
    transcript.line("## Planned gate: target extern payloads")
    transcript.line("## Working directory: .")
    for path in payloads:
        requirement = "file"
        if path in EXECUTABLE_EXTERN_PAYLOADS:
            requirement = "executable file"
        transcript.line(f"## Required {requirement}: {relative_path(path)}")


def run_extern_payload_gate(transcript: Transcript, config: TargetConfig) -> int:
    payloads = required_extern_payloads(config)
    if not payloads:
        return 0

    transcript.line()
    transcript.line("## Gate: target extern payloads")
    transcript.line("## Working directory: .")
    for path in payloads:
        problem = extern_payload_problem(path)
        status = "present" if problem is None else problem
        transcript.line(f"## {status}: {relative_path(path)}")

    invalid = invalid_required_extern_payloads(config)
    if not invalid:
        transcript.line("## Exit code: 0")
        return 0

    transcript.line("## Exit code: 2")
    transcript.line(
        "Failure: required target extern payloads are missing or not executable; "
        "run `python3 Tools/FetchTargetExterns.py --target-profile "
        f"{config.profile}` or restore the vendored payload roots before the verification."
    )
    return 2


def host_packaged_tool_dir(config: TargetConfig) -> Path:
    return config.compiler_path.parent / PACKAGED_TOOL_PLATFORM[config.profile] / "tools"


def find_existing_tool(tool_dir: Path, names: Sequence[str]) -> Path:
    for name in names:
        candidate = tool_dir / name
        if candidate.is_file():
            return candidate
    candidates = ", ".join(names)
    raise FileNotFoundError(f"could not find {candidates} in {tool_dir}")


def copy_executable(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        same_file = source.resolve() == destination.resolve()
    except OSError:
        same_file = False
    if not same_file:
        shutil.copy2(source, destination)
    destination.chmod(destination.stat().st_mode | 0o111)


def write_executable_script(destination: Path, text: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8", newline="\n")
    destination.chmod(destination.stat().st_mode | 0o111)


def remove_stale_tool(path: Path) -> None:
    if path.is_file() or path.is_symlink():
        path.unlink()


def llvm_lib_wrapper_text(llvm_ar: Path) -> str:
    return f"""#!/usr/bin/env bash
set -euo pipefail

out=""
inputs=()
for arg in "$@"; do
    case "$arg" in
        /NOLOGO)
            ;;
        /OUT:*)
            out="${{arg#/OUT:}}"
            ;;
        *)
            inputs+=("$arg")
            ;;
    esac
done

if [[ -z "$out" ]]; then
    echo "llvm-lib wrapper expected /OUT:<archive>" >&2
    exit 2
fi

exec {shlex.quote(str(llvm_ar))} rcs "$out" "${{inputs[@]}}"
"""


def llvm_as_failure_shim_text() -> str:
    return """#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "--version" ]]; then
    printf 'LLVM (http://llvm.org/):\\n  LLVM version 21.1.8\\n  Optimized build.\\n'
    exit 0
fi

echo "llvm-as fixture shim: intentional assembly failure" >&2
exit 1
"""


def windows_llvm_as_failure_shim_source() -> str:
    return r"""#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        fputs("LLVM (http://llvm.org/):\n", stdout);
        fputs("  LLVM version 21.1.8\n", stdout);
        fputs("  Optimized build.\n", stdout);
        return 0;
    }

    fputs("llvm-as fixture shim: intentional assembly failure\n", stderr);
    return 1;
}
"""


def cmake_cache_value(cache_path: Path, key: str) -> str | None:
    if not cache_path.is_file():
        return None

    prefix = f"{key}:"
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith(prefix):
            continue
        _, _, value = line.partition("=")
        return value.strip() or None
    return None


def windows_c_compiler(config: TargetConfig) -> str:
    cache_path = config.compiler_path.parent.parent / "CMakeCache.txt"
    configured = cmake_cache_value(cache_path, "CMAKE_C_COMPILER")
    if configured:
        return configured

    discovered = shutil.which("cl")
    if discovered:
        return discovered

    raise FileNotFoundError(
        "could not find a Windows C compiler: CMAKE_C_COMPILER is missing from "
        f"{cache_path} and cl.exe is not on PATH"
    )


def visual_studio_dev_command_for_compiler(compiler: str) -> Path | None:
    compiler_path = Path(compiler)
    for parent in compiler_path.parents:
        if parent.name.lower() != "vc":
            continue
        candidate = parent.parent / "Common7" / "Tools" / "VsDevCmd.bat"
        if candidate.is_file():
            return candidate
    return None


def build_windows_llvm_as_failure_shim(
    config: TargetConfig,
    destination: Path,
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    source_path = destination.with_name("llvm_as_failure_shim.c")
    object_path = destination.with_suffix(".obj")
    source_path.write_text(
        windows_llvm_as_failure_shim_source(),
        encoding="utf-8",
        newline="\n",
    )
    compiler = windows_c_compiler(config)
    compile_command = [
        compiler,
        "/nologo",
        "/O2",
        f"/Fe:{destination}",
        f"/Fo:{object_path}",
        str(source_path),
    ]
    dev_command = visual_studio_dev_command_for_compiler(compiler)
    if dev_command is not None:
        compile_command = (
            f'call "{dev_command}" -arch=x64 -host_arch=x64 >nul && '
            f"{subprocess.list2cmdline(compile_command)}"
        )
    result = subprocess.run(
        compile_command,
        cwd=destination.parent,
        check=False,
        capture_output=True,
        shell=dev_command is not None,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr).strip()
        raise RuntimeError(
            f"failed to build Windows llvm-as shim with {compiler}: {output}"
        )
    remove_stale_tool(source_path)
    remove_stale_tool(object_path)


def stage_coff_fixture_tools(config: TargetConfig, destination_dir: Path) -> list[Path]:
    tool_dir = host_packaged_tool_dir(config)
    staged: list[Path] = []
    if platform.system() == "Windows":
        remove_stale_tool(destination_dir / "llvm-as")
        remove_stale_tool(destination_dir / "llvm-lib")
        llvm_as = find_existing_tool(tool_dir, ("llvm-as.exe",))
        llvm_lib = find_existing_tool(tool_dir, ("llvm-lib.exe",))
        destinations = (
            (llvm_as, destination_dir / "llvm-as.exe"),
            (llvm_lib, destination_dir / "llvm-lib.exe"),
        )
        for source, destination in destinations:
            copy_executable(source, destination)
            staged.append(destination)
        return staged

    remove_stale_tool(destination_dir / "llvm-as.exe")
    remove_stale_tool(destination_dir / "llvm-lib.exe")
    llvm_as = find_existing_tool(tool_dir, ("llvm-as",))
    llvm_ar = find_existing_tool(tool_dir, ("llvm-ar",))
    copy_executable(llvm_as, destination_dir / "llvm-as")
    write_executable_script(destination_dir / "llvm-lib", llvm_lib_wrapper_text(llvm_ar))
    staged.append(destination_dir / "llvm-as")
    staged.append(destination_dir / "llvm-lib")
    return staged


def stage_emit_llvm_render_failure_tool(config: TargetConfig) -> list[Path]:
    destination_dir = (
        ROOT
        / "HelloUltraviolet"
        / "Fixtures"
        / "OutputDiagnostics"
        / "Lowering"
        / "EmitLLVMRenderFailure"
        / "Tools"
    )
    if platform.system() == "Windows":
        remove_stale_tool(destination_dir / "llvm-as")
        destination = destination_dir / "llvm-as.exe"
        build_windows_llvm_as_failure_shim(config, destination)
        return [destination]

    destinations = (destination_dir / "llvm-as", destination_dir / "llvm-as.exe")
    for destination in destinations:
        write_executable_script(destination, llvm_as_failure_shim_text())
    return list(destinations)


def run_internal_gate(
    transcript: Transcript,
    label: str,
    cwd: Path,
    action: str,
    config: TargetConfig,
) -> int:
    transcript.line()
    transcript.line(f"## Gate: {label}")
    transcript.line(f"## Working directory: {relative_path(cwd)}")
    transcript.line(f"## Command: <internal: {action}>")

    start = time.monotonic()
    try:
        if action != "stage-executable-audit-fixture-tools":
            raise ValueError(f"unknown internal action {action}")

        target_support_tool_dir = config.compiler_path.parent / "windows" / "tools"
        emit_bc_compat_tool_dir = ROOT / "Bootstrap" / "Ultraviolet" / "build" / "tools"
        staged = [
            *stage_coff_fixture_tools(config, target_support_tool_dir),
            *stage_coff_fixture_tools(config, emit_bc_compat_tool_dir),
            *stage_emit_llvm_render_failure_tool(config),
        ]
        for path in staged:
            transcript.line(f"## Staged: {relative_path(path)}")
        exit_code = 0
    except Exception as exc:
        transcript.line(f"## Internal action failed: {exc}")
        exit_code = 1

    elapsed = time.monotonic() - start
    transcript.line(f"## Exit code: {exit_code}")
    transcript.line(f"## Elapsed seconds: {elapsed:.2f}")
    return exit_code


def run_command(
    transcript: Transcript,
    label: str,
    cwd: Path,
    command: Sequence[str],
    target_profile: str,
) -> int:
    transcript.line()
    transcript.line(f"## Gate: {label}")
    transcript.line(f"## Working directory: {relative_path(cwd)}")
    transcript.line(f"## Command: {command_text(command)}")

    start = time.monotonic()
    env = os.environ.copy()
    env["HUV_TARGET_PROFILE"] = target_profile
    try:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
    except OSError as exc:
        transcript.line(f"## Failed to start: {exc}")
        transcript.line("## Exit code: 127")
        return 127

    assert process.stdout is not None
    for line in process.stdout:
        transcript.write(line)
    exit_code = process.wait()
    elapsed = time.monotonic() - start
    transcript.line(f"## Exit code: {exit_code}")
    transcript.line(f"## Elapsed seconds: {elapsed:.2f}")
    return exit_code


def run_verification_gate(
    transcript: Transcript,
    gate: VerificationGate,
    config: TargetConfig,
) -> int:
    if gate.internal_action is None:
        exit_code = run_command(
            transcript,
            gate.label,
            gate.cwd,
            gate.command,
            config.profile,
        )
    else:
        exit_code = run_internal_gate(
            transcript,
            gate.label,
            gate.cwd,
            gate.internal_action,
            config,
        )

    if exit_code in gate.allowed_exit_codes:
        if gate.allowed_exit_codes != (0,):
            transcript.line(f"## Accepted exit code: {exit_code}")
        return 0

    transcript.line(
        "## Expected exit code(s): "
        f"{allowed_exit_codes_text(gate.allowed_exit_codes)}"
    )
    if exit_code == 0:
        return 1
    return exit_code


def write_header(transcript: Transcript, args: argparse.Namespace, config: TargetConfig) -> None:
    now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
    nonce = args.nonce or uuid.uuid4().hex

    transcript.line("HelloUltraviolet conformance verification")
    transcript.line(f"Verification ID: huv-{now.strftime('%Y%m%dT%H%M%SZ')}-{nonce[:12]}")
    transcript.line(f"UTC timestamp: {now.isoformat().replace('+00:00', 'Z')}")
    transcript.line(f"Repository root: {ROOT}")
    transcript.line(f"PR/ref: {args.pr or 'not provided'}")
    transcript.line(f"Commit: {git_text(['rev-parse', 'HEAD'])}")
    transcript.line(f"Worktree status: {git_worktree_state()}")
    transcript.line(f"Host system: {platform.system()}")
    transcript.line(f"Host platform: {platform.platform()}")
    transcript.line(f"Python executable: {sys.executable}")
    transcript.line(f"Target profile: {config.profile}")
    transcript.line(f"Compiler path: {config.compiler_path}")
    transcript.line(f"HelloUltraviolet executable: {config.executable_path}")
    if args.cmake_configure_arg:
        joined_args = " ".join(args.cmake_configure_arg)
        transcript.line(f"CMake configure args: {joined_args}")

    github_run_id = os.environ.get("GITHUB_RUN_ID", "")
    if github_run_id:
        transcript.line(f"GitHub repository: {os.environ.get('GITHUB_REPOSITORY', '')}")
        transcript.line(f"GitHub SHA: {os.environ.get('GITHUB_SHA', '')}")
        transcript.line(f"GitHub run ID: {github_run_id}")
        transcript.line(f"GitHub run attempt: {os.environ.get('GITHUB_RUN_ATTEMPT', '')}")

    if args.skip_package:
        transcript.line("Package gate: skipped by --skip-package")
    if args.dry_run:
        transcript.line("Execution mode: dry-run")
    if args.stop_before_gate:
        transcript.line(f"## Stop before gate: {args.stop_before_gate}")


def main() -> int:
    args = parse_args()
    transcript = Transcript()

    if not args.target_profile:
        transcript.line("HelloUltraviolet conformance verification")
        transcript.line("Verification result: FAIL")
        if platform.system() == "Darwin":
            transcript.line(
                "Failure: macOS verification requires Apple Silicon "
                "(arm64/aarch64), but this host reports "
                f"{platform.machine() or 'unknown'}"
            )
        else:
            transcript.line("Failure: could not infer target profile for this host")
        digest = transcript.hexdigest()
        transcript.line(f"Verification transcript SHA256: {digest}", hashed=False)
        return 2

    config = TARGETS[args.target_profile]
    if args.dry_run:
        write_header(transcript, args, config)
        write_planned_extern_payload_gate(transcript, config)
        commands, stop_label_found = gates_before_stop_label(
            planned_commands(args, config),
            args.stop_before_gate,
        )
        if not stop_label_found:
            transcript.line()
            transcript.line("Verification result: FAIL")
            transcript.line(
                f"Failure: stop-before-gate label not found: {args.stop_before_gate}"
            )
            digest = transcript.hexdigest()
            transcript.line(f"Verification transcript SHA256: {digest}", hashed=False)
            return 2
        for gate in commands:
            transcript.line()
            transcript.line(f"## Planned gate: {gate.label}")
            transcript.line(f"## Working directory: {relative_path(gate.cwd)}")
            transcript.line(f"## Command: {gate_command_text(gate)}")
            if gate.allowed_exit_codes != (0,):
                transcript.line(
                    "## Expected exit code(s): "
                    f"{allowed_exit_codes_text(gate.allowed_exit_codes)}"
                )
        transcript.line()
        transcript.line("Verification result: DRY-RUN")
        digest = transcript.hexdigest()
        transcript.line(f"Verification transcript SHA256: {digest}", hashed=False)
        return 0

    actual_host = platform.system()
    if actual_host != config.host_system:
        transcript.line("HelloUltraviolet conformance verification")
        transcript.line(f"Target profile: {config.profile}")
        transcript.line(f"Verification result: FAIL")
        transcript.line(
            "Failure: this target must be validated on "
            f"{config.host_system}, but this Python process is running on {actual_host}"
        )
        if config.host_system == "Windows":
            transcript.line(
                "Run the Windows verification from actual Windows Visual Studio "
                "Developer PowerShell, not WSL."
            )
        if config.host_system == "Darwin":
            transcript.line(
                "Run the macOS verification on Apple Silicon macOS 14+ with "
                "Xcode Command Line Tools installed."
            )
        digest = transcript.hexdigest()
        transcript.line(f"Verification transcript SHA256: {digest}", hashed=False)
        return 2
    if config.profile == "aarch64-darwin" and platform.machine().lower() not in {
        "arm64",
        "aarch64",
    }:
        transcript.line("HelloUltraviolet conformance verification")
        transcript.line(f"Target profile: {config.profile}")
        transcript.line("Verification result: FAIL")
        transcript.line(
            "Failure: aarch64-darwin must be validated on Apple Silicon "
            f"macOS, but this host reports {platform.machine() or 'unknown'}"
        )
        digest = transcript.hexdigest()
        transcript.line(f"Verification transcript SHA256: {digest}", hashed=False)
        return 2

    write_header(transcript, args, config)
    commands, stop_label_found = gates_before_stop_label(
        planned_commands(args, config),
        args.stop_before_gate,
    )
    if not stop_label_found:
        transcript.line()
        transcript.line("Verification result: FAIL")
        transcript.line(
            f"Failure: stop-before-gate label not found: {args.stop_before_gate}"
        )
        digest = transcript.hexdigest()
        transcript.line(f"Verification transcript SHA256: {digest}", hashed=False)
        return 2

    start = time.monotonic()
    failure_code = run_extern_payload_gate(transcript, config)
    if failure_code == 0:
        for gate in commands:
            exit_code = run_verification_gate(transcript, gate, config)
            if exit_code != 0:
                failure_code = exit_code
                break

    transcript.line()
    if failure_code == 0 and args.stop_before_gate:
        result_label = "STOPPED-BEFORE-GATE"
    else:
        result_label = "PASS" if failure_code == 0 else "FAIL"
    transcript.line(f"Verification result: {result_label}")
    transcript.line(f"Verification elapsed seconds: {time.monotonic() - start:.2f}")
    digest = transcript.hexdigest()
    transcript.line(f"Verification transcript SHA256: {digest}", hashed=False)
    return failure_code


if __name__ == "__main__":
    raise SystemExit(main())
