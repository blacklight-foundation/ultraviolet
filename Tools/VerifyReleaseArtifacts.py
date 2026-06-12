#!/usr/bin/env python3
"""Verify platform release artifacts produced by HelloUltraviolet verification."""

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import PackageRelease


ROOT = Path(__file__).resolve().parents[1]
BUNDLED_DYLIBS = {
    "libUltravioletRTSupport.dylib",
    "libicui18n.72.dylib",
    "libicuuc.72.dylib",
    "libicudata.72.dylib",
}
BUNDLED_TOOLS = {
    "clang++",
    "llvm-ar",
    "llvm-as",
}
RPATH_PREFIXES = ("@rpath/", "@loader_path/", "@executable_path/")
SYSTEM_DYLIB_PREFIXES = ("/usr/lib/", "/System/Library/")
PACKAGED_COMPILER_RPATHS = {
    "@executable_path/macos/lib",
    "@executable_path/lib",
}
HELLO_EXECUTABLE_RPATHS = {
    "@executable_path",
    "@loader_path",
}
SHARED_LIBRARY_RPATHS = {
    "@loader_path",
}
SPECIAL_ARCHIVE_MEMBERS = {
    "/",
    "//",
    "__.SYMDEF",
    "__.SYMDEF SORTED",
    "__.SYMDEF_64",
    "__.SYMDEF_64 SORTED",
}
MACHO_HOSTED_LIFECYCLE_SECTION_ALTERNATIVES = (
    frozenset({"__mod_init_func"}),
    frozenset({"__init_offsets"}),
)
HOSTED_DYLIB_EXPORT_SYMBOLS = {
    "_uv_macos_hosted_reference_increment",
    "___ultraviolet_host_abi_version",
    "___ultraviolet_host_session_create",
    "___ultraviolet_host_session_destroy",
}
HOSTED_DYLIB_UNDEFINED_SYMBOLS = {
    "___cxa_atexit",
}
HOSTED_ABI_VERSION = 1
HOSTED_ABI_VERSION_SYMBOL = "__ultraviolet_host_abi_version"
HOSTED_SESSION_CREATE_SYMBOL = "__ultraviolet_host_session_create"
HOSTED_SESSION_DESTROY_SYMBOL = "__ultraviolet_host_session_destroy"
HOSTED_INCREMENT_SYMBOL = "uv_macos_hosted_reference_increment"


@dataclass(frozen=True)
class MacOSArtifactFixture:
    name: str
    primary_relative_path: Path
    object_relative_path: Path
    kind: str
    run_executable: bool = False
    load_dylib: bool = False
    required_sections: frozenset[str] = frozenset()
    required_section_alternatives: tuple[frozenset[str], ...] = ()
    required_exported_symbols: frozenset[str] = frozenset()
    required_undefined_symbols: frozenset[str] = frozenset()


