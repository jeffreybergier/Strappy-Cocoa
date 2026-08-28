import createStrappyModule from "./strappy.js";
import { makeOpenRouterTestRequest } from "./openrouter_transport.js";

const helloMessage = "Hello from Strappy WebAssembly.";

let apiKey = "";
let activeRequest = null;
let wasmReady = false;

function postKeyState() {
  self.postMessage({ type: "key-state", available: apiKey !== "" });
}

function cancelActiveRequest() {
  if (activeRequest) {
    activeRequest.abort();
    activeRequest = null;
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

  const controller = new AbortController();
  activeRequest = controller;
  self.postMessage({ type: "request-started" });

  try {
    const response = await makeOpenRouterTestRequest(
      apiKey,
      controller.signal,
    );
    if (activeRequest === controller) {
      self.postMessage({
        type: "response-received",
        httpStatus: response.httpStatus,
        model: response.model,
        status: response.status,
        outputText: response.outputText,
      });
    }
  } catch (error) {
    if (error instanceof DOMException && error.name === "AbortError") {
      self.postMessage({ type: "request-cancelled" });
    } else {
      self.postMessage({
        type: "request-error",
        message: error instanceof Error
          ? error.message
          : "The OpenRouter transport test failed.",
      });
    }
  } finally {
    if (activeRequest === controller) {
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
