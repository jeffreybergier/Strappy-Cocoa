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
const sessionList = document.querySelector("#session-list");
const newSessionButton = document.querySelector("#new-session");
const chatTitle = document.querySelector("#chat-title");
const chatSubtitle = document.querySelector("#chat-subtitle");
const sessionTab = document.querySelector("#session-tab");
const defaultsTab = document.querySelector("#defaults-tab");
const preferencesForm = document.querySelector("#preferences-form");
const sessionNameField = document.querySelector("#session-name-field");
const sessionNameInput = document.querySelector("#session-name");
const sessionModelInput = document.querySelector("#session-model");
const webSearchInput = document.querySelector("#web-search-enabled");
const webProviderSelect = document.querySelector("#web-provider");
const limitToOneToolInput = document.querySelector("#limit-to-one-tool");
const answerQualityInput = document.querySelector("#answer-quality-enabled");
const roundLimitInput = document.querySelector("#round-limit");
const savePreferencesButton = document.querySelector("#save-preferences");
const deleteSessionButton = document.querySelector("#delete-session");
const preferencesStatus = document.querySelector("#preferences-status");

let worker = null;
let workerReady = false;
let keyAvailable = false;
let requestActive = false;
let timelineReady = false;
let workspaceState = null;
let preferenceScope = "session";

function activeSession() {
  return workspaceState?.sessions.find(
    (session) => session.id === workspaceState.activeSessionId,
  ) ?? null;
}

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

function installSharedAppearance(css) {
  if (typeof css !== "string" || !css.startsWith(":root{")) {
    throw new Error("The shared Strappy appearance is invalid.");
  }
  let style = document.querySelector("#strappy-shared-appearance");
  if (!style) {
    style = document.createElement("style");
    style.id = "strappy-shared-appearance";
    document.head.append(style);
  }
  style.textContent = css;
  statusElement.dataset.appearanceSource = "shared-c";
}

function updateControls() {
  const workspaceAvailable = workerReady && workspaceState !== null;
  setKeyButton.disabled = !workerReady || requestActive;
  clearKeyButton.disabled = !workerReady || !keyAvailable;
  promptInput.disabled = !workspaceAvailable || requestActive;
  sendButton.disabled = !workspaceAvailable || !keyAvailable || requestActive;
  cancelButton.disabled = !workerReady || !requestActive;
  newSessionButton.disabled = !workspaceAvailable || requestActive;
  savePreferencesButton.disabled = !workspaceAvailable || requestActive;
  deleteSessionButton.disabled = !workspaceAvailable || requestActive ||
    preferenceScope !== "session";
  for (const button of sessionList.querySelectorAll("button")) {
    button.disabled = requestActive;
  }
  for (const control of preferencesForm.elements) {
    if (control.id !== "delete-session" && control.id !== "save-preferences") {
      control.disabled = !workspaceAvailable || requestActive ||
        control.id === "assistant-set" || control.id === "session-model";
    }
  }
}

function formatSessionDate(value) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return "No messages yet";
  }
  return new Intl.DateTimeFormat(undefined, {
    dateStyle: "medium",
    timeStyle: "short",
  }).format(date);
}

function renderSessionList() {
  sessionList.replaceChildren();
  for (const session of workspaceState?.sessions ?? []) {
    const item = document.createElement("li");
    const button = document.createElement("button");
    const name = document.createElement("span");
    const date = document.createElement("span");
    button.type = "button";
    button.className = "session-button";
    button.dataset.sessionId = String(session.id);
    button.setAttribute(
      "aria-current",
      session.id === workspaceState.activeSessionId ? "true" : "false",
    );
    name.className = "session-name";
    name.textContent = session.name || "New chat";
    date.className = "session-date";
    date.textContent = formatSessionDate(session.last_activity_at);
    button.append(name, date);
    button.addEventListener("click", () => {
      if (!requestActive && session.id !== workspaceState.activeSessionId) {
        preferencesStatus.textContent = "Switching chats…";
        worker?.postMessage({ type: "select-session", sessionId: session.id });
      }
    });
    item.append(button);
    sessionList.append(item);
  }
}