MACOS_ARTIFACT_FIXTURES = (
    MacOSArtifactFixture(
        name="MacOSExecutableOutput",
        primary_relative_path=Path("MacOSExecutableOutput")
        / "Build"
        / "Binary"
        / "MacOSExecutableOutput",
        object_relative_path=Path("MacOSExecutableOutput")
        / "Build"
        / "Intermediate"
        / "Obj"
        / "Source"
        / "MacOSExecutableOutput.o",
        kind="executable",
        run_executable=True,
    ),
    MacOSArtifactFixture(
        name="MacOSStaticLibraryArchive",
        primary_relative_path=Path("MacOSStaticLibraryArchive")
        / "Build"
        / "Library"
        / "libMacOSStaticLibraryArchive.a",
        object_relative_path=Path("MacOSStaticLibraryArchive")
        / "Build"
        / "Intermediate"
        / "Obj"
        / "Source"
        / "MacOSStaticLibraryArchive.o",
        kind="static_archive",
    ),
    MacOSArtifactFixture(
        name="MacOSHostedSharedLibrary",
        primary_relative_path=Path("MacOSHostedSharedLibrary")
        / "Build"
        / "Binary"
        / "libMacOSHostedSharedLibrary.dylib",
        object_relative_path=Path("MacOSHostedSharedLibrary")
        / "Build"
        / "Intermediate"
        / "Obj"
        / "Source"
        / "MacOSHostedSharedLibrary.o",
        kind="dylib",
        load_dylib=True,
        required_section_alternatives=MACHO_HOSTED_LIFECYCLE_SECTION_ALTERNATIVES,
        required_exported_symbols=frozenset(HOSTED_DYLIB_EXPORT_SYMBOLS),
        required_undefined_symbols=frozenset(HOSTED_DYLIB_UNDEFINED_SYMBOLS),
    ),
    MacOSArtifactFixture(
        name="MacOSDylibLibrary",
        primary_relative_path=Path("MacOSDylibLibrary")
        / "Build"
        / "Binary"
        / "MacOSDylibLibrary",
        object_relative_path=Path("MacOSDylibLibrary")
        / "Build"
        / "Intermediate"
        / "Obj"
        / "Source"
        / "MacOSDylibLibrary.o",
        kind="executable",
        run_executable=True,
    ),
)


class VerificationError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify platform-specific release artifact structure and linkage."
    )
    parser.add_argument(
        "--platform",
        choices=("linux", "windows", "macos"),
        required=True,
        help="release platform artifact contract to verify",
    )
    parser.add_argument(
        "--compiler",
        type=Path,
        default=ROOT
        / "Bootstrap"
        / "Ultraviolet"
        / "build"
        / "macos"
        / "out"
        / "uvc",
        help="staged macOS compiler executable",
    )
    parser.add_argument(
        "--hello-executable",
        type=Path,
        default=ROOT / "HelloUltraviolet" / "Build" / "Binary" / "HelloUltraviolet",
        help="built HelloUltraviolet executable",
    )
    parser.add_argument(
        "--object-root",
        type=Path,
        default=ROOT / "HelloUltraviolet" / "Build" / "Intermediate" / "Obj",
        help="root containing HelloUltraviolet object files",
    )
    parser.add_argument(
        "--support-lib-dir",
        type=Path,
        default=ROOT
        / "Bootstrap"
        / "Ultraviolet"
        / "build"
        / "macos"
        / "out"
        / "macos"
        / "lib",
        help="staged macOS support dylib directory",
    )
    parser.add_argument(
        "--support-tool-dir",
        type=Path,
        default=ROOT
        / "Bootstrap"
        / "Ultraviolet"
        / "build"
        / "macos"
        / "out"
        / "macos"
        / "tools",
        help="staged macOS LLVM sidecar tool directory",
    )
    parser.add_argument(
        "--artifact-project-root",
        type=Path,
        default=ROOT / "HelloUltraviolet" / "Fixtures" / "ArtifactProjects",
        help="root containing macOS artifact fixture project outputs",
    )
    return parser.parse_args()


def run(command: list[str], *, cwd: Path | None = None) -> str:
    print(f"## Command: {shlex.join(command)}")
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    if result.stderr:
        print(result.stderr, end="" if result.stderr.endswith("\n") else "\n")
    if result.returncode != 0:
        raise VerificationError(
            f"command failed with exit code {result.returncode}: {shlex.join(command)}"
        )
    return result.stdout + result.stderr


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise VerificationError(f"{label} is missing: {path}")


def require_executable_file(path: Path, label: str) -> None:
    require_file(path, label)
    if (path.stat().st_mode & 0o111) == 0:
        raise VerificationError(f"{label} is not executable: {path}")


def require_directory(path: Path, label: str) -> None:
    if not path.is_dir():
        raise VerificationError(f"{label} is missing: {path}")


def require_macho_arm64(file_output: str, label: str) -> None:
    if "Mach-O 64-bit" not in file_output or "arm64" not in file_output:
        raise VerificationError(f"{label} is not reported as Mach-O 64-bit arm64")


