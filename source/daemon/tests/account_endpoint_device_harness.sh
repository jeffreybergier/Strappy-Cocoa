#!/bin/sh
# Run as the normal app user in a fresh private staging directory.
# Uses the production daemon with an isolated database and synthetic credentials.
set -eu
root="$1"
cd "$root"
export STRAPPY_INFO_PLIST_PATH="$root/resources/Info.plist"
if [ -e fixture.sqlite ]; then
  echo 'Refusing to reuse an account endpoint fixture store.' >&2
  exit 1
fi
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
./account-endpoint-probe "$root/service.sock" "$root/fixture.sqlite"
kill "$daemon"
wait "$daemon"
trap - 0 1 2 15
