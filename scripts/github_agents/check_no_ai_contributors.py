from __future__ import annotations

import argparse
import re
import sys

from common import ROOT, git_output


ALLOWED_BOT_NAMES = {"github-actions[bot]", "dependabot[bot]", "renovate[bot]"}
AI_PATTERNS = (
    r"\bchatgpt\b",
    r"\bcodex\b",
    r"\bclaude\b",
    r"\bcopilot\b",
    r"\bgemini\b",
    r"\bgrok\b",
    r"\bopenai\b",
    r"\banthropic\b",
    r"\bdeepseek\b",
    r"\bperplexity\b",
    r"\bx\.ai\b",
)
AI_REGEX = re.compile("|".join(AI_PATTERNS), re.IGNORECASE)
TRAILER_PREFIXES = ("co-authored-by:", "contributed-by:", "pair-programmed-by:")
ZERO_SHA = "0" * 40


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Reject commits that declare AI contributors.")
    parser.add_argument("--event-name", default="")
    parser.add_argument("--before", default="")
    parser.add_argument("--after", default="")
    parser.add_argument("--pr-base", default="")
    parser.add_argument("--pr-head", default="")
    parser.add_argument("--hook", action="store_true", help="Read pre-push ref information from stdin.")
    return parser.parse_args()


def parse_commit(commit: str) -> dict:
    format_string = "%H%x1f%an%x1f%ae%x1f%cn%x1f%ce%x1f%B"
    payload = git_output(["show", "-s", f"--format={format_string}", commit])
    sha, author_name, author_email, committer_name, committer_email, message = payload.split("\x1f", 5)
    return {
        "sha": sha,
        "author_name": author_name.strip(),
        "author_email": author_email.strip(),
        "committer_name": committer_name.strip(),
        "committer_email": committer_email.strip(),
        "message": message.strip(),
    }


def collect_range_commits(revision_range: str) -> list[str]:
    output = git_output(["rev-list", "--reverse", revision_range])
    return [line.strip() for line in output.splitlines() if line.strip()]


def commits_for_hook() -> list[str]:
    commits: list[str] = []
    seen: set[str] = set()
    for raw_line in sys.stdin.read().splitlines():
        line = raw_line.strip()
        if not line:
            continue

        local_ref, local_sha, _remote_ref, remote_sha = line.split()
        if local_sha == ZERO_SHA:
            continue

        if remote_sha == ZERO_SHA:
            output = git_output(["rev-list", "--reverse", local_sha, "--not", "--remotes"])
            candidates = [item.strip() for item in output.splitlines() if item.strip()]
        else:
            candidates = collect_range_commits(f"{remote_sha}..{local_sha}")

        for candidate in candidates:
            if candidate in seen:
                continue
            seen.add(candidate)
            commits.append(candidate)

    return commits


def commits_for_event(args: argparse.Namespace) -> list[str]:
    if args.event_name == "pull_request" and args.pr_base and args.pr_head:
        return collect_range_commits(f"{args.pr_base}..{args.pr_head}")
    if args.before and args.before != ZERO_SHA and args.after:
        return collect_range_commits(f"{args.before}..{args.after}")
    if args.after:
        return [args.after]
    return [git_output(["rev-parse", "HEAD"])]


def trailers(message: str) -> list[str]:
    values: list[str] = []
    for line in message.splitlines():
        normalized = line.strip()
        if not normalized:
            continue
        if normalized.lower().startswith(TRAILER_PREFIXES):
            values.append(normalized)
    return values


def identity_is_allowed(identity: str) -> bool:
    return identity in ALLOWED_BOT_NAMES


def inspect_commit(commit: dict) -> list[str]:
    findings: list[str] = []
    identities = (
        ("author", commit["author_name"]),
        ("author email", commit["author_email"]),
        ("committer", commit["committer_name"]),
        ("committer email", commit["committer_email"]),
    )
    for label, value in identities:
        if not value or identity_is_allowed(value):
            continue
        if AI_REGEX.search(value):
            findings.append(f"{label} identity matched AI contributor rule: {value}")

    for trailer in trailers(commit["message"]):
        if AI_REGEX.search(trailer):
            findings.append(f"commit trailer matched AI contributor rule: {trailer}")

    return findings


def main() -> int:
    args = parse_args()
    commits = commits_for_hook() if args.hook else commits_for_event(args)
    violations: list[tuple[str, list[str]]] = []
    for commit_sha in commits:
        details = parse_commit(commit_sha)
        findings = inspect_commit(details)
        if findings:
            violations.append((commit_sha, findings))

    if not violations:
        return 0

    print("AI contributor rule violation detected:", file=sys.stderr)
    for commit_sha, findings in violations:
        print(f"- {commit_sha}", file=sys.stderr)
        for finding in findings:
            print(f"  - {finding}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
