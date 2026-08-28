import createStrappyModule from "./strappy.js";

const helloMessage = "Hello from Strappy WebAssembly.";

let apiKey = "";
let activeRequest = null;
let wasmReady = false;
let strappyModule = null;

function postKeyState() {
  self.postMessage({ type: "key-state", available: apiKey !== "" });
}

function cancelActiveRequest() {
  if (activeRequest) {
    strappyModule?.ccall(
      "strappy_client_web_cancel_active_request",
      null,
      [],
      [],
    );
  }
}

function clearKey() {
  cancelActiveRequest();
  apiKey = "";
  postKeyState();
}

async function startTestRequest() {
  if (!wasmReady) {
    self.postMessage({ type: "request-error", message: "The Worker is not ready." });
    return;
  }
  if (apiKey === "") {
    self.postMessage({ type: "request-error", message: "Enter an OpenRouter API key first." });
    return;
  }
  if (activeRequest) {
    return;
  }

  const requestMarker = {};
  activeRequest = requestMarker;
  self.postMessage({ type: "request-started" });

  try {
    const succeeded = await strappyModule.ccall(
      "strappy_web_client_test_request",
      "number",
      ["string"],
      [apiKey],
      { async: true },
    );
    if (activeRequest === requestMarker && succeeded === 1) {
      self.postMessage({
        type: "response-received",
        httpStatus: strappyModule.ccall(
          "strappy_web_client_result_http_status", "number", [], []),
        model: strappyModule.ccall(
          "strappy_web_client_result_model", "string", [], []),
        status: strappyModule.ccall(
          "strappy_web_client_result_status", "string", [], []),
        outputText: strappyModule.ccall(
          "strappy_web_client_result_output_text", "string", [], []),
      });
    } else if (activeRequest === requestMarker) {
      const message = strappyModule.ccall(
        "strappy_web_client_result_error", "string", [], []);
      if (message.includes("cancelled")) {
        self.postMessage({ type: "request-cancelled" });
      } else {
        self.postMessage({ type: "request-error", message });
      }
    }
  } catch (error) {
    self.postMessage({
      type: "request-error",
      message: error instanceof Error
        ? error.message
        : "The OpenRouter transport test failed.",
    });
  } finally {
    if (activeRequest === requestMarker) {
      activeRequest = null;
    }
  }
}

self.addEventListener("message", (event) => {
  const message = event.data;
  switch (message?.type) {
    case "set-key":
      apiKey = typeof message.key === "string" ? message.key.trim() : "";
      postKeyState();
      break;
    case "start-test":
      void startTestRequest();
      break;
    case "cancel-request":
      cancelActiveRequest();
      break;
    case "clear-key":
      clearKey();
      break;
    case "shutdown":
      clearKey();
      self.close();
      break;
    default:
      break;
  }
});

try {
  const strappy = await createStrappyModule();
  strappyModule = strappy;
  const utf8Length = new TextEncoder().encode(helloMessage).byteLength;
  const isValidUtf8 = strappy.ccall(
    "strappy_utf8_validate",
    "number",
    ["string", "number"],
    [helloMessage, utf8Length],
  );

  if (isValidUtf8 !== 1) {
    throw new Error("Shared C code rejected the UTF-8 greeting.");
  }

  wasmReady = true;
  self.postMessage({
    type: "ready",
    message: helloMessage,
    detail: "strappy_utf8_validate() returned success from shared C code.",
  });
} catch (error) {
  self.postMessage({
    type: "error",
    message: error instanceof Error ? error.message : "Unknown Worker error.",
  });
}
