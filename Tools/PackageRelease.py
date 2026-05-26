#!/usr/bin/env python3
"""Create public release archives consumed by the Ultraviolet installers."""

from __future__ import annotations

import argparse
import datetime as dt
import gzip
import hashlib
import os
import re
import shutil
import tarfile
import zipfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACKAGE_DOC_NAMES = ("LICENSE.md", "THIRD_PARTY_NOTICES.md", "README.md")
PACKAGE_SOURCE_BASE = "package"
REPOSITORY_SOURCE_BASE = "repository"
FILE_ENTRY = "file"
DIRECTORY_ENTRY = "directory"
ARCHIVE_TIMESTAMP = 1_704_067_200
DIRECTORY_MODE = 0o755
EXECUTABLE_MODE = 0o755
FILE_MODE = 0o644
INSTALLER_ASSET_FILES = (
    ROOT / "Tools" / "InstallUltraviolet.sh",
    ROOT / "Tools" / "InstallUltraviolet.ps1",
)
INSTALLER_ASSET_PATTERN = re.compile(r'(?:ASSET_NAME|\$AssetName)\s*=\s*"([^"]+)"')


@dataclass(frozen=True)
class PackageEntry:
    source_path: str
    staging_path: str
    archive_path: str
    kind: str = FILE_ENTRY
    executable: bool = False
    source_base: str = PACKAGE_SOURCE_BASE


@dataclass(frozen=True)
class PackageConfig:
    name: str
    source: Path
    archive_name: str
    archive_kind: str
    entries: tuple[PackageEntry, ...]


@dataclass(frozen=True)
class ArchiveInput:
    path: Path
    member_name: str
    kind: str
    executable: bool = False


