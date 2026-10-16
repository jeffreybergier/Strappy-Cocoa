#!/bin/sh
# Fresh staging only, as the normal app user. No provider requests or Keychain writes.
set -eu
root="$1"
cd "$root"
if [ -e fixture.sqlite ]; then
  echo 'Refusing to reuse a maintenance fixture store.' >&2
  exit 1
fi
mkdir scan
ulimit -c 0
./strappyd "$root/heartbeat.log" "$root/fixture.sqlite" "$root/resources" > console.log 2>&1 &
daemon=$!
trap 'kill "$daemon" 2>/dev/null || true; wait "$daemon" 2>/dev/null || true' 0 1 2 15
attempt=0
until ./strappyd --ipc-probe "$root/service.sock" > readiness.log 2>&1; do
  attempt=$((attempt+1))
  if [ "$attempt" -ge 15 ]; then cat console.log; exit 1; fi
  sleep 1
done
cat readiness.log
./maintenance-probe "$root/service.sock" "$root/fixture.sqlite" "$root/scan"
kill "$daemon"
wait "$daemon"
trap - 0 1 2 15
