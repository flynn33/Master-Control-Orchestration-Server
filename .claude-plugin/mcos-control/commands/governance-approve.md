---
description: Review pending CLU approvals and approve / reject with operator confirmation.
argument-hint: [approval-id]
---

Hand off to the `mcos-governance-reviewer` sub-agent.

If `$1` is provided (an approval ID), the sub-agent should:
1. `mcos_governance_approvals` — find the matching item.
2. Render it in plain language with quoted payload.
3. Wait for operator decision (`approve $1` or `reject $1 reason="..."`).
4. Apply via `mcos_governance_approve` or `mcos_governance_reject confirm=true`.
5. Verify the action dropped from pending.

If `$1` is empty, list the queue:

1. `mcos_governance_approvals` filter `status=pending`.
2. Surface posture summary first (`mcos_dashboard` extracts).
3. List pending items with id, kind, requester, timestamp.
4. Wait for operator to pick one to review.

Don't auto-approve. The operator approves; this command surfaces the queue and applies decisions on instruction.