def verify_single_file(path: Path, label: str) -> None:
    require_file(path, label)
    output = run(["file", "-L", str(path)])
    require_macho_arm64(output, label)


def verify_single_executable(path: Path, label: str) -> None:
    require_executable_file(path, label)
    output = run(["file", "-L", str(path)])
    require_macho_arm64(output, label)


def verify_object_files(object_root: Path) -> None:
    require_directory(object_root, "HelloUltraviolet object root")
    object_paths = sorted(path for path in object_root.rglob("*.o") if path.is_file())
    if not object_paths:
        raise VerificationError(f"no object files found under {object_root}")

    for path in object_paths:
        output = run(["file", "-L", str(path)])
        require_macho_arm64(output, f"object file {path}")


def dylib_basename(load_name: str) -> str:
    return load_name.rsplit("/", 1)[-1]


def otool_load_names(output: str) -> list[str]:
    load_names: list[str] = []
    for line in output.splitlines()[1:]:
        stripped = line.strip()
        if not stripped:
            continue
        load_names.append(stripped.split(" ", 1)[0])
    return load_names


def otool_rpaths(output: str) -> set[str]:
    rpaths: set[str] = set()
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped.startswith("path "):
            continue
        path = stripped.removeprefix("path ").split(" ", 1)[0]
        if path:
            rpaths.add(path)
    return rpaths


def verify_rpaths(path: Path, label: str, required_rpaths: set[str]) -> None:
    require_file(path, label)
    output = run(["otool", "-l", str(path)])
    actual_rpaths = otool_rpaths(output)
    missing = sorted(required_rpaths - actual_rpaths)
    if missing:
        expected = ", ".join(sorted(required_rpaths))
        observed = ", ".join(sorted(actual_rpaths)) or "<none>"
        raise VerificationError(
            f"{label} is missing LC_RPATH entries; expected {expected}; observed {observed}"
        )


def verify_macho_sections(path: Path, label: str, required_sections: frozenset[str]) -> None:
    if not required_sections:
        return

    output = run(["otool", "-l", str(path)])
    missing = sorted(
        section
        for section in required_sections
        if f"sectname {section}" not in output
    )
    if missing:
        expected = ", ".join(sorted(required_sections))
        raise VerificationError(f"{label} is missing Mach-O sections: {expected}")


def verify_macho_section_alternatives(
    path: Path,
    label: str,
    section_alternatives: tuple[frozenset[str], ...],
) -> None:
    if not section_alternatives:
        return

    output = run(["otool", "-l", str(path)])
    actual_sections = {
        line.strip().removeprefix("sectname ").strip()
        for line in output.splitlines()
        if line.strip().startswith("sectname ")
    }
    for alternative in section_alternatives:
        if alternative <= actual_sections:
            return

    expected = " or ".join(
        ", ".join(sorted(alternative)) for alternative in section_alternatives
    )
    observed = ", ".join(sorted(actual_sections)) or "<none>"
    raise VerificationError(
        f"{label} is missing Mach-O section alternative; expected {expected}; "
        f"observed {observed}"
    )


def bundled_rpath_loads(load_names: list[str], label: str) -> set[str]:
    bundled_loads: set[str] = set()
    for load_name in load_names:
        base_name = dylib_basename(load_name)
        if base_name not in BUNDLED_DYLIBS:
            continue
        bundled_loads.add(base_name)
        if not load_name.startswith(RPATH_PREFIXES):
            raise VerificationError(
                f"{label} loads {base_name} through non-rpath name {load_name}"
            )
    return bundled_loads


def verify_otool_load_names(path: Path, label: str) -> set[str]:
    require_file(path, label)
    output = run(["otool", "-L", str(path)])
    bundled_loads = bundled_rpath_loads(otool_load_names(output), label)

    if not bundled_loads:
        raise VerificationError(f"{label} does not load any bundled Ultraviolet dylibs")
    return bundled_loads


