#!/usr/bin/env python3
"""Resolve compiler release intent, version, and validation runs for CI."""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


REPOSITORY = "blacklight-foundation/ultraviolet"
RELEASE_LABEL_PREFIX = "release: "
RELEASE_LABELS = {
    "release: none": "none",
    "release: canary": "canary",
    "release: patch": "patch",
    "release: minor": "minor",
    "release: breaking": "breaking",
}
VALIDATION_WORKFLOWS = {
    "linux": {
        "workflow": "hello-verification-linux.yml",
        "artifact": "ultraviolet-linux-x86_64",
    },
    "macos": {
        "workflow": "hello-verification-macos.yml",
        "artifact": "ultraviolet-macos-aarch64",
    },
    "windows": {
        "workflow": "hello-verification-windows.yml",
        "artifact": "ultraviolet-windows-x86_64",
    },
}
VERSION_RE = re.compile(r"^v(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)-alpha(?:\.(?P<canary>[0-9A-Za-z.-]+))?$")


@dataclasses.dataclass(frozen=True, order=True)
class CompilerVersion:
    major: int
    minor: int
    patch: int

    @property
    def version(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}-alpha"

    @property
    def tag(self) -> str:
        return f"v{self.version}"

    def bump_patch(self) -> "CompilerVersion":
        return CompilerVersion(self.major, self.minor, self.patch + 1)

    def bump_minor(self) -> "CompilerVersion":
        return CompilerVersion(self.major, self.minor + 1, 0)


@dataclasses.dataclass
class ReleasePlan:
    intent: str
    version: str = ""
    tag: str = ""
    title: str = ""
    prerelease: bool = False
    should_publish: bool = False
    reason: str = ""
    source_pr: str = ""
    source_labels: tuple[str, ...] = ()
    validation_run_ids: dict[str, str] = dataclasses.field(default_factory=dict)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Resolve the compiler release plan for a main-branch commit."
    )
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", REPOSITORY))
    parser.add_argument("--sha", default=os.environ.get("GITHUB_SHA", ""))
    parser.add_argument(
        "--release-kind",
        choices=("auto", "none", "canary", "patch", "minor", "breaking"),
        default="auto",
    )
    parser.add_argument("--latest-release-tag", default="")
    parser.add_argument("--skip-validation", action="store_true")
    parser.add_argument("--require-validations", action="store_true")
    parser.add_argument("--write-env", default="")
    parser.add_argument("--write-output", default="")
    parser.add_argument("--summary", default="")
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()


def github_token() -> str:
    return os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN", "")


def github_api(repository: str, path: str, token: str = "") -> Any:
    url = f"https://api.github.com/repos/{repository}{path}"
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            **({"Authorization": f"Bearer {token}"} if token else {}),
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        details = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GitHub API request failed: {url}: {exc.code} {details}") from exc


def parse_compiler_tag(tag: str) -> CompilerVersion | None:
    match = VERSION_RE.match(tag)
    if not match or match.group("canary"):
        return None
    return CompilerVersion(
        int(match.group("major")),
        int(match.group("minor")),
        int(match.group("patch")),
    )


def latest_compiler_release(repository: str, token: str, explicit_tag: str) -> CompilerVersion:
    if explicit_tag:
        parsed = parse_compiler_tag(explicit_tag)
        if parsed is None:
            raise ValueError(f"latest release tag is not a compiler alpha tag: {explicit_tag}")
        return parsed

    releases = github_api(repository, "/releases?per_page=100", token)
    candidates: list[CompilerVersion] = []
    for release in releases:
        if release.get("draft") or release.get("prerelease"):
            continue
        parsed = parse_compiler_tag(str(release.get("tag_name", "")))
        if parsed is not None:
            candidates.append(parsed)
    if not candidates:
        raise RuntimeError("no non-prerelease compiler releases with tags matching v#.#.#-alpha")
    return max(candidates)


def associated_pr_labels(repository: str, sha: str, token: str) -> tuple[str, tuple[str, ...]]:
    if not sha:
        return "", ()
    pulls = github_api(repository, f"/commits/{sha}/pulls", token)
    if not pulls:
        return "", ()

    pull = pulls[0]
    number = str(pull.get("number", ""))
    issue = github_api(repository, f"/issues/{number}", token)
    labels = tuple(
        str(label.get("name", ""))
        for label in issue.get("labels", [])
        if isinstance(label, dict)
    )
    return number, labels


def resolve_intent(
    repository: str,
    sha: str,
    requested_kind: str,
    token: str,
) -> tuple[str, str, tuple[str, ...]]:
    if requested_kind != "auto":
        return requested_kind, "", ()

    pr_number, labels = associated_pr_labels(repository, sha, token)
    release_labels = [label for label in labels if label.startswith(RELEASE_LABEL_PREFIX)]
    selected = [RELEASE_LABELS[label] for label in release_labels if label in RELEASE_LABELS]
    distinct = sorted(set(selected))
    if not distinct:
        return "none", pr_number, labels
    if len(distinct) > 1:
        raise RuntimeError(
            "conflicting release labels on associated PR: " + ", ".join(release_labels)
        )
    return distinct[0], pr_number, labels


def successful_validation_runs(
    repository: str,
    sha: str,
    token: str,
) -> dict[str, str]:
    runs: dict[str, str] = {}
    query = urllib.parse.urlencode({"branch": "main", "event": "push", "per_page": "30"})
    for platform, config in VALIDATION_WORKFLOWS.items():
        workflow = config["workflow"]
        data = github_api(repository, f"/actions/workflows/{workflow}/runs?{query}", token)
        for run in data.get("workflow_runs", []):
            if (
                run.get("head_sha") == sha
                and run.get("status") == "completed"
                and run.get("conclusion") == "success"
            ):
                runs[platform] = str(run.get("id"))
                break
    return runs


