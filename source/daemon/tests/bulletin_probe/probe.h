#ifndef STRAPPY_BULLETIN_PROBE_H
#define STRAPPY_BULLETIN_PROBE_H

/* Fixed-message, diagnostic-only IPC. No credentials or user data are sent.
 * Run one sender at a time: these Darwin notification names are not a
 * request-correlated or authenticated production protocol. */
#define STRAPPY_BULLETIN_REQUEST \
  "com.altivecintelligence.strappy.bulletin-probe.request"
#define STRAPPY_BULLETIN_ACK \
  "com.altivecintelligence.strappy.bulletin-probe.dispatched"
#define STRAPPY_BULLETIN_LOG \
  "/var/mobile/tmp_altivec/strappy-bulletin-probe/receiver.log"

/* Foreground probe entry point. Waits five seconds, sends once, then waits
 * up to five seconds for the receiver's dispatch acknowledgement. Does not
 * initialize the store, credentials, providers, or production service. */
int strappy_bulletin_probe_run(void);

#endif
