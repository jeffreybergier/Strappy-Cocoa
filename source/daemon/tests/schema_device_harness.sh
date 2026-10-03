#!/bin/sh
# Run as mobile in a new private test directory. Never touches installed services.
set -eu
root="$1"
cd "$root"
export STRAPPY_INFO_PLIST_PATH="$root/resources/Info.plist"
if [ -e fresh.sqlite ] || [ -e legacy.sqlite ]; then
    echo 'Refusing to reuse schema fixture stores.' >&2
    exit 1
fi
ulimit -c 0
./strappyd "$root/heartbeat.log" "$root/fresh.sqlite" "$root/resources" > fresh-console.log 2>&1 &
daemon=$!
trap 'kill "$daemon" 2>/dev/null || true; wait "$daemon" 2>/dev/null || true' 0 1 2 15
attempt=0
until ./strappyd --ipc-probe "$root/service.sock" > readiness.log 2>&1; do
    attempt=$((attempt+1))
    if [ "$attempt" -ge 15 ]; then cat fresh-console.log; exit 1; fi
    sleep 1
done
cat readiness.log
kill "$daemon"
wait "$daemon"
trap - 0 1 2 15
./schema-probe "$root/legacy.sqlite" --legacy
./schema-reader-probe "$root/legacy.sqlite" --incompatible
cp legacy.sqlite legacy-before.sqlite
if ./strappyd "$root/legacy.log" "$root/legacy.sqlite" "$root/resources" > legacy-console.log 2>&1; then
    echo 'Incompatible daemon unexpectedly started.' >&2
    exit 1
fi
case "$(cat legacy-console.log)" in
    *'incompatible UNIQUE'*) cat legacy-console.log ;;
    *) cat legacy-console.log; exit 1 ;;
esac
test ! -S "$root/service.sock"
cmp legacy.sqlite legacy-before.sqlite
./schema-reader-probe "$root/legacy.sqlite" --incompatible
echo 'PASS: fresh readiness, incompatible-store refusal, no published socket, preserved store bytes'