def tag_exists(repository: str, tag: str, token: str) -> bool:
    encoded = urllib.parse.quote(tag, safe="")
    try:
        github_api(repository, f"/git/ref/tags/{encoded}", token)
        return True
    except RuntimeError as exc:
        if " 404 " in str(exc):
            return False
        raise


def commit_date_suffix(repository: str, sha: str, token: str) -> str:
    if sha and token:
        commit = github_api(repository, f"/commits/{sha}", token)
        raw_date = (
            commit.get("commit", {})
            .get("committer", {})
            .get("date", "")
        )
        if raw_date:
            parsed = dt.datetime.fromisoformat(raw_date.replace("Z", "+00:00"))
            return parsed.astimezone(dt.timezone.utc).strftime("%Y%m%d")
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d")


def canary_suffix(repository: str, sha: str, token: str) -> str:
    today = commit_date_suffix(repository, sha, token)
    short_sha = (sha or "unknown")[:7]
    return f"{today}.{short_sha}"


def build_plan(args: argparse.Namespace) -> ReleasePlan:
    token = github_token()
    needs_github = (
        args.release_kind == "auto"
        or not args.latest_release_tag
        or args.require_validations
    )
    if needs_github and not token:
        raise RuntimeError("GITHUB_TOKEN or GH_TOKEN is required for release planning")

    intent, pr_number, labels = resolve_intent(
        args.repository,
        args.sha,
        args.release_kind,
        token,
    )
    plan = ReleasePlan(intent=intent, source_pr=pr_number, source_labels=labels)
    if intent == "none":
        plan.reason = "release intent is none"
        return plan

    base = latest_compiler_release(args.repository, token, args.latest_release_tag)
    if intent in ("minor", "breaking"):
        next_version = base.bump_minor()
        plan.version = next_version.version
        plan.tag = next_version.tag
        plan.title = f"Ultraviolet {plan.version}"
    elif intent == "patch":
        next_version = base.bump_patch()
        plan.version = next_version.version
        plan.tag = next_version.tag
        plan.title = f"Ultraviolet {plan.version}"
    elif intent == "canary":
        next_version = base.bump_patch()
        suffix = canary_suffix(args.repository, args.sha, token)
        plan.version = f"{next_version.version}.{suffix}"
        plan.title = f"Ultraviolet canary artifacts {plan.version}"
        plan.prerelease = True
    else:
        raise RuntimeError(f"unsupported release intent: {intent}")

    if not args.skip_validation or args.require_validations:
        plan.validation_run_ids = successful_validation_runs(args.repository, args.sha, token)
        missing = sorted(set(VALIDATION_WORKFLOWS) - set(plan.validation_run_ids))
        if missing and args.require_validations:
            plan.reason = "missing successful validation runs: " + ", ".join(missing)
            return plan

    if intent == "canary":
        plan.reason = "canary artifacts are retained on validation workflow runs; no release tag is created"
        return plan

    if token and tag_exists(args.repository, plan.tag, token):
        plan.reason = f"release tag already exists: {plan.tag}"
        return plan

    plan.should_publish = True
    plan.reason = "release is ready to publish"
    return plan


def env_value(value: str | bool) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return value


def plan_env(plan: ReleasePlan) -> dict[str, str]:
    values = {
        "UV_RELEASE_INTENT": plan.intent,
        "UV_RELEASE_VERSION": plan.version,
        "UV_RELEASE_TAG": plan.tag,
        "UV_RELEASE_TITLE": plan.title,
        "UV_RELEASE_PRERELEASE": env_value(plan.prerelease),
        "UV_RELEASE_SHOULD_PUBLISH": env_value(plan.should_publish),
        "UV_RELEASE_REASON": plan.reason,
        "UV_RELEASE_SOURCE_PR": plan.source_pr,
    }
    for platform in VALIDATION_WORKFLOWS:
        values[f"UV_VALIDATION_{platform.upper()}_RUN_ID"] = plan.validation_run_ids.get(
            platform,
            "",
        )
    return values


def write_key_values(path: str, values: dict[str, str]) -> None:
    if not path:
        return
    with Path(path).open("a", encoding="utf-8", newline="\n") as handle:
        for key, value in values.items():
            handle.write(f"{key}={value}\n")


def summary_text(plan: ReleasePlan) -> str:
    labels = ", ".join(plan.source_labels) if plan.source_labels else "none"
    lines = [
        "# Compiler Release Plan",
        "",
        f"- Intent: `{plan.intent}`",
        f"- Version: `{plan.version or 'none'}`",
        f"- Tag: `{plan.tag or 'none'}`",
        f"- Prerelease: `{str(plan.prerelease).lower()}`",
        f"- Breaking change: `{str(plan.intent == 'breaking').lower()}`",
        f"- Should publish: `{str(plan.should_publish).lower()}`",
        f"- Reason: {plan.reason}",
        f"- Source PR: `{plan.source_pr or 'none'}`",
        f"- Source labels: {labels}",
    ]
    if plan.validation_run_ids:
        lines.append("- Validation runs:")
        for platform, run_id in sorted(plan.validation_run_ids.items()):
            lines.append(f"  - {platform}: `{run_id}`")
    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()
    try:
        plan = build_plan(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    values = plan_env(plan)
    write_key_values(args.write_env, values)
    write_key_values(args.write_output, values)
    if args.summary:
        Path(args.summary).write_text(summary_text(plan), encoding="utf-8", newline="\n")
    if args.json:
        print(json.dumps(dataclasses.asdict(plan), sort_keys=True))
    else:
        print(summary_text(plan), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
