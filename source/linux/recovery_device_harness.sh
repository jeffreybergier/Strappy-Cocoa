#!/bin/sh
# Requires a fresh private directory containing strappyd, probe and resources/.
set -eu
root="$1"
cd "$root"
if [ -e strappy.sqlite ] || [ -e tool.pid ]; then
    echo "Refusing to overwrite an existing fixture." >&2
    exit 1
fi
./strappyd "$root/heartbeat.log" "$root/strappy.sqlite" "$root/resources" > console.log 2>&1 &
daemon=$!
trap 'kill "$daemon" 2>/dev/null || true' 0 1 2 15
sleep 2
./strappyd --ipc-probe "$root/service.sock"
kill "$daemon"
wait "$daemon"
./probe seed "$root/strappy.sqlite" "$root"
./strappyd "$root/heartbeat.log" "$root/strappy.sqlite" "$root/resources" >> console.log 2>&1 &
daemon=$!
sleep 2
./strappyd --ipc-probe "$root/service.sock"
sleep 32
sleep 30
./strappyd --ipc-probe "$root/service.sock"
kill "$daemon"
wait "$daemon"
trap - 0 1 2 15
./probe check "$root/strappy.sqlite" "$root"
./probe bash "$root/strappy.sqlite" "$root" > bash.log 2>&1 &
owner=$!
i=0
while [ ! -s tool.pid ]; do
    sleep 1
    i=$((i + 1))
    if [ "$i" -ge 10 ]; then kill "$owner"; exit 1; fi
done
child=$(cat tool.pid)
kill -9 "$owner"
wait "$owner" 2>/dev/null || true
sleep 2
if kill -0 "$child" 2>/dev/null; then
    echo 'FAIL: tool survived owner death'
    exit 1
fi
echo 'PASS: native recovery, idle IPC, SIGTERM, crash-owned tool cleanup'
cat heartbeat.log
