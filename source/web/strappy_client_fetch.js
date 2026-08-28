mergeInto(LibraryManager.library, {
  $StrappyClientFetchState: {
    body: new Uint8Array(),
    headers: new Uint8Array(),
    effectiveUrl: new Uint8Array(),
    contentType: new Uint8Array(),
    error: new Uint8Array(),
    httpStatus: 0,
    transportCode: 0,
    cancelled: 0,
    totalSeconds: 0,
    controller: null,
  },

  strappy_client_fetch_execute__deps: ["$StrappyClientFetchState"],
  strappy_client_fetch_execute__async: "auto",
  strappy_client_fetch_execute: async function(
    urlPointer,
    methodPointer,
    headersPointer,
    bodyPointer,
    bodyLength,
    timeoutSeconds,
  ) {
      const state = StrappyClientFetchState;
      const encoder = new TextEncoder();
      state.body = new Uint8Array();
      state.headers = new Uint8Array();
      state.effectiveUrl = new Uint8Array();
      state.contentType = new Uint8Array();
      state.error = new Uint8Array();
      state.httpStatus = 0;
      state.transportCode = 0;
      state.cancelled = 0;
      state.totalSeconds = 0;
      const url = UTF8ToString(urlPointer);
      const method = UTF8ToString(methodPointer);
      const headerBlock = UTF8ToString(headersPointer);
      const headers = {};
      for (const line of headerBlock.split(/\r?\n/)) {
        const separator = line.indexOf(":");
        if (separator > 0) {
          headers[line.slice(0, separator).trim()] =
            line.slice(separator + 1).trim();
        }
      }
      const body = bodyLength > 0
        ? HEAPU8.slice(bodyPointer, bodyPointer + bodyLength)
        : undefined;
      const controller = new AbortController();
      state.controller = controller;
      const timeout = timeoutSeconds > 0
        ? setTimeout(() => controller.abort("timeout"), timeoutSeconds * 1000)
        : null;
      const started = performance.now();
      try {
        const response = await globalThis.fetch(url, {
          method,
          headers,
          body,
          cache: "no-store",
          credentials: "omit",
          redirect: "error",
          referrerPolicy: "no-referrer",
          signal: controller.signal,
        });
        state.httpStatus = response.status;
        state.effectiveUrl = encoder.encode(response.url || url);
        state.contentType = encoder.encode(
          response.headers.get("content-type") || "",
        );
        const responseHeaders = [];
        response.headers.forEach((value, name) => {
          responseHeaders.push(`${name}: ${value}\r\n`);
        });
        state.headers = encoder.encode(responseHeaders.join(""));
        state.body = new Uint8Array(await response.arrayBuffer());
      } catch (error) {
        state.transportCode = 1;
        if (controller.signal.aborted) {
          state.cancelled = 1;
          state.error = encoder.encode("Responses request was cancelled.");
        } else {
          state.error = encoder.encode(
            "The browser could not reach the provider. This may be a network or CORS failure.",
          );
        }
      } finally {
        state.totalSeconds = (performance.now() - started) / 1000;
        if (timeout !== null) {
          clearTimeout(timeout);
        }
        if (state.controller === controller) {
          state.controller = null;
        }
      }
    return 1;
  },

  strappy_client_fetch_http_status__deps: ["$StrappyClientFetchState"],
  strappy_client_fetch_http_status: function() {
    return StrappyClientFetchState.httpStatus;
  },
  strappy_client_fetch_transport_code__deps: ["$StrappyClientFetchState"],
  strappy_client_fetch_transport_code: function() {
    return StrappyClientFetchState.transportCode;
  },
  strappy_client_fetch_cancelled__deps: ["$StrappyClientFetchState"],
  strappy_client_fetch_cancelled: function() {
    return StrappyClientFetchState.cancelled;
  },
  strappy_client_fetch_total_seconds__deps: ["$StrappyClientFetchState"],
  strappy_client_fetch_total_seconds: function() {
    return StrappyClientFetchState.totalSeconds;
  },
  strappy_client_fetch_value_length__deps: ["$StrappyClientFetchState"],
  strappy_client_fetch_value_length: function(kind) {
    const state = StrappyClientFetchState;
    const values = [null, state.body, state.headers, state.effectiveUrl,
      state.contentType, state.error];
    return values[kind]?.length || 0;
  },
  strappy_client_fetch_copy_value__deps: ["$StrappyClientFetchState"],
  strappy_client_fetch_copy_value: function(kind, target, capacity) {
    const state = StrappyClientFetchState;
    const values = [null, state.body, state.headers, state.effectiveUrl,
      state.contentType, state.error];
    const value = values[kind];
    if (!value || capacity <= value.length) {
      return 0;
    }
    HEAPU8.set(value, target);
    HEAPU8[target + value.length] = 0;
    return 1;
  },
  strappy_client_fetch_cancel__deps: ["$StrappyClientFetchState"],
  strappy_client_fetch_cancel: function() {
    StrappyClientFetchState.controller?.abort("cancelled");
  },
});
