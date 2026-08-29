const statusElement = document.querySelector("#status");
const wasmDetailElement = document.querySelector("#wasm-detail");
const keyForm = document.querySelector("#key-form");
const apiKeyInput = document.querySelector("#api-key");
const setKeyButton = document.querySelector("#set-key");
const testRequestButton = document.querySelector("#test-request");
const cancelRequestButton = document.querySelector("#cancel-request");
const clearKeyButton = document.querySelector("#clear-key");
const shutdownWorkerButton = document.querySelector("#shutdown-worker");
const restartWorkerButton = document.querySelector("#restart-worker");
const transportStatusElement = document.querySelector("#transport-status");
const responseElement = document.querySelector("#response");

let worker = null;
let workerReady = false;
let keyAvailable = false;
let requestActive = false;

function showWasmError(message) {
  statusElement.dataset.state = "error";
  statusElement.textContent = "Could not load Strappy WebAssembly.";
  wasmDetailElement.hidden = false;
  wasmDetailElement.textContent = message;
}

function setTransportStatus(state, message) {
  transportStatusElement.dataset.state = state;
  transportStatusElement.textContent = message;
}

function updateControls() {
  setKeyButton.disabled = !workerReady || requestActive;
  testRequestButton.disabled = !workerReady || !keyAvailable || requestActive;
  cancelRequestButton.disabled = !workerReady || !requestActive;
  clearKeyButton.disabled = !workerReady || !keyAvailable;
  shutdownWorkerButton.disabled = !workerReady;
  restartWorkerButton.hidden = worker !== null;
}

function clearResponse() {
  responseElement.hidden = true;
  responseElement.textContent = "";
}

function handleWorkerMessage(event) {
  const message = event.data;
  if (!message || typeof message.type !== "string") {
    return;
  }

  switch (message.type) {
    case "ready":
      workerReady = true;
      statusElement.dataset.state = "ready";
      statusElement.textContent = message.message;
      wasmDetailElement.hidden = false;
      wasmDetailElement.textContent = message.detail;
      statusElement.dataset.databasePersistent =
        message.databasePersistent === true ? "true" : "false";
      statusElement.dataset.sessionCount = String(message.databaseSessionCount ?? 0);
      statusElement.dataset.sessionId = String(message.databaseSessionId ?? 0);
      setTransportStatus("idle", "Enter an API key to enable the transport test.");
      break;
    case "key-state":
      keyAvailable = message.available === true;
      setTransportStatus(
        keyAvailable ? "ready" : "idle",
        keyAvailable
          ? "The API key is held in volatile Worker memory."
          : "The API key has been cleared.",
      );
      break;
    case "request-started":
      requestActive = true;
      clearResponse();
      setTransportStatus("running", "Connecting directly to OpenRouter…");
      break;
    case "response-received": {
      requestActive = false;
      const completed = message.status === "completed";
      setTransportStatus(
        completed ? "complete" : "idle",
        completed
          ? `OpenRouter returned a complete JSON response (HTTP ${message.httpStatus}).`
          : `OpenRouter returned a JSON response with status “${message.status}”.`,
      );
      const details = [
        `Model: ${message.model || "not reported"}`,
        `Status: ${message.status}`,
      ];
      if (message.outputText) {
        details.push("", message.outputText);
      }
      responseElement.textContent = details.join("\n");
      responseElement.hidden = false;
      break;
    }
    case "request-cancelled":
      requestActive = false;
      setTransportStatus("idle", "The request was cancelled.");
      break;
    case "request-error":
      requestActive = false;
      setTransportStatus("error", message.message);
      break;
    case "error":
      showWasmError(message.message);
      workerReady = false;
      break;
    default:
      break;
  }
  updateControls();
}

function handleWorkerFailure() {
  workerReady = false;
  keyAvailable = false;
  requestActive = false;
  worker = null;
  showWasmError("The WebAssembly Worker stopped unexpectedly.");
  setTransportStatus("error", "The Worker is not running and its key was forgotten.");
  updateControls();
}

function startWorker() {
  if (!("Worker" in window)) {
    showWasmError("This browser does not support Web Workers.");
    return;
  }

  workerReady = false;
  keyAvailable = false;
  requestActive = false;
  statusElement.dataset.state = "loading";
  statusElement.textContent = "Loading shared C code…";
  setTransportStatus("idle", "Waiting for the Worker.");
  worker = new Worker(new URL("./worker.js", import.meta.url), { type: "module" });
  worker.addEventListener("message", handleWorkerMessage);
  worker.addEventListener("error", handleWorkerFailure, { once: true });
  updateControls();
}

keyForm.addEventListener("submit", (event) => {
  event.preventDefault();
  if (!workerReady || requestActive || apiKeyInput.value.trim() === "") {
    return;
  }

  worker.postMessage({ type: "set-key", key: apiKeyInput.value });
  apiKeyInput.value = "";
});

testRequestButton.addEventListener("click", () => {
  worker?.postMessage({ type: "start-test" });
});

cancelRequestButton.addEventListener("click", () => {
  worker?.postMessage({ type: "cancel-request" });
});

clearKeyButton.addEventListener("click", () => {
  apiKeyInput.value = "";
  worker?.postMessage({ type: "clear-key" });
});

shutdownWorkerButton.addEventListener("click", () => {
  apiKeyInput.value = "";
  worker?.postMessage({ type: "shutdown" });
  worker?.terminate();
  worker = null;
  workerReady = false;
  keyAvailable = false;
  requestActive = false;
  setTransportStatus("idle", "The Worker was shut down and its key was forgotten.");
  updateControls();
});

restartWorkerButton.addEventListener("click", () => {
  clearResponse();
  startWorker();
});

window.addEventListener("pagehide", () => {
  worker?.postMessage({ type: "shutdown" });
  worker?.terminate();
  worker = null;
});

startWorker();