def verify_exported_symbols(
    path: Path,
    label: str,
    required_symbols: frozenset[str],
) -> None:
    if not required_symbols:
        return

    output = run(["nm", "-g", "-U", "-j", str(path)])
    actual_symbols = {
        line.strip()
        for line in output.splitlines()
        if line.strip()
    }
    missing = sorted(required_symbols - actual_symbols)
    if missing:
        expected = ", ".join(sorted(required_symbols))
        observed = ", ".join(sorted(actual_symbols)) or "<none>"
        raise VerificationError(
            f"{label} is missing exported symbols; expected {expected}; observed {observed}"
        )


def verify_undefined_symbols(
    path: Path,
    label: str,
    required_symbols: frozenset[str],
) -> None:
    if not required_symbols:
        return

    output = run(["nm", "-u", "-j", str(path)])
    actual_symbols = {
        line.strip()
        for line in output.splitlines()
        if line.strip()
    }
    missing = sorted(required_symbols - actual_symbols)
    if missing:
        expected = ", ".join(sorted(required_symbols))
        observed = ", ".join(sorted(actual_symbols)) or "<none>"
        raise VerificationError(
            f"{label} is missing undefined symbols; expected {expected}; observed {observed}"
        )


def run_artifact_executable(path: Path, label: str) -> None:
    require_executable_file(path, label)
    run([str(path.resolve())], cwd=path.parent)


def run_hosted_dylib_lifecycle_test(path: Path, label: str) -> None:
    require_file(path, label)
    script = "\n".join(
        [
            "import ctypes",
            "import sys",
            "import _ctypes",
            "library = ctypes.CDLL(sys.argv[1])",
            f"abi_version = getattr(library, {HOSTED_ABI_VERSION_SYMBOL!r})",
            f"create = getattr(library, {HOSTED_SESSION_CREATE_SYMBOL!r})",
            f"destroy = getattr(library, {HOSTED_SESSION_DESTROY_SYMBOL!r})",
            f"increment = getattr(library, {HOSTED_INCREMENT_SYMBOL!r})",
            "abi_version.argtypes = []",
            "abi_version.restype = ctypes.c_uint32",
            "create.argtypes = []",
            "create.restype = ctypes.c_size_t",
            "destroy.argtypes = [ctypes.c_size_t]",
            "destroy.restype = ctypes.c_uint32",
            "increment.argtypes = [ctypes.c_size_t, ctypes.c_int32]",
            "increment.restype = ctypes.c_int32",
            f"if abi_version() != {HOSTED_ABI_VERSION}:",
            "    raise SystemExit('unexpected hosted ABI version')",
            "session = create()",
            "if session == 0:",
            "    raise SystemExit('hosted session create returned 0')",
            "try:",
            "    if increment(session, 41) != 42:",
            "        raise SystemExit('hosted increment export returned wrong value')",
            "finally:",
            "    destroyed = destroy(session)",
            "if destroyed != 1:",
            "    raise SystemExit('hosted session destroy did not return 1')",
            "if destroy(session) != 0:",
            "    raise SystemExit('hosted session handle remained live after destroy')",
            "handle = library._handle",
            "del increment, destroy, create, abi_version, library",
            "if hasattr(_ctypes, 'dlclose'):",
            "    _ctypes.dlclose(handle)",
        ]
    )
    run([sys.executable, "-c", script, str(path.resolve())], cwd=path.parent)


def verify_packaged_dylib_install_name(
    name: str,
    load_names: list[str],
) -> None:
    if not load_names:
        raise VerificationError(f"packaged dylib {name} has no install name")

    expected = f"@rpath/{name}"
    install_name = load_names[0]
    if install_name != expected:
        raise VerificationError(
            f"packaged dylib {name} install name is {install_name}, expected {expected}"
        )


