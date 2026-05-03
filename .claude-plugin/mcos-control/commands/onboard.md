---
description: Pull the onboarding profile for an AI client type and present manual + copyable config.
argument-hint: <clientType: claude-code|codex|grok|chatgpt|generic-mcp>
---

Fetch the onboarding profile for client type `$1` and present it operator-friendly.

1. If `$1` is empty, list known types via `mcos_onboarding` (no arg) and ask which.
2. `mcos_onboarding clientType=$1`.
3. If MCOS returns 404 (unknown client type), suggest the closest match from the known list.
4. Format the output as three blocks:

```
=== ONBOARDING: $1 ===
Gateway URL: <gatewayMcpUrl>
Auth: <authRequired>  Trust: <trust>  Transport: <transport>

=== MANUAL INSTRUCTIONS ===
<numbered list from manualInstructions>

=== CONFIG SNIPPETS ===
For each snippet:
  --- <label> ---
  Destination: <fileName if present, else "see manual instructions">
  <content in a fenced code block>

=== VERIFICATION STEPS ===
<numbered list from verificationSteps>

=== CAVEATS ===
<bullet list, if any>
```

Don't paraphrase the snippets. Output them verbatim — the operator copies them into their AI client's config.

For ChatGPT (connector-edge), surface the caveats prominently — connector-edge has constraints other client types don't.
