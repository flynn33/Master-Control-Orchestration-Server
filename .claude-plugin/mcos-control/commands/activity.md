---
description: Show recent telemetry events, filtered by severity and category.
argument-hint: [severity: info|warning|error|critical] [category: system|gateway|worker|client|discovery|governance]
---

Surface recent MCOS telemetry events.

1. `mcos_telemetry_events?max=200`.
2. Optionally filter:
   - by `$1` if it matches a severity slug
   - by `$2` if it matches a category slug
3. Surface the filtered list, newest first, formatted:

```
TIME (ago)        SEVERITY    CATEGORY      SOURCE                 MESSAGE
12s ago           info        system        runtime                MCOS runtime constructing telemetry aggregator. PHASE-08 baseline event.
3m ago            warning     gateway       NativeHttpSysGatewayAdapter Probe returned status=degraded (httpCode=503)
...
```

Show the last 25 by default. If the operator asked for more, raise `max`.

After the list, surface a one-line summary:

```
TOTAL: <N events in window> · <X> errors · <Y> warnings
```

If there are any `error` or `critical` events, prepend an `ATTENTION:` block with the most recent one quoted in full and a pointer at the related troubleshooting section.