DEFAULT_PACKAGES = {
    "linux": PackageConfig(
        name="linux",
        source=ROOT / "Bootstrap" / "Ultraviolet" / "build" / "linux" / "out",
        archive_name="ultraviolet-linux-x86_64.tar.gz",
        archive_kind="tar.gz",
        entries=(
            PackageEntry("uv", "uv", "ultraviolet/uv", executable=True),
            PackageEntry("uvc", "uvc", "ultraviolet/uvc", executable=True),
            PackageEntry("UltravioletRT.a", "UltravioletRT.a", "ultraviolet/UltravioletRT.a"),
            PackageEntry(
                "linux/runtime/uv_start_x86_64_sysv.o",
                "runtime/uv_start_x86_64_sysv.o",
                "ultraviolet/linux/runtime/uv_start_x86_64_sysv.o",
            ),
            PackageEntry(
                "linux/bin",
                "bin",
                "ultraviolet/linux/bin",
                kind=DIRECTORY_ENTRY,
            ),
            PackageEntry(
                "linux/tools/ld.lld",
                "tools/ld.lld",
                "ultraviolet/linux/tools/ld.lld",
                executable=True,
            ),
            PackageEntry(
                "linux/tools/llvm-ar",
                "tools/llvm-ar",
                "ultraviolet/linux/tools/llvm-ar",
                executable=True,
            ),
            PackageEntry(
                "linux/tools/llvm-as",
                "tools/llvm-as",
                "ultraviolet/linux/tools/llvm-as",
                executable=True,
            ),
            PackageEntry(
                "linux/lib/libUltravioletRTSupport.so",
                "lib/libUltravioletRTSupport.so",
                "ultraviolet/linux/lib/libUltravioletRTSupport.so",
            ),
            PackageEntry(
                "linux/lib/libicui18n.so.72",
                "lib/libicui18n.so.72",
                "ultraviolet/linux/lib/libicui18n.so.72",
            ),
            PackageEntry(
                "linux/lib/libicuuc.so.72",
                "lib/libicuuc.so.72",
                "ultraviolet/linux/lib/libicuuc.so.72",
            ),
            PackageEntry(
                "linux/lib/libicudata.so.72",
                "lib/libicudata.so.72",
                "ultraviolet/linux/lib/libicudata.so.72",
            ),
            PackageEntry(
                "linux/lib/icudt72l.dat",
                "lib/icudt72l.dat",
                "ultraviolet/linux/lib/icudt72l.dat",
            ),
        ),
    ),
    "windows": PackageConfig(
        name="windows",
        source=ROOT / "Bootstrap" / "Ultraviolet" / "build" / "windows" / "out",
        archive_name="ultraviolet-windows-x86_64.zip",
        archive_kind="zip",
        entries=(
            PackageEntry("uv.exe", "uv.exe", "ultraviolet/uv.exe"),
            PackageEntry("uvc.exe", "uvc.exe", "ultraviolet/uvc.exe"),
            PackageEntry(
                "UltravioletRT.lib",
                "UltravioletRT.lib",
                "ultraviolet/UltravioletRT.lib",
            ),
            PackageEntry(
                "windows/bin/icuuc72.dll",
                "bin/icuuc72.dll",
                "ultraviolet/windows/bin/icuuc72.dll",
            ),
            PackageEntry(
                "windows/bin/icuin72.dll",
                "bin/icuin72.dll",
                "ultraviolet/windows/bin/icuin72.dll",
            ),
            PackageEntry(
                "windows/bin/icudt72.dll",
                "bin/icudt72.dll",
                "ultraviolet/windows/bin/icudt72.dll",
            ),
            PackageEntry(
                "windows/lib/delayimp.lib",
                "lib/delayimp.lib",
                "ultraviolet/windows/lib/delayimp.lib",
            ),
            PackageEntry(
                "windows/tools/lld-link.exe",
                "tools/lld-link.exe",
                "ultraviolet/windows/tools/lld-link.exe",
            ),
            PackageEntry(
                "windows/tools/llvm-lib.exe",
                "tools/llvm-lib.exe",
                "ultraviolet/windows/tools/llvm-lib.exe",
            ),
            PackageEntry(
                "windows/tools/llvm-as.exe",
                "tools/llvm-as.exe",
                "ultraviolet/windows/tools/llvm-as.exe",
            ),
        ),
    ),
    "macos": PackageConfig(
        name="macos",
        source=ROOT / "Bootstrap" / "Ultraviolet" / "build" / "macos" / "out",
        archive_name="ultraviolet-macos-aarch64.tar.gz",
        archive_kind="tar.gz",
        entries=(
            PackageEntry("uv", "uv", "ultraviolet/uv", executable=True),
            PackageEntry("uvc", "uvc", "ultraviolet/uvc", executable=True),
            PackageEntry("UltravioletRT.a", "UltravioletRT.a", "ultraviolet/UltravioletRT.a"),
            PackageEntry(
                "macos/bin",
                "bin",
                "ultraviolet/macos/bin",
                kind=DIRECTORY_ENTRY,
            ),
            PackageEntry(
                "macos/lib/libUltravioletRTSupport.dylib",
                "lib/libUltravioletRTSupport.dylib",
                "ultraviolet/macos/lib/libUltravioletRTSupport.dylib",
            ),
            PackageEntry(
                "macos/lib/libicui18n.72.dylib",
                "lib/libicui18n.72.dylib",
                "ultraviolet/macos/lib/libicui18n.72.dylib",
            ),
            PackageEntry(
                "macos/lib/libicuuc.72.dylib",
                "lib/libicuuc.72.dylib",
                "ultraviolet/macos/lib/libicuuc.72.dylib",
            ),
            PackageEntry(
                "macos/lib/libicudata.72.dylib",
                "lib/libicudata.72.dylib",
                "ultraviolet/macos/lib/libicudata.72.dylib",
            ),
            PackageEntry(
                "macos/tools/clang++",
                "tools/clang++",
                "ultraviolet/macos/tools/clang++",
                executable=True,
            ),
            PackageEntry(
                "macos/tools/llvm-ar",
                "tools/llvm-ar",
                "ultraviolet/macos/tools/llvm-ar",
                executable=True,
            ),
            PackageEntry(
                "macos/tools/llvm-as",
                "tools/llvm-as",
                "ultraviolet/macos/tools/llvm-as",
                executable=True,
            ),
        ),
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package staged Ultraviolet compiler outputs for public release."
    )
    parser.add_argument(
        "--platform",
        choices=("linux", "windows", "macos", "all"),
        default="all",
        help="package to create; default: all",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "Build" / "Release",
        help="directory for release archives and checksums",
    )
    parser.add_argument(
        "--staging-dir",
        type=Path,
        default=ROOT / "Build" / "Staging",
        help="directory for durable per-platform release staging trees",
    )
    parser.add_argument(
        "--linux-source",
        type=Path,
        default=DEFAULT_PACKAGES["linux"].source,
        help="staged Linux compiler package root",
    )
    parser.add_argument(
        "--windows-source",
        type=Path,
        default=DEFAULT_PACKAGES["windows"].source,
        help="staged Windows compiler package root",
    )
    parser.add_argument(
        "--macos-source",
        type=Path,
        default=DEFAULT_PACKAGES["macos"].source,
        help="staged macOS compiler package root",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print planned archives without writing files",
    )
    parser.add_argument(
        "--check-release-assets",
        action="store_true",
        help="verify installer asset names match the package manifest",
    )
    return parser.parse_args()


def selected_packages(args: argparse.Namespace) -> list[PackageConfig]:
    packages = dict(DEFAULT_PACKAGES)
    packages["linux"] = PackageConfig(
        **{**packages["linux"].__dict__, "source": args.linux_source}
    )
    packages["windows"] = PackageConfig(
        **{**packages["windows"].__dict__, "source": args.windows_source}
    )
    packages["macos"] = PackageConfig(
        **{**packages["macos"].__dict__, "source": args.macos_source}
    )
    if args.platform == "all":
        return [packages["linux"], packages["windows"], packages["macos"]]
    return [packages[args.platform]]


def release_asset_names() -> set[str]:
    return {
        config.archive_name
        for config in DEFAULT_PACKAGES.values()
    }


def installer_release_asset_names() -> set[str]:
    names: set[str] = set()
    for path in INSTALLER_ASSET_FILES:
        text = path.read_text(encoding="utf-8")
        names.update(
            match.group(1)
            for match in INSTALLER_ASSET_PATTERN.finditer(text)
            if match.group(1)
        )
    return names


def verify_release_asset_contracts() -> None:
    expected = release_asset_names()
    actual = installer_release_asset_names()
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        message_parts = []
        if missing:
            message_parts.append("missing:\n  " + "\n  ".join(missing))
        if extra:
            message_parts.append("extra:\n  " + "\n  ".join(extra))
        raise ValueError(
            "installer release asset names do not match package manifest:\n"
            + "\n".join(message_parts)
        )


def checked_relative_path(value: str, label: str) -> Path:
    path = Path(value)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError(f"{label} must be a nonempty relative path: {value}")
    return path


def documentation_entries() -> tuple[PackageEntry, ...]:
    return tuple(
        PackageEntry(
            doc_name,
            doc_name,
            (Path("ultraviolet") / doc_name).as_posix(),
            source_base=REPOSITORY_SOURCE_BASE,
        )
        for doc_name in PACKAGE_DOC_NAMES
    )


def package_entries(config: PackageConfig) -> tuple[PackageEntry, ...]:
    return (*config.entries, *documentation_entries())


def entry_source_root(config: PackageConfig, entry: PackageEntry) -> Path:
    if entry.source_base == PACKAGE_SOURCE_BASE:
        return config.source
    if entry.source_base == REPOSITORY_SOURCE_BASE:
        return ROOT
    raise ValueError(f"unknown package entry source base: {entry.source_base}")


def entry_source_path(config: PackageConfig, entry: PackageEntry) -> Path:
    return entry_source_root(config, entry) / checked_relative_path(
        entry.source_path,
        "package entry source_path",
    )


def entry_staging_path(package_root: Path, entry: PackageEntry) -> Path:
    return package_root / checked_relative_path(
        entry.staging_path,
        "package entry staging_path",
    )


def entry_archive_path(entry: PackageEntry) -> str:
    return checked_relative_path(
        entry.archive_path,
        "package entry archive_path",
    ).as_posix()


def validate_entry(config: PackageConfig, entry: PackageEntry) -> None:
    source = entry_source_path(config, entry)
    if entry.kind == FILE_ENTRY:
        if not source.is_file():
            raise FileNotFoundError(f"package entry file does not exist: {source}")
        if entry.executable and os.name != "nt" and not os.access(source, os.X_OK):
            raise PermissionError(f"package entry is not executable: {source}")
        return

    if entry.kind == DIRECTORY_ENTRY:
        if not source.is_dir():
            raise FileNotFoundError(f"package entry directory does not exist: {source}")
        return

    raise ValueError(f"unknown package entry kind: {entry.kind}")


def require_package_inputs(config: PackageConfig) -> None:
    if not config.source.is_dir():
        raise FileNotFoundError(f"package source does not exist: {config.source}")
    for entry in package_entries(config):
        validate_entry(config, entry)


def copy_package_entry(
    config: PackageConfig,
    package_root: Path,
    entry: PackageEntry,
) -> None:
    source = entry_source_path(config, entry)
    destination = entry_staging_path(package_root, entry)
    if destination.exists():
        raise FileExistsError(f"staging path collision: {destination}")
    if entry.kind == DIRECTORY_ENTRY:
        destination.mkdir(parents=True)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_package_tree(config: PackageConfig, package_root: Path) -> Path:
    if package_root.exists():
        shutil.rmtree(package_root)
    package_root.mkdir(parents=True)

    for entry in package_entries(config):
        copy_package_entry(config, package_root, entry)

    return package_root


def archive_parent_members(member_name: str) -> tuple[str, ...]:
    parents = []
    for parent in Path(member_name).parents:
        if parent == Path("."):
            continue
        parents.append(parent.as_posix())
    return tuple(reversed(parents))


def expected_archive_members(config: PackageConfig) -> set[str]:
    members = {"ultraviolet"}
    for entry in package_entries(config):
        member_name = entry_archive_path(entry)
        members.update(archive_parent_members(member_name))
        members.add(member_name)
    return members


def expected_staging_members(config: PackageConfig) -> set[str]:
    members: set[str] = set()
    for entry in package_entries(config):
        staging_path = checked_relative_path(
            entry.staging_path,
            "package entry staging_path",
        ).as_posix()
        members.update(
            parent
            for parent in archive_parent_members(staging_path)
            if parent != "."
        )
        members.add(staging_path)
    return members


def actual_staging_members(package_root: Path) -> set[str]:
    return {
        path.relative_to(package_root).as_posix()
        for path in package_root.rglob("*")
    }


def require_staged_package_inputs(config: PackageConfig, package_root: Path) -> None:
    expected = expected_staging_members(config)
    actual = actual_staging_members(package_root)
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        message_parts = []
        if missing:
            message_parts.append("missing:\n  " + "\n  ".join(missing))
        if extra:
            message_parts.append("extra:\n  " + "\n  ".join(extra))
        raise FileNotFoundError(
            f"{config.name} package staging does not match manifest:\n"
            + "\n".join(message_parts)
        )


def iter_archive_inputs(config: PackageConfig, package_root: Path):
    yielded_directories: set[str] = set()

    def yield_directory(member_name: str, source: Path):
        if member_name in yielded_directories:
            return
        yielded_directories.add(member_name)
        yield ArchiveInput(source, member_name, DIRECTORY_ENTRY, executable=True)

    for item in yield_directory("ultraviolet", package_root):
        yield item

    for entry in package_entries(config):
        member_name = entry_archive_path(entry)
        for parent in archive_parent_members(member_name):
            for item in yield_directory(parent, package_root):
                yield item
        yield ArchiveInput(
            entry_staging_path(package_root, entry),
            member_name,
            entry.kind,
            entry.executable,
        )


def normalized_tar_info(archive_input: ArchiveInput) -> tarfile.TarInfo:
    info = tarfile.TarInfo(archive_input.member_name)
    info.mtime = ARCHIVE_TIMESTAMP
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    if archive_input.kind == DIRECTORY_ENTRY:
        info.type = tarfile.DIRTYPE
        info.mode = DIRECTORY_MODE
        return info

    info.type = tarfile.REGTYPE
    info.size = archive_input.path.stat().st_size
    info.mode = EXECUTABLE_MODE if archive_input.executable else FILE_MODE
    return info


def create_tar_gz(config: PackageConfig, package_root: Path, archive_path: Path) -> None:
    with archive_path.open("wb") as output:
        with gzip.GzipFile(
            filename="",
            mode="wb",
            fileobj=output,
            mtime=ARCHIVE_TIMESTAMP,
        ) as gzip_output:
            with tarfile.open(fileobj=gzip_output, mode="w") as archive:
                for archive_input in iter_archive_inputs(config, package_root):
                    info = normalized_tar_info(archive_input)
                    if archive_input.kind == DIRECTORY_ENTRY:
                        archive.addfile(info)
                        continue
                    with archive_input.path.open("rb") as source:
                        archive.addfile(info, source)


def zip_timestamp() -> tuple[int, int, int, int, int, int]:
    return datetime_tuple(ARCHIVE_TIMESTAMP)


def datetime_tuple(timestamp: int) -> tuple[int, int, int, int, int, int]:
    value = dt.datetime.fromtimestamp(timestamp, tz=dt.UTC)
    return (value.year, value.month, value.day, value.hour, value.minute, value.second)


def zip_info(member_name: str, kind: str, executable: bool = False) -> zipfile.ZipInfo:
    name = member_name.rstrip("/") + "/" if kind == DIRECTORY_ENTRY else member_name
    info = zipfile.ZipInfo(name, zip_timestamp())
    info.create_system = 3
    mode = DIRECTORY_MODE if kind == DIRECTORY_ENTRY else (
        EXECUTABLE_MODE if executable else FILE_MODE
    )
    file_type = 0o040000 if kind == DIRECTORY_ENTRY else 0o100000
    info.external_attr = (file_type | mode) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    return info


def create_zip(config: PackageConfig, package_root: Path, archive_path: Path) -> None:
    with zipfile.ZipFile(
        archive_path,
        mode="w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        written_directories: set[str] = set()

        def write_directory(member_name: str) -> None:
            directory_name = member_name.rstrip("/") + "/"
            if directory_name in written_directories:
                return
            written_directories.add(directory_name)
            archive.writestr(zip_info(directory_name, DIRECTORY_ENTRY), "")

        for archive_input in iter_archive_inputs(config, package_root):
            if archive_input.kind == DIRECTORY_ENTRY:
                write_directory(archive_input.member_name)
                continue

            for parent in reversed(Path(archive_input.member_name).parents):
                if parent == Path("."):
                    continue
                write_directory(parent.as_posix())
            with archive_input.path.open("rb") as source:
                archive.writestr(
                    zip_info(
                        archive_input.member_name,
                        FILE_ENTRY,
                        archive_input.executable,
                    ),
                    source.read(),
                )


def write_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    hexdigest = digest.hexdigest()
    path.with_suffix(path.suffix + ".sha256").write_text(
        f"{hexdigest}  {path.name}\n",
        encoding="utf-8",
    )
    return hexdigest


def normalized_archive_member(name: str) -> str:
    return name.rstrip("/")


def actual_archive_members(config: PackageConfig, archive_path: Path) -> set[str]:
    if config.archive_kind == "tar.gz":
        with tarfile.open(archive_path, "r:gz") as archive:
            return {normalized_archive_member(name) for name in archive.getnames()}
    if config.archive_kind == "zip":
        with zipfile.ZipFile(archive_path) as archive:
            return {
                normalized_archive_member(name)
                for name in archive.namelist()
                if normalized_archive_member(name)
            }
    raise ValueError(f"unknown archive kind: {config.archive_kind}")


def verify_archive_members(config: PackageConfig, archive_path: Path) -> None:
    expected = expected_archive_members(config)
    actual = actual_archive_members(config, archive_path)
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        message_parts = []
        if missing:
            message_parts.append("missing:\n  " + "\n  ".join(missing))
        if extra:
            message_parts.append("extra:\n  " + "\n  ".join(extra))
        raise ValueError(
            f"{config.name} release archive does not match manifest:\n"
            + "\n".join(message_parts)
        )


def package(config: PackageConfig, output_dir: Path, staging_dir: Path, dry_run: bool) -> None:
    archive_path = output_dir / config.archive_name
    package_root = staging_dir / config.name
    print(f"{config.name}: {config.source} -> {package_root} -> {archive_path}")
    if dry_run:
        return

    require_package_inputs(config)

    output_dir.mkdir(parents=True, exist_ok=True)
    staging_dir.mkdir(parents=True, exist_ok=True)
    package_root = copy_package_tree(config, package_root)
    require_staged_package_inputs(config, package_root)

    if archive_path.exists():
        archive_path.unlink()
    checksum_path = archive_path.with_suffix(archive_path.suffix + ".sha256")
    if checksum_path.exists():
        checksum_path.unlink()

    if config.archive_kind == "tar.gz":
        create_tar_gz(config, package_root, archive_path)
    elif config.archive_kind == "zip":
        create_zip(config, package_root, archive_path)
    else:
        raise ValueError(f"unknown archive kind: {config.archive_kind}")

    verify_archive_members(config, archive_path)
    digest = write_sha256(archive_path)
    print(f"{archive_path.name}: {digest}")


def main() -> int:
    args = parse_args()
    if args.check_release_assets:
        verify_release_asset_contracts()
        print("release asset contract ok")
        return 0

    for config in selected_packages(args):
        package(config, args.output_dir, args.staging_dir, args.dry_run)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
