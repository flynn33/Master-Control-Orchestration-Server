# Master Control Program Automation

The repository is maintained by a suite of GitHub agents that run automatically
on every push to `main`. These agents handle versioning, changelog updates,
documentation generation, wiki synchronization, and contributor compliance.

## Automation Pipeline

When code is pushed to `main`, the following sequence runs:

```
Push to main
    |
    v
release_manager.py init
    |
    v
release_manager.py bump          (VERSION.json, CHANGELOG.md)
    |
    v
sync_docs.py                     (README.md, docs/wiki/*.md)
    |
    v
git add + commit                 (chore(agents): sync...)
    |
    v
gh release create                (if version bumped)
    |
    v
sync_docs.py --sync-wiki         (push docs/wiki/ to GitHub wiki)
```

Commits made by the automation pipeline use the `[skip agents]` suffix to
prevent infinite loops.

## Agent Details

| Agent | Trigger | Script | What It Does |
| --- | --- | --- | --- |
| **Version Agent** | `push`, `workflow_dispatch` | `release_manager.py bump` | Reads commit history, determines the next semantic version, updates `VERSION.json` |
| **Changelog Agent** | `push`, `workflow_dispatch` | `release_manager.py bump` | Generates changelog entries for new commits, appends to `CHANGELOG.md` |
| **Wiki + README Agent** | `push`, `workflow_dispatch` | `sync_docs.py` | Regenerates `README.md` and all wiki source pages from plan documents |
| **Version Documentation Agent** | `push`, `workflow_dispatch` | `release_manager.py` | Creates release pages in `docs/versions/` and publishes GitHub Releases |
| **AI Contributor Guard** | `push`, `pull_request` | `check_no_ai_contributors.py` | Rejects commits that declare AI contributors in author or co-author fields |

## Workflows

| Workflow File | Purpose |
| --- | --- |
| `repository-maintenance-agents.yml` | Main pipeline: version bump, docs sync, release, wiki push |
| `ai-contributor-guard.yml` | Blocks AI-attributed commits on push and PR |
| `forsetti-compliance.yml` | Forsetti governance compliance checks |

## Commit Conventions

- Agent commits are prefixed with `chore(agents):`
- Agent commits include `[skip agents]` to prevent re-triggering.
- The `if: github.actor != 'github-actions[bot]'` guard prevents bot loops.

## Manual Trigger

All maintenance agents can be triggered manually via `workflow_dispatch`:

```bash
gh workflow run repository-maintenance-agents.yml
```

## Current Baseline

| Property | Value |
| --- | --- |
| Current tracked release | `v0.1.49` |
| Agent commit prefix | `chore(agents):` |
| Wiki source directory | `docs/wiki/` |
| Wiki sync target | `github.com/{repo}.wiki.git` |

## Scripts

All automation scripts are in `scripts/github_agents/`:

| Script | Purpose |
| --- | --- |
| `release_manager.py` | Version bumping, changelog generation, release notes |
| `sync_docs.py` | README, wiki page generation, and GitHub wiki sync |
| `check_no_ai_contributors.py` | AI contributor detection and rejection |
| `common.py` | Shared constants, git helpers, and file utilities |

See also: [Operations](Operations) | [Versions](Versions)
