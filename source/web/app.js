const statusElement = document.querySelector("#status");
const wasmDetailElement = document.querySelector("#wasm-detail");
const keyForm = document.querySelector("#key-form");
const apiKeyInput = document.querySelector("#api-key");
const setKeyButton = document.querySelector("#set-key");
const clearKeyButton = document.querySelector("#clear-key");
const promptForm = document.querySelector("#prompt-form");
const promptInput = document.querySelector("#prompt");
const sendButton = document.querySelector("#send-prompt");
const cancelButton = document.querySelector("#cancel-request");
const conversationStatus = document.querySelector("#conversation-status");
const timelineFrame = document.querySelector("#timeline");

let worker = null;
let workerReady = false;
let keyAvailable = false;
let requestActive = false;
let timelineReady = false;

function showWasmError(message) {
  statusElement.dataset.state = "error";
  statusElement.textContent = "Could not load Strappy WebAssembly.";
  wasmDetailElement.hidden = false;
  wasmDetailElement.textContent = message;
}

function setConversationStatus(state, message) {
  conversationStatus.dataset.state = state;
  conversationStatus.textContent = message;
}

function updateControls() {
  setKeyButton.disabled = !workerReady || requestActive;
  clearKeyButton.disabled = !workerReady || !keyAvailable;
  promptInput.disabled = !workerReady || requestActive;
  sendButton.disabled = !workerReady || !keyAvailable || requestActive;
  cancelButton.disabled = !workerReady || !requestActive;
}

function loadTimelinePage(html) {
  timelineReady = false;
  timelineFrame.addEventListener("load", () => {
    timelineReady = true;
  }, { once: true });
  timelineFrame.srcdoc = html;
}

function applyTimelineScript(script) {
  if (!timelineReady || !script) {
    worker?.postMessage({ type: "reload-timeline" });
    return;
  }
  try {
    timelineFrame.contentWindow.eval(script);
  } catch {
    timelineReady = false;
    worker?.postMessage({ type: "reload-timeline" });
  }
}

function handleConversationEvent(message) {
  switch (message.kind) {
    case "timeline-script":
      applyTimelineScript(message.payload);
      break;
    case "timeline-reload":
      worker?.postMessage({ type: "reload-timeline" });
      if (message.payload) {
        setConversationStatus("error", message.payload);
      }
      break;
    case "processing-status":
      if (message.payload) {
        setConversationStatus("running", `Processing: ${message.payload}`);
      }
      break;
    default:
      break;
  }
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
      statusElement.dataset.capabilityProfile = JSON.stringify(
        message.capabilityProfile ?? null,
      );
      setConversationStatus("idle", "Enter an API key, then send a prompt.");
      break;
    case "timeline-page":
      loadTimelinePage(message.html);
      break;
    case "conversation-event":
      handleConversationEvent(message);
      break;
    case "key-state":
      keyAvailable = message.available === true;
      setConversationStatus(
        keyAvailable ? "ready" : "idle",
        keyAvailable
          ? "The API key is held in volatile Worker memory."
          : "The API key has been cleared.",
      );
      break;
    case "request-started":
      requestActive = true;
      setConversationStatus("running", "Strappy is processing the prompt…");
      break;
    case "response-complete":
      requestActive = false;
      setConversationStatus("complete", "Response complete and saved.");
      promptInput.focus();
      break;
    case "request-cancelled":
      requestActive = false;
      setConversationStatus("idle", "The request was cancelled.");
      break;
    case "request-error":
      requestActive = false;
      setConversationStatus("error", message.message);
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
  setConversationStatus("error", "The Worker stopped and its key was forgotten.");
  updateControls();
}

function startWorker() {
  if (!("Worker" in window)) {
    showWasmError("This browser does not support Web Workers.");
    return;
  }
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

promptForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const prompt = promptInput.value.trim();
  if (!workerReady || !keyAvailable || requestActive || prompt === "") {
    return;
  }
  promptInput.value = "";
  worker.postMessage({ type: "submit-prompt", prompt });
});

cancelButton.addEventListener("click", () => {
  worker?.postMessage({ type: "cancel-request" });
});

clearKeyButton.addEventListener("click", () => {
  apiKeyInput.value = "";
  worker?.postMessage({ type: "clear-key" });
});

window.addEventListener("pagehide", () => {
  worker?.postMessage({ type: "shutdown" });
  worker?.terminate();
  worker = null;
});

startWorker();