def verify_dylib_install_name(path: Path, label: str) -> None:
    output = run(["otool", "-L", str(path)])
    load_names = otool_load_names(output)
    if not load_names:
        raise VerificationError(f"{label} has no install name")

    expected = f"@rpath/{path.name}"
    install_name = load_names[0]
    if install_name != expected:
        raise VerificationError(
            f"{label} install name is {install_name}, expected {expected}"
        )


def verify_packaged_dylibs(support_lib_dir: Path) -> set[str]:
    require_directory(support_lib_dir, "staged macOS support dylib directory")
    bundled_loads: set[str] = set()

    for name in sorted(BUNDLED_DYLIBS):
        path = support_lib_dir / name
        verify_single_file(path, f"packaged dylib {name}")
        output = run(["otool", "-L", str(path)])
        load_names = otool_load_names(output)
        verify_packaged_dylib_install_name(name, load_names)
        bundled_loads |= bundled_rpath_loads(load_names, f"packaged dylib {name}")

    return bundled_loads


def is_packaged_tool_dependency(load_name: str, support_lib_dir: Path) -> bool:
    if load_name.startswith(SYSTEM_DYLIB_PREFIXES):
        return True

    if not load_name.startswith(RPATH_PREFIXES):
        return False

    return (support_lib_dir / dylib_basename(load_name)).is_file()


def verify_packaged_tool_load_names(
    path: Path,
    label: str,
    support_lib_dir: Path,
) -> None:
    output = run(["otool", "-L", str(path)])
    for load_name in otool_load_names(output):
        if is_packaged_tool_dependency(load_name, support_lib_dir):
            continue
        raise VerificationError(
            f"{label} loads non-system dylib not staged in macos/lib: {load_name}"
        )


def verify_packaged_tools(support_tool_dir: Path, support_lib_dir: Path) -> None:
    require_directory(support_tool_dir, "staged macOS sidecar tool directory")
    for name in sorted(BUNDLED_TOOLS):
        path = support_tool_dir / name
        label = f"packaged tool {name}"
        verify_single_executable(path, label)
        verify_packaged_tool_load_names(path, label, support_lib_dir)


def archive_member_is_special(member: str) -> bool:
    return member in SPECIAL_ARCHIVE_MEMBERS or member.startswith("__.SYMDEF")


def verify_static_archive_members(path: Path, label: str, archiver: Path) -> None:
    require_executable_file(archiver, "packaged llvm-ar")
    require_file(path, label)

    output = run(["file", "-L", str(path)])
    if "archive" not in output.lower():
        raise VerificationError(f"{label} is not reported as an archive")

    members_output = run([str(archiver), "t", str(path)])
    member_names = [
        line.strip()
        for line in members_output.splitlines()
        if line.strip() and not archive_member_is_special(line.strip())
    ]
    if not member_names:
        raise VerificationError(f"{label} contains no object members")

    with tempfile.TemporaryDirectory(prefix="uv-macos-archive-") as tmp:
        tmp_path = Path(tmp)
        run([str(archiver), "x", str(path.resolve())], cwd=tmp_path)
        extracted_members = sorted(
            member
            for member in tmp_path.rglob("*")
            if member.is_file() and not archive_member_is_special(member.name)
        )
        if not extracted_members:
            raise VerificationError(f"{label} did not extract any object members")

        for member in extracted_members:
            verify_single_file(member, f"{label} member {member.name}")


