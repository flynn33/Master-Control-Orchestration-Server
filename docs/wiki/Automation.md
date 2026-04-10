# Master Control Orchestration Server Automation

The repository is maintained by GitHub agents that handle versioning, changelog updates, generated docs, wiki synchronization, and contributor compliance.

## Automation Pipeline

```
Push to main
    |
    v
release_manager.py init
    |
    v
release_manager.py bump
    |
    v
sync_docs.py
    |
    v
git add + commit
    |
    v
gh release create
    |
    v
sync_docs.py --sync-wiki
```

## Agent Details

| Agent | Trigger | Script | What It Does |
| --- | --- | --- | --- |
| **Version Agent** | `push`, `workflow_dispatch` | `release_manager.py bump` | Calculates the next semantic version and updates `VERSION.json` |
| **Changelog Agent** | `push`, `workflow_dispatch` | `release_manager.py bump` | Rebuilds `CHANGELOG.md` and release notes |
| **Wiki + README Agent** | `push`, `workflow_dispatch` | `sync_docs.py` | Regenerates `README.md` and the wiki source pages |
| **Version Documentation Agent** | `push`, `workflow_dispatch` | `release_manager.py` | Updates `docs/versions/` and publishes GitHub releases |
| **AI Contributor Guard** | `push`, `pull_request` | `check_no_ai_contributors.py` | Rejects AI-attributed commits and co-authors |

## Current Baseline

| Property | Value |
| --- | --- |
| Current tracked release | `v0.1.61` |
| Agent commit prefix | `chore(agents):` |
| Wiki source directory | `docs/wiki/` |
| Wiki sync target | `github.com/{repo}.wiki.git` |

## Scripts

| Script | Purpose |
| --- | --- |
| `release_manager.py` | Version bumping, changelog generation, and release pages |
| `sync_docs.py` | README generation, wiki generation, and GitHub wiki sync |
| `check_no_ai_contributors.py` | Contributor compliance enforcement |
| `common.py` | Shared constants, git helpers, and file utilities |

See also: [Operations](Operations) | [Versions](Versions)
