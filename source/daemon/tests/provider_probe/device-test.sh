#!/bin/sh
# Run explicitly as root on gomadango from the documented staging directory.
# Every disruptive action is separate; this script never resprings implicitly.
set -eu
stage=/var/mobile/tmp_altivec/strappy-provider-probe-v1
package=com.altivecintelligence.strappy.provider-probe
bundle=/System/Library/BulletinBoardPlugins/StrappyProviderProbe.bundle
sender=/usr/libexec/strappy-provider-probe
[ "$(id -u)" = 0 ] || { echo 'Run as root.' >&2; exit 1; }
[ "$(pwd -P)" = "$stage" ] || { echo 'Wrong staging directory.' >&2; exit 1; }

production_fingerprints() {
  for file in /Applications/Strappy.app/Strappy /usr/libexec/strappyd \
    /Library/MobileSubstrate/DynamicLibraries/StrappyBulletin.dylib \
    /Library/MobileSubstrate/DynamicLibraries/StrappyBulletin.plist; do
    [ -f "$file" ] || { echo "Missing production file: $file" >&2; return 1; }
    /usr/bin/sha256sum "$file"
  done
}

case "${1:-}" in
  baseline)
    [ ! -e before.sha256 ] || { echo 'Baseline already exists.' >&2; exit 1; }
    [ ! -e "$bundle" ] && [ ! -e "$sender" ] || {
      echo 'Test payload already exists; stop and inspect ownership.' >&2; exit 1;
    }
    if dpkg-query -W "$package" >/dev/null 2>&1; then
      echo 'Test package already registered; stop and inspect.' >&2; exit 1
    fi
    production_fingerprints > before.sha256
    dpkg-query -W com.altivecintelligence.strappy > before.package
    ps -ax > before.processes
    ls -lt /var/mobile/Library/Logs/CrashReporter > before.crashes
    echo 'Baseline saved. Installation is a separate action.'
    ;;
  install)
    [ -s before.sha256 ] && [ -f package.deb ]
    [ "$(dpkg-deb -f package.deb Package)" = "$package" ]
    [ "$(dpkg-deb -f package.deb Version)" = 0.1.0 ]
    dpkg -i package.deb > install.log 2>&1
    echo 'Installed. No respring performed.'
    ;;
  respring)
    echo 'Explicitly restarting SpringBoard.'
    /usr/bin/killall -TERM SpringBoard
    ;;
  send)
    [ ! -e attempted ] || { echo 'Already attempted; do not repeat.' >&2; exit 1; }
    [ -f receiver.log ]
    grep -q 'provider initialized; idle' receiver.log || {
      echo 'No initialization evidence; stop and collect logs.' >&2; exit 1;
    }
    su mobile -c "$sender send" > sender.log 2>&1
    ;;
  collect)
    production_fingerprints > after.sha256
    dpkg-query -W com.altivecintelligence.strappy > after.package
    ps -ax > after.processes
    ls -lt /var/mobile/Library/Logs/CrashReporter > after.crashes
    if [ -r /var/log/syslog ]; then
      grep 'StrappyProviderProbe' /var/log/syslog | tail -100 > provider-syslog.log || :
    fi
    diff before.sha256 after.sha256
    diff before.package after.package
    echo 'Production package identity and four file hashes unchanged.'
    ;;
  clear)
    su mobile -c "$sender clear" > clear.log 2>&1
    ;;
  remove)
    # Also the emergency action: works without a responsive provider/UI.
    # Do not make removal depend on sending a private API request.
    dpkg -r "$package" > remove.log 2>&1
    [ ! -e "$bundle/StrappyProviderProbe" ] && [ ! -e "$sender" ]
    echo 'Package removed. Explicitly respring next to unload the old image.'
    ;;
  *)
    echo 'usage: sh device-test.sh baseline|install|respring|send|collect|clear|remove' >&2
    exit 2
    ;;
esac
