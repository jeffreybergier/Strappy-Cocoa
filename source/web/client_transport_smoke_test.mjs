import createStrappyModule from "./build-release/strappy.js";

const canaryKey = "not-a-real-phase-three-key";
let capturedUrl = "";
let capturedOptions = null;

globalThis.fetch = async (url, options) => {
  capturedUrl = url;
  capturedOptions = options;
  return new Response(JSON.stringify({
    model: "example/test-model",
    status: "completed",
    output: [{
      type: "message",
      content: [{ type: "output_text", text: "Strappy transport works." }],
    }],
  }), {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "X-Request-Id": "request-phase-three",
    },
  });
};

const strappy = await createStrappyModule();
const succeeded = await strappy.ccall(
  "strappy_web_client_test_request",
  "number",
  ["string"],
  [canaryKey],
  { async: true },
);
if (succeeded !== 1) {
  const message = strappy.ccall(
    "strappy_web_client_result_error", "string", [], []);
  throw new Error(
    `Shared C client failed (result ${String(succeeded)}, URL ${capturedUrl}): ${message}`,
  );
}
if (capturedUrl !== "https://openrouter.ai/api/v1/responses") {
  throw new Error("Shared C client used an unexpected Responses URL.");
}
const headers = new Headers(capturedOptions.headers);
if (headers.get("Authorization") !== `Bearer ${canaryKey}`) {
  throw new Error("Shared C did not construct the Authorization header.");
}
if (headers.get("X-OpenRouter-Title") !== "Strappy") {
  throw new Error("Shared C omitted the browser-safe OpenRouter title.");
}
if (headers.has("X-OpenRouter-Metadata")) {
  throw new Error("Shared C did not omit the browser-restricted metadata header.");
}
const requestBody = JSON.parse(new TextDecoder().decode(capturedOptions.body));
if (requestBody.model !== "openrouter/free" || requestBody.stream !== false) {
  throw new Error("The C-backed transport request is not bounded as expected.");
}
if (
  strappy.ccall("strappy_web_client_result_http_status", "number", [], []) !== 200 ||
  strappy.ccall("strappy_web_client_result_model", "string", [], []) !==
    "example/test-model" ||
  strappy.ccall("strappy_web_client_result_status", "string", [], []) !==
    "completed" ||
  strappy.ccall("strappy_web_client_result_output_text", "string", [], []) !==
    "Strappy transport works."
) {
  throw new Error("Shared C did not interpret the Fetch response.");
}

globalThis.fetch = async () => new Response(
  JSON.stringify({ error: { message: canaryKey } }),
  { status: 401, headers: { "Content-Type": "application/json" } },
);
const invalidKeyResult = await strappy.ccall(
  "strappy_web_client_test_request",
  "number",
  ["string"],
  [canaryKey],
  { async: true },
);
const invalidKeyError = strappy.ccall(
  "strappy_web_client_result_error", "string", [], []);
if (
  invalidKeyResult !== 0 ||
  !invalidKeyError.includes("HTTP 401") ||
  invalidKeyError.includes(canaryKey)
) {
  throw new Error("Shared C did not safely report an invalid API key.");
}

globalThis.fetch = async (_url, options) => new Promise((_resolve, reject) => {
  options.signal.addEventListener("abort", () => {
    reject(new DOMException("Cancelled", "AbortError"));
  }, { once: true });
});
const cancellationPromise = strappy.ccall(
  "strappy_web_client_test_request",
  "number",
  ["string"],
  [canaryKey],
  { async: true },
);
setTimeout(() => {
  strappy.ccall(
    "strappy_client_web_cancel_active_request", null, [], []);
}, 0);
const cancellationResult = await cancellationPromise;
const cancellationError = strappy.ccall(
  "strappy_web_client_result_error", "string", [], []);
if (
  cancellationResult !== 0 ||
  !cancellationError.includes("cancelled") ||
  cancellationError.includes(canaryKey)
) {
  throw new Error("Shared C did not safely report Fetch cancellation.");
}

const workerListeners = new Map();
const workerMessages = [];
let workerMessageWaiter = null;
globalThis.self = {
  addEventListener(type, listener) {
    workerListeners.set(type, listener);
  },
  postMessage(message) {
    workerMessages.push(message);
    workerMessageWaiter?.();
  },
  close() {},
};
globalThis.fetch = async (_url, options) => new Promise((_resolve, reject) => {
  options.signal.addEventListener("abort", () => {
    reject(new DOMException("Cancelled", "AbortError"));
  }, { once: true });
});
await import("./build-release/worker.js?transport-cancellation-test");

async function waitForWorkerMessage(type) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    const message = workerMessages.find((item) => item.type === type);
    if (message) {
      return message;
    }
    await new Promise((resolve) => {
      workerMessageWaiter = resolve;
      setTimeout(resolve, 25);
    });
    workerMessageWaiter = null;
  }
  throw new Error(`Timed out waiting for Worker message: ${type}`);
}

await waitForWorkerMessage("ready");
const workerMessageListener = workerListeners.get("message");
workerMessageListener({ data: { type: "set-key", key: canaryKey } });
workerMessageListener({ data: { type: "start-test" } });
await waitForWorkerMessage("request-started");
workerMessageListener({ data: { type: "cancel-request" } });
await waitForWorkerMessage("request-cancelled");

console.log("Shared C Fetch transport smoke test passed.");
