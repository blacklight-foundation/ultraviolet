#!/usr/bin/env python3
"""Fetch SHA-pinned target extern payloads without using Git LFS."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import stat
import sys
import tarfile
import tempfile
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "Bootstrap" / "extern" / "ExternManifest.json"
DEFAULT_CACHE_DIR = ROOT / "Bootstrap" / "extern" / ".cache"


@dataclass(frozen=True)
class ExternArchivePart:
    archive: str
    sha256: str
    url: str


@dataclass(frozen=True)
class ExternArchive:
    target_profile: str
    archive: str
    archive_format: str
    sha256: str
    parts: tuple[ExternArchivePart, ...]
    install_roots: tuple[Path, ...]
    required_paths: tuple[Path, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch target-specific LLVM and ICU extern payload archives."
    )
    parser.add_argument(
        "--target-profile",
        required=True,
        help="target profile whose extern payloads should be restored",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="extern archive manifest path",
    )
    parser.add_argument(
        "--cache-dir",
        type=Path,
        default=DEFAULT_CACHE_DIR,
        help="directory used to cache downloaded archives",
    )
    parser.add_argument(
        "--base-url",
        default="",
        help=(
            "override manifest base_url; useful for local file:// archive mirrors "
            "or alternate object storage"
        ),
    )
    parser.add_argument(
        "--print-only",
        action="store_true",
        help="print selected archive metadata without downloading or extracting",
    )
    return parser.parse_args()


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def manifest_entry(
    manifest: dict[str, Any],
    target_profile: str,
    base_url_override: str,
) -> ExternArchive:
    archives = manifest.get("archives")
    if not isinstance(archives, dict):
        raise ValueError("extern manifest must contain an object field named 'archives'")

    raw_entry = archives.get(target_profile)
    if not isinstance(raw_entry, dict):
        choices = ", ".join(sorted(str(key) for key in archives))
        raise ValueError(f"unknown target profile '{target_profile}'; choices: {choices}")

    archive = require_archive_name(raw_entry)
    archive_format = require_string(raw_entry, "format")
    sha256 = require_string(raw_entry, "sha256").lower()
    base_url = base_url_override or str(manifest.get("base_url", ""))
    parts = manifest_parts(raw_entry, archive, sha256, base_url)

    return ExternArchive(
        target_profile=target_profile,
        archive=archive,
        archive_format=archive_format,
        sha256=sha256,
        parts=parts,
        install_roots=tuple(relative_manifest_paths(raw_entry, "install_roots")),
        required_paths=tuple(relative_manifest_paths(raw_entry, "required_paths")),
    )


def require_string(entry: dict[str, Any], field: str) -> str:
    value = entry.get(field)
    if not isinstance(value, str) or not value:
        raise ValueError(f"extern manifest archive entry field '{field}' must be a string")
    return value


def require_archive_name(entry: dict[str, Any]) -> str:
    archive = require_string(entry, "archive")
    if archive in {".", ".."} or "/" in archive or "\\" in archive:
        raise ValueError(f"extern manifest archive entry field 'archive' must be a file name: {archive}")
    return archive


def manifest_parts(
    entry: dict[str, Any],
    archive: str,
    sha256: str,
    base_url: str,
) -> tuple[ExternArchivePart, ...]:
    raw_parts = entry.get("parts")
    if raw_parts is None:
        return (
            ExternArchivePart(
                archive=archive,
                sha256=sha256,
                url=manifest_url(entry, archive, base_url),
            ),
        )

    if not isinstance(raw_parts, list) or not raw_parts:
        raise ValueError("extern manifest archive entry field 'parts' must be a nonempty list")

    parts: list[ExternArchivePart] = []
    for raw_part in raw_parts:
        if not isinstance(raw_part, dict):
            raise ValueError("extern manifest archive entry field 'parts' contains a non-object")
        part_archive = require_archive_name(raw_part)
        parts.append(
            ExternArchivePart(
                archive=part_archive,
                sha256=require_string(raw_part, "sha256").lower(),
                url=manifest_url(raw_part, part_archive, base_url),
            )
        )
    return tuple(parts)


def manifest_url(entry: dict[str, Any], archive: str, base_url: str) -> str:
    raw_url = entry.get("url", "")
    if raw_url:
        if not isinstance(raw_url, str):
            raise ValueError("extern manifest archive entry field 'url' must be a string")
        return raw_url

    if not base_url:
        raise ValueError(f"{archive} does not provide url and manifest has no base_url")
    return f"{base_url.rstrip('/')}/{archive}"


def relative_manifest_paths(entry: dict[str, Any], field: str) -> list[Path]:
    value = entry.get(field)
    if not isinstance(value, list) or not value:
        raise ValueError(f"extern manifest archive entry field '{field}' must be a nonempty list")

    paths: list[Path] = []
    for item in value:
        if not isinstance(item, str) or not item:
            raise ValueError(f"extern manifest field '{field}' contains a non-string path")
        path = Path(item)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"extern manifest field '{field}' contains an unsafe path: {item}")
        paths.append(path)
    return paths


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_archive(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme in {"", "file"}:
        source_path = Path(urllib.request.url2pathname(parsed.path if parsed.scheme else url))
        shutil.copyfile(source_path, destination)
        return

    with urllib.request.urlopen(url) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output, length=1024 * 1024)


def ensure_archive_part(part: ExternArchivePart, cache_dir: Path) -> Path:
    part_path = cache_dir / part.archive
    if part_path.is_file() and sha256_file(part_path) == part.sha256:
        print(f"Using cached extern archive part: {part_path}")
        return part_path

    partial_path = part_path.with_name(part_path.name + ".download")
    if partial_path.exists():
        partial_path.unlink()

    print(f"Downloading extern archive part: {part.url}")
    download_archive(part.url, partial_path)
    actual = sha256_file(partial_path)
    if actual != part.sha256:
        partial_path.unlink(missing_ok=True)
        raise ValueError(
            f"extern archive part checksum mismatch for {part.archive}: "
            f"expected {part.sha256}, got {actual}"
        )
    partial_path.replace(part_path)
    return part_path


def ensure_archive(entry: ExternArchive, cache_dir: Path) -> Path:
    archive_path = cache_dir / entry.archive
    if archive_path.is_file() and sha256_file(archive_path) == entry.sha256:
        print(f"Using cached extern archive: {archive_path}")
        return archive_path

    part_paths = [ensure_archive_part(part, cache_dir) for part in entry.parts]
    if (
        len(entry.parts) == 1
        and entry.parts[0].archive == entry.archive
        and entry.parts[0].sha256 == entry.sha256
    ):
        return part_paths[0]

    partial_path = archive_path.with_name(archive_path.name + ".part")
    if partial_path.exists():
        partial_path.unlink()

    print(f"Combining extern archive parts: {archive_path}")
    with partial_path.open("wb") as output:
        for part_path in part_paths:
            with part_path.open("rb") as handle:
                shutil.copyfileobj(handle, output, length=1024 * 1024)

    actual = sha256_file(partial_path)
    if actual != entry.sha256:
        partial_path.unlink(missing_ok=True)
        raise ValueError(
            f"extern archive checksum mismatch for {entry.archive}: "
            f"expected {entry.sha256}, got {actual}"
        )
    partial_path.replace(archive_path)
    return archive_path


def safe_destination(root: Path, member_name: str) -> Path:
    if not member_name:
        raise ValueError("archive member name must not be empty")
    member_path = Path(member_name)
    if member_path.is_absolute() or ".." in member_path.parts:
        raise ValueError(f"archive member escapes extraction root: {member_name}")

    destination = root / member_name
    resolved_root = root.resolve()
    resolved_destination = destination.resolve()
    try:
        resolved_destination.relative_to(resolved_root)
    except ValueError as exc:
        raise ValueError(f"archive member escapes extraction root: {member_name}") from exc
    return destination


def apply_file_mode(path: Path, mode: int) -> None:
    if mode:
        path.chmod(mode & 0o777)


def make_writable(path: Path) -> None:
    try:
        current_mode = path.stat().st_mode
        path.chmod(current_mode | stat.S_IREAD | stat.S_IWRITE | stat.S_IEXEC)
    except OSError:
        pass


def remove_readonly_and_retry(function, path: str, _exc_info) -> None:
    make_writable(Path(path))
    function(path)


def remove_path(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path, onerror=remove_readonly_and_retry)
        return
    make_writable(path)
    path.unlink()


def validate_tar_member(destination: Path, member: tarfile.TarInfo) -> Path:
    member_destination = safe_destination(destination, member.name)
    if member.isdir() or member.isfile():
        return member_destination
    raise ValueError(f"extern archive contains unsupported tar member: {member.name}")


def extract_tar_member(
    archive: tarfile.TarFile,
    member: tarfile.TarInfo,
    destination: Path,
) -> None:
    member_destination = validate_tar_member(destination, member)
    if member.isdir():
        member_destination.mkdir(parents=True, exist_ok=True)
        apply_file_mode(member_destination, member.mode)
        return

    source = archive.extractfile(member)
    if source is None:
        raise ValueError(f"extern archive regular file could not be read: {member.name}")
    member_destination.parent.mkdir(parents=True, exist_ok=True)
    with source, member_destination.open("wb") as output:
        shutil.copyfileobj(source, output, length=1024 * 1024)
    apply_file_mode(member_destination, member.mode)


def zip_member_mode(member: zipfile.ZipInfo) -> int:
    if member.create_system != 3:
        return 0
    return member.external_attr >> 16


def validate_zip_member(destination: Path, member: zipfile.ZipInfo) -> Path:
    member_destination = safe_destination(destination, member.filename)
    mode = zip_member_mode(member)
    file_type = stat.S_IFMT(mode)
    if file_type and not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
        raise ValueError(f"extern archive contains unsupported zip member: {member.filename}")
    return member_destination


def extract_zip_member(
    archive: zipfile.ZipFile,
    member: zipfile.ZipInfo,
    destination: Path,
) -> None:
    member_destination = validate_zip_member(destination, member)
    mode = zip_member_mode(member)
    if member.is_dir() or stat.S_ISDIR(mode):
        member_destination.mkdir(parents=True, exist_ok=True)
        apply_file_mode(member_destination, mode)
        return

    member_destination.parent.mkdir(parents=True, exist_ok=True)
    with archive.open(member) as source, member_destination.open("wb") as output:
        shutil.copyfileobj(source, output, length=1024 * 1024)
    apply_file_mode(member_destination, mode)


def extract_archive(archive_path: Path, archive_format: str, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    if archive_format == "tar.gz":
        with tarfile.open(archive_path, "r:gz") as archive:
            members = archive.getmembers()
            for member in members:
                validate_tar_member(destination, member)
            for member in members:
                extract_tar_member(archive, member, destination)
        return

    if archive_format == "zip":
        with zipfile.ZipFile(archive_path) as archive:
            members = archive.infolist()
            for member in members:
                validate_zip_member(destination, member)
            for member in members:
                extract_zip_member(archive, member, destination)
        return

    raise ValueError(f"unsupported extern archive format: {archive_format}")


def require_extracted_paths(entry: ExternArchive, extraction_root: Path) -> None:
    missing = [path.as_posix() for path in entry.required_paths if not (extraction_root / path).exists()]
    if missing:
        joined = "\n  ".join(missing)
        raise FileNotFoundError(f"extern archive is missing required paths:\n  {joined}")


def install_extracted_roots(entry: ExternArchive, extraction_root: Path) -> None:
    operations: list[tuple[Path, Path, Path]] = []
    for relative_root in entry.install_roots:
        source = extraction_root / relative_root
        destination = safe_destination(ROOT, relative_root.as_posix())
        if not source.exists():
            raise FileNotFoundError(f"extern archive is missing install root: {relative_root}")
        operations.append((relative_root, source, destination))

    backup_root = Path(tempfile.mkdtemp(prefix=".extern-install-", dir=ROOT))
    backed_up_roots: list[tuple[Path, Path]] = []
    installed_roots: list[Path] = []
    try:
        for index, (_relative_root, _source, destination) in enumerate(operations):
            if destination.exists() or destination.is_symlink():
                backup = backup_root / str(index)
                backup.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(destination), str(backup))
                backed_up_roots.append((destination, backup))

        for _relative_root, source, destination in operations:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(source), str(destination))
            installed_roots.append(destination)
    except Exception as exc:
        rollback_errors: list[str] = []
        for destination in reversed(installed_roots):
            try:
                if destination.exists() or destination.is_symlink():
                    remove_path(destination)
            except Exception as rollback_exc:
                rollback_errors.append(f"{destination}: {rollback_exc}")
        for destination, backup in reversed(backed_up_roots):
            try:
                destination.parent.mkdir(parents=True, exist_ok=True)
                if destination.exists() or destination.is_symlink():
                    remove_path(destination)
                shutil.move(str(backup), str(destination))
            except Exception as rollback_exc:
                rollback_errors.append(f"{destination}: {rollback_exc}")
        if rollback_errors:
            joined = "; ".join(rollback_errors)
            raise RuntimeError(
                f"extern install failed and rollback was incomplete: {joined}"
            ) from exc
        raise
    finally:
        if backup_root.exists():
            remove_path(backup_root)


def print_entry(entry: ExternArchive) -> None:
    print(f"Target profile: {entry.target_profile}")
    print(f"Archive: {entry.archive}")
    print(f"Format: {entry.archive_format}")
    print(f"SHA256: {entry.sha256}")
    if len(entry.parts) == 1 and entry.parts[0].archive == entry.archive:
        print(f"URL: {entry.parts[0].url}")
    else:
        print("Archive parts:")
        for part in entry.parts:
            print(f"  {part.archive}")
            print(f"    SHA256: {part.sha256}")
            print(f"    URL: {part.url}")
    print("Install roots:")
    for path in entry.install_roots:
        print(f"  {path.as_posix()}")
    print("Required paths:")
    for path in entry.required_paths:
        print(f"  {path.as_posix()}")


def main() -> int:
    args = parse_args()
    try:
        manifest = load_manifest(args.manifest)
        entry = manifest_entry(manifest, args.target_profile, args.base_url)
        print_entry(entry)
        if args.print_only:
            return 0

        archive_path = ensure_archive(entry, args.cache_dir)
        with tempfile.TemporaryDirectory(prefix="extern-", dir=args.cache_dir) as temp:
            extraction_root = Path(temp)
            extract_archive(archive_path, entry.archive_format, extraction_root)
            require_extracted_paths(entry, extraction_root)
            install_extracted_roots(entry, extraction_root)
        print(f"Extern payload restored for {entry.target_profile}")
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
