# AGENTS.md

## Operating Mode
You must operate with a local repository-first workflow.

## Core Rules
- Use the local repository, local build/test tools, and local project documents as the primary execution path.
- Prefer direct inspection of source, tests, manifests, configs, and staged outputs before external research.
- Keep project context in-repo through code, tests, docs, and handoff notes rather than relying on external memory services.
- Validate meaningful work locally with the strongest available repo-native checks.
- External browsing or integrations should be used only when the task truly requires information that is not available in the repository or local environment.

## Required Sequence
1. Interpret the task and identify needed capabilities.
2. Read existing local context from the repository before planning or editing project work when relevant.
3. Choose the best matching local code, script, test, or document entry point.
4. Execute changes through repo-managed code and tooling.
5. Validate using local build, test, lint, review, or staged-install paths when available.
6. Record meaningful state in-repo when relevant through docs, changelog, version notes, or handoff files.
7. Use external browsing or integrations only if the answer cannot be derived locally.

## Local Handoff Minimum Content
When recording project context in-repo, include these items when relevant:
- project identifier
- current objective
- active task
- completed work
- design or architecture decisions
- constraints
- blockers or risks
- important files or modules touched
- validation results
- next steps or handoff notes
