#!/bin/bash
ROOT="/Users/benjaminzou/Projects/KPArb"
NODE="/opt/homebrew/bin/node"
HEARTBEAT="$ROOT/data/watchdog_heartbeat"
STALE_SECONDS=300

now=$(date +%s)
last_ms=$(cat "$HEARTBEAT" 2>/dev/null || echo 0)
age=$(( now - last_ms / 1000 ))
if pgrep -f "node dashboard/watchdog.js" >/dev/null && [ "$age" -lt "$STALE_SECONDS" ]; then
    exit 0
fi

pkill -f "node dashboard/watchdog.js"
cd "$ROOT" || exit 1
nohup "$NODE" dashboard/watchdog.js >> "$ROOT/data/watchdog.log" 2>&1 < /dev/null &

url=$(grep '^KPARB_SLACK_WEBHOOK_URL=' "$ROOT/.env" | cut -d= -f2-)
if [ -n "$url" ]; then
    curl -s -m 20 -X POST -H 'Content-Type: application/json' \
        -d "{\"text\":\":warning: The KPArb watchdog was not running or had stopped responding (last heartbeat ${age}s ago), so the guard restarted it.\"}" \
        "$url" > /dev/null
fi
echo "$(date -u +%FT%TZ) restarted the watchdog (heartbeat age ${age}s)"
