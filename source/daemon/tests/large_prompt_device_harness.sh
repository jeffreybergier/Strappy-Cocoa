#!/bin/sh
# Fresh private directory only; production service registration is untouched.
set -eu
root="$1"
cd "$root"
if [ -e strappy.sqlite ]; then
    echo 'Refusing to reuse an existing fixture store.' >&2
    exit 1
fi
ulimit -c 0
./strappyd "$root/heartbeat.log" "$root/strappy.sqlite" "$root/resources" > console.log 2>&1 &
daemon=$!
trap 'kill "$daemon" 2>/dev/null || true; wait "$daemon" 2>/dev/null || true' 0 1 2 15
attempt=0
until ./strappyd --ipc-probe "$root/service.sock" >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 15 ]; then echo 'Fixture daemon failed to become ready.' >&2; exit 1; fi
    sleep 1
done
./large-prompt-probe "$root/service.sock" "$root/strappy.sqlite" "$root"
kill "$daemon"
wait "$daemon"
trap - 0 1 2 15
./session-options-probe
cat heartbeat.log
