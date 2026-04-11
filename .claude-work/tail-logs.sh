#!/usr/bin/env bash
# Live log snapshot helper for the Claude autonomous testing session.
# Usage: ./tail-logs.sh [lines]   (default 20)
LINES="${1:-20}"
PUBLIC="/c/Users/Public/Documents/Master Control Orchestration Server/logs"
INSTALLER="$PUBLIC/installer"

echo "================================================================"
echo " MASTER CONTROL LIVE LOG SNAPSHOT   $(date '+%Y-%m-%d %H:%M:%S')"
echo "================================================================"
echo
echo "-- running processes --"
tasklist 2>/dev/null | grep -i "MasterControl" | head
echo
echo "-- service-latest.log (tail -$LINES) --"
tail -n "$LINES" "$INSTALLER/components/service-latest.log" 2>/dev/null || echo "  (not yet created)"
echo
echo "-- shell-latest.log (tail -$LINES) --"
tail -n "$LINES" "$INSTALLER/components/shell-latest.log" 2>/dev/null || echo "  (not yet created)"
echo
echo "-- installer-latest.json (if any) --"
if [ -f "$INSTALLER/installer-latest.json" ]; then
  python -c "
import json
try:
    d = json.load(open(r'$INSTALLER/installer-latest.json'))
    print(f\"  component={d.get('component','?')}  action={d.get('action','?')}  outcome={d.get('outcome','?')}\")
    print(f\"  generatedAtLocal={d.get('generatedAtLocal','?')}\")
    if d.get('message'): print(f\"  message={d.get('message')}\")
except Exception as e:
    print(f'  (parse error: {e})')
"
fi
echo
echo "-- admin API health --"
curl -sS --max-time 3 -o /dev/null -w "  HTTP %{http_code}  /api/config  |  time %{time_total}s\n" http://127.0.0.1:7300/api/config 2>&1
