from __future__ import annotations

import argparse
import re
import sys

from common import ROOT, git_output


ALLOWED_BOT_NAMES = {"github-actions[bot]", "dependabot[bot]", "renovate[bot]"}

# AI vendor / model / tool name patterns. Matched word-bounded, case-insensitive.
# Audited 2026-05-12 for completeness against then-current AI coding tools.
AI_PATTERNS = (
    # Anthropic
    r"\bclaude\b",
    r"\banthropic\b",
    # OpenAI
    r"\bchatgpt\b",
    r"\bcodex\b",
    r"\bopenai\b",
    # Google
    r"\bgemini\b",
    r"\bbard\b",
    # GitHub / Microsoft
    r"\bcopilot\b",
    # xAI
    r"\bgrok\b",
    r"\bx\.ai\b",
    # Other major AI vendors
    r"\bdeepseek\b",
    r"\bperplexity\b",
    r"\bmistral\b",
    r"\bmeta-ai\b",
    r"\bllama\b",
    # AI-coding-tool brands
    r"\bcursor\b",
    r"\btabnine\b",
    r"\bcodewhisperer\b",
    r"\bamazon\s*q\b",
    r"\bcontinue\.dev\b",
    r"\bsourcegraph\s*cody\b",
    r"\bcody\b",
    r"\bwindsurf\b",
    r"\baider\b",
    r"\bphind\b",
    # Generic AI authorship tokens
    r"\bllm\b",
    r"\bai-generated\b",
    r"\bai-authored\b",
    r"\bgenerated\s+with\s+ai\b",
    r"\bgenerated\s+by\s+ai\b",
    r"\bauthored\s+by\s+ai\b",
    r"\bwritten\s+by\s+ai\b",
    # Robot/AI emoji often used as authorship marker (escaped Unicode codepoints)
    "\U0001F916",  # robot face emoji
    "\U0001F9E0",  # brain emoji
)
AI_REGEX = re.compile("|".join(AI_PATTERNS), re.IGNORECASE)

# Trailer prefixes scanned for AI vendor identities. Includes signed-off-by
# (added 2026-05-12) because AI-tool commit signers historically leak through
# the SoB trailer when DCO sign-off is enforced upstream.
TRAILER_PREFIXES = (
    "co-authored-by:",
    "contributed-by:",
    "pair-programmed-by:",
    "signed-off-by:",
    "written-by:",
    "authored-by:",
    "generated-by:",
    "assisted-by:",
)

# Body-line patterns scanned across the entire commit message (NOT only
# trailer-prefixed lines). Catches free-form attribution in commit prose like
# "Generated with Claude Code", a 🤖 emoji on its own line, etc.
BODY_LINE_AI_PATTERNS = (
    r"generated\s+with\s+\w+",
    r"generated\s+by\s+\w+",
    r"\bvia\s+(claude\s+code|cursor|copilot|chatgpt|codex|grok)\b",
    r"\bauthored\s+by\s+(claude|chatgpt|codex|grok|copilot|cursor)\b",
    r"powered\s+by\s+(claude|gpt|chatgpt|anthropic|openai|gemini)",
    "\U0001F916",  # robot emoji anywhere in the body
)
BODY_LINE_REGEX = re.compile("|".join(BODY_LINE_AI_PATTERNS), re.IGNORECASE)

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

    # v0.10.14 alignment: scan the entire commit body for free-form AI
    # attribution that's NOT in a recognised trailer line. The audit found
    # patterns like "Generated with Claude Code" historically slipped past the
    # trailer-only check. Trailer matches above already cover Co-Authored-By
    # style; this pass adds prose / body-line coverage.
    #
    # Documentation-vs-attribution heuristic: skip matches that are inside a
    # fenced code block, or where the matched substring is inside a quote
    # (`"..."`, `'...'`, or `` `...` ``) -- those are documentation tokens
    # describing the pattern, not actual authorship attribution. This
    # prevents false-positives on commit messages that describe what the
    # guard catches (a real situation in this repo's commit history).
    in_code_fence = False
    for line in commit["message"].splitlines():
        normalized = line.strip()
        if not normalized:
            continue
        # Track fenced code blocks; skip everything inside them.
        if normalized.startswith("```"):
            in_code_fence = not in_code_fence
            continue
        if in_code_fence:
            continue
        # Don't double-report a trailer line.
        if normalized.lower().startswith(TRAILER_PREFIXES):
            continue
        # Skip markdown blockquote / inline-quote-only lines.
        if normalized.startswith(">"):
            continue

        match = BODY_LINE_REGEX.search(normalized)
        if not match:
            continue

        # If the matched substring is inside quote characters, treat as
        # documentation. Look at the 4 chars before the match start; if any
        # of `"`, `'`, `` ` `` appear, assume the pattern is quoted text.
        prefix_window = normalized[max(0, match.start() - 4):match.start()]
        if any(q in prefix_window for q in ('"', "'", "`")):
            continue

        findings.append(f"commit body line matched AI authorship pattern: {normalized}")

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