function renderInspector() {
  const session = activeSession();
  const options = preferenceScope === "defaults"
    ? workspaceState?.defaultOptions
    : workspaceState?.activeOptions;
  const editsDefaults = preferenceScope === "defaults";
  sessionTab.setAttribute("aria-selected", editsDefaults ? "false" : "true");
  defaultsTab.setAttribute("aria-selected", editsDefaults ? "true" : "false");
  sessionNameField.hidden = editsDefaults;
  deleteSessionButton.hidden = editsDefaults;
  sessionNameInput.value = editsDefaults ? "" : (session?.name || "New chat");
  sessionModelInput.value = editsDefaults
    ? (options?.model_id || "Bundled default")
    : (session?.model_name || options?.model_id || "Bundled default");
  webSearchInput.checked = options?.web_search_enabled === true;
  webProviderSelect.value = options?.web_provider || "auto";
  limitToOneToolInput.checked = options?.limit_to_one_tool === true;
  answerQualityInput.checked = options?.answer_quality_enabled === true;
  roundLimitInput.value = String(options?.round_limit ?? 50);
  preferencesStatus.textContent = editsDefaults
    ? "These settings are copied into each new chat."
    : "Changes apply to the selected chat.";
}

function renderWorkspace() {
  const session = activeSession();
  renderSessionList();
  chatTitle.textContent = session?.name || "New chat";
  chatSubtitle.textContent = session?.model_name
    ? `World Knowledge · ${session.model_name}`
    : "World Knowledge";
  renderInspector();
  updateControls();
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
    case "session-updated":
      worker?.postMessage({ type: "reload-workspace" });
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
      try {
        installSharedAppearance(message.appearanceCss);
      } catch (error) {
        showWasmError(error instanceof Error ? error.message : "Could not apply appearance.");
        workerReady = false;
        break;
      }
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
    case "workspace-state":
      workspaceState = {
        sessions: Array.isArray(message.sessions) ? message.sessions : [],
        activeSessionId: Number(message.activeSessionId),
        activeOptions: message.activeOptions,
        defaultOptions: message.defaultOptions,
      };
      statusElement.dataset.sessionCount = String(workspaceState.sessions.length);
      statusElement.dataset.sessionId = String(workspaceState.activeSessionId);
      renderWorkspace();
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
    case "preferences-saved":
      preferencesStatus.dataset.state = "complete";
      preferencesStatus.textContent = message.scope === "defaults"
        ? "New-chat defaults saved."
        : "Chat configuration saved.";
      break;
    case "workspace-error":
      preferencesStatus.dataset.state = "error";
      preferencesStatus.textContent = message.message;
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
  workspaceState = null;
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

preferencesForm.addEventListener("submit", (event) => {
  event.preventDefault();
  if (!workerReady || requestActive || workspaceState === null) {
    return;
  }
  preferencesStatus.dataset.state = "running";
  preferencesStatus.textContent = "Saving…";
  worker.postMessage({
    type: "save-options",
    scope: preferenceScope,
    name: preferenceScope === "session" ? sessionNameInput.value : undefined,
    options: {
      webProvider: webProviderSelect.value,
      webSearchEnabled: webSearchInput.checked,
      limitToOneTool: limitToOneToolInput.checked,
      answerQualityEnabled: answerQualityInput.checked,
      roundLimit: Number(roundLimitInput.value),
    },
  });
});

sessionTab.addEventListener("click", () => {
  preferenceScope = "session";
  renderInspector();
  updateControls();
});

defaultsTab.addEventListener("click", () => {
  preferenceScope = "defaults";
  renderInspector();
  updateControls();
});

newSessionButton.addEventListener("click", () => {
  preferencesStatus.textContent = "Creating a new chat…";
  worker?.postMessage({ type: "create-session" });
});

deleteSessionButton.addEventListener("click", () => {
  const session = activeSession();
  if (!session || requestActive || !window.confirm(
    `Delete “${session.name || "New chat"}” and its conversation history?`,
  )) {
    return;
  }
  preferencesStatus.textContent = "Deleting chat…";
  worker?.postMessage({ type: "delete-session", sessionId: session.id });
});

cancelButton.addEventListener("click", () => worker?.postMessage({ type: "cancel-request" }));
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