def verify_macos_artifact_fixtures(
    artifact_project_root: Path,
    support_tool_dir: Path,
) -> set[str]:
    require_directory(artifact_project_root, "macOS artifact fixture project root")
    archiver = support_tool_dir / "llvm-ar"
    bundled_loads: set[str] = set()

    for fixture in MACOS_ARTIFACT_FIXTURES:
        primary = artifact_project_root / fixture.primary_relative_path
        object_file = artifact_project_root / fixture.object_relative_path
        verify_single_file(object_file, f"{fixture.name} object file")

        if fixture.kind == "executable":
            verify_single_executable(primary, f"{fixture.name} executable")
            verify_rpaths(primary, f"{fixture.name} executable", HELLO_EXECUTABLE_RPATHS)
            bundled_loads |= verify_otool_load_names(
                primary,
                f"{fixture.name} executable",
            )
            if fixture.run_executable:
                run_artifact_executable(primary, f"{fixture.name} executable")
        elif fixture.kind == "dylib":
            verify_single_file(primary, f"{fixture.name} dylib")
            verify_dylib_install_name(primary, f"{fixture.name} dylib")
            verify_rpaths(primary, f"{fixture.name} dylib", SHARED_LIBRARY_RPATHS)
            verify_macho_sections(
                primary,
                f"{fixture.name} dylib",
                fixture.required_sections,
            )
            verify_macho_section_alternatives(
                primary,
                f"{fixture.name} dylib",
                fixture.required_section_alternatives,
            )
            verify_exported_symbols(
                primary,
                f"{fixture.name} dylib",
                fixture.required_exported_symbols,
            )
            verify_undefined_symbols(
                primary,
                f"{fixture.name} dylib",
                fixture.required_undefined_symbols,
            )
            bundled_loads |= verify_otool_load_names(
                primary,
                f"{fixture.name} dylib",
            )
            if fixture.load_dylib:
                run_hosted_dylib_lifecycle_test(primary, f"{fixture.name} dylib")
        elif fixture.kind == "static_archive":
            verify_static_archive_members(
                primary,
                f"{fixture.name} static archive",
                archiver,
            )
        else:
            raise VerificationError(
                f"{fixture.name} has unknown fixture kind {fixture.kind}"
            )

    return bundled_loads


def verify_package_inputs(platform_name: str) -> None:
    try:
        package_config = PackageRelease.DEFAULT_PACKAGES[platform_name]
    except KeyError as exc:
        raise VerificationError(f"unknown release platform: {platform_name}") from exc

    try:
        PackageRelease.require_package_inputs(package_config)
    except (FileNotFoundError, PermissionError, ValueError) as exc:
        raise VerificationError(str(exc)) from exc


def verify_macos_release_artifacts(args: argparse.Namespace) -> None:
    verify_single_executable(args.compiler, "macOS compiler executable")
    verify_single_executable(args.hello_executable, "HelloUltraviolet executable")
    verify_object_files(args.object_root)
    verify_rpaths(
        args.compiler,
        "macOS compiler executable",
        PACKAGED_COMPILER_RPATHS,
    )
    verify_rpaths(
        args.hello_executable,
        "HelloUltraviolet executable",
        HELLO_EXECUTABLE_RPATHS,
    )

    compiler_loads = verify_otool_load_names(
        args.compiler,
        "macOS compiler executable",
    )
    hello_loads = verify_otool_load_names(
        args.hello_executable,
        "HelloUltraviolet executable",
    )
    packaged_dylib_loads = verify_packaged_dylibs(args.support_lib_dir)
    verify_packaged_tools(args.support_tool_dir, args.support_lib_dir)
    fixture_loads = verify_macos_artifact_fixtures(
        args.artifact_project_root,
        args.support_tool_dir,
    )
    all_loads = compiler_loads | hello_loads | packaged_dylib_loads | fixture_loads
    missing_loads = BUNDLED_DYLIBS - all_loads
    if missing_loads:
        missing = ", ".join(sorted(missing_loads))
        raise VerificationError(f"bundled dylib load names not observed: {missing}")


def main() -> int:
    args = parse_args()
    try:
        verify_package_inputs(args.platform)
        if args.platform == "macos":
            verify_macos_release_artifacts(args)
    except (OSError, VerificationError) as exc:
        print(f"[release-artifacts:{args.platform}] FAIL {exc}")
        return 1

    print(f"[release-artifacts:{args.platform}] PASS artifact validation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
