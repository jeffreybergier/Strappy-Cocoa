#!/bin/sh
set -eu
stage=/var/mobile/tmp_altivec/strappy-provider-candidate-v1
package=com.altivecintelligence.strappy.notification-provider-test
bundle=/System/Library/BulletinBoardPlugins/StrappyNotificationProvider.bundle
[ "$(id -u)" = 0 ]
[ "$(pwd -P)" = "$(cd "$stage" && pwd -P)" ]
fingerprints() {
  sha256sum /Applications/Strappy.app/Strappy /usr/libexec/strappyd \
    /Library/MobileSubstrate/DynamicLibraries/StrappyBulletin.dylib \
    /Library/MobileSubstrate/DynamicLibraries/StrappyBulletin.plist
}
case "${1:-}" in
 baseline)
  [ ! -e before.sha256 ] && [ ! -e "$bundle" ]
  [ ! -e /usr/libexec/strappy-provider-test ]
  fingerprints > before.sha256
  dpkg-query -W com.altivecintelligence.strappy > before.package
  launchctl list > before.services
  ls -lt /var/mobile/Library/Logs/CrashReporter > before.crashes
  ;;
 install)
  [ -s before.sha256 ]
  [ "$(dpkg-deb -f package.deb Package)" = "$package" ]
  dpkg -i package.deb > install.log 2>&1
  ;;
 respring) /usr/bin/killall -TERM SpringBoard ;;
 ready)
  # Socket connection is authoritative; syslog can be delayed. Bound the wait.
  attempt=0
  while [ "$attempt" -lt 30 ]; do
    if su mobile -c "/usr/libexec/strappy-provider-test ready" > ready.log 2>&1; then
      cat ready.log
      exit 0
    fi
    attempt=$((attempt + 1))
    sleep 1
  done
  echo 'No live candidate receiver within 30 seconds.' >&2
  exit 1
  ;;
 send)
  su mobile -c "/usr/libexec/strappy-provider-test ready" > ready.log 2>&1
  case "${2:-}" in 1|2|3) ;; *) exit 2 ;; esac
  [ ! -e "sent-$2" ]
  touch "sent-$2"
  su mobile -c "/usr/libexec/strappy-provider-test $2" > "sender-$2.log" 2>&1
  ;;
 collect)
  fingerprints > after.sha256
  dpkg-query -W com.altivecintelligence.strappy > after.package
  launchctl list > after.services
  ls -lt /var/mobile/Library/Logs/CrashReporter > after.crashes
  grep 'StrappyProvider:' /var/log/syslog | tail -80 > provider-syslog.log || :
  diff before.sha256 after.sha256
  diff before.package after.package
  ;;
 remove)
  dpkg -r "$package" > remove.log 2>&1
  [ ! -e "$bundle/StrappyNotificationProvider" ]
  [ ! -e /usr/libexec/strappy-provider-test ]
  ;;
 *) exit 2 ;;
esac
