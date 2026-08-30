const $ = (selector) => document.querySelector(selector);
const statusElement = $("#status");
const wasmDetailElement = $("#wasm-detail");
const keyForm = $("#key-form");
const apiKeyInput = $("#api-key");
const setKeyButton = $("#set-key");
const clearKeyButton = $("#clear-key");
const promptForm = $("#prompt-form");
const promptInput = $("#prompt");
const sendButton = $("#send-prompt");
const timelineFrame = $("#timeline");
const sessionList = $("#session-list");
const newSessionButton = $("#new-session");
const deleteSessionButton = $("#delete-session");
const preferencesButton = $("#preferences-button");
const preferencesWindow = $("#preferences-window");
const closePreferencesButton = $("#close-preferences");
const chatTitle = $("#chat-title");
const sidebarToggleButton = $("#sidebar-toggle");
const closeChatButton = $("#close-chat");
const optionsToggleButton = $("#options-toggle");
const authenticationTab = $("#authentication-tab");
const modelsTab = $("#models-tab");
const defaultsTab = $("#defaults-tab");
const promptsTab = $("#prompts-tab");
const authenticationPane = $("#authentication-pane");
const modelsPane = $("#models-pane");
const defaultsPane = $("#defaults-pane");
const promptsPane = $("#prompts-pane");
const preferencesForm = $("#preferences-form");
const sessionModelInput = $("#session-model");
const webSearchInput = $("#web-search-enabled");
const webProviderSelect = $("#web-provider");
const limitToOneToolInput = $("#limit-to-one-tool");
const answerQualityInput = $("#answer-quality-enabled");
const roundLimitInput = $("#round-limit");
const roundLimitValue = $("#round-limit-value");
const preferencesStatus = $("#preferences-status");
const defaultsForm = $("#defaults-form");
const defaultModelInput = $("#default-model");
const defaultWebSearchInput = $("#default-web-search-enabled");
const defaultWebProviderSelect = $("#default-web-provider");
const defaultLimitToOneToolInput = $("#default-limit-to-one-tool");
const defaultAnswerQualityInput = $("#default-answer-quality-enabled");
const defaultRoundLimitInput = $("#default-round-limit");
const defaultRoundLimitValue = $("#default-round-limit-value");
const defaultsStatus = $("#defaults-status");
const modelSearchInput = $("#model-search");
const fetchModelsButton = $("#fetch-models");
const modelCatalogStatus = $("#model-catalog-status");
const modelTable = $("#model-table");
const modelList = $("#model-list");
const systemPrompt = $("#system-prompt");
const workspace = $(".workspace");
const sessionContextMenu = $("#session-context-menu");
const copySessionTitleButton = $("#copy-session-title");
const copyLastMessageButton = $("#copy-last-message");
const modelProviderDialog = $("#model-provider-dialog");
const fetchOpenRouterModelsButton = $("#fetch-openrouter-models");
const closeModelProviderDialogButton = $("#close-model-provider-dialog");
const providerDialogStatus = $("#provider-dialog-status");

const locale = navigator.language?.toLowerCase().startsWith("ja") ? "ja" : "en";
async function loadLocalization() {
  try {
    const response = await fetch(`./Resources/${locale}.lproj/Localizable.strings`);
    if (!response.ok) return {};
    const source = await response.text();
    const strings = {};
    const pattern = /"((?:\\.|[^"\\])*)"\s*=\s*"((?:\\.|[^"\\])*)"\s*;/g;
    for (const match of source.matchAll(pattern)) {
      strings[JSON.parse(`"${match[1]}"`)] = JSON.parse(`"${match[2]}"`);
    }
    return strings;
  } catch {
    return {};
  }
}
const localizedStrings = await loadLocalization();
const t = (key) => localizedStrings[key] || key;
const formatLocalized = (key, value) => t(key)
  .replace("%@", String(value))
  .replace("%lu", String(value));
for (const element of document.querySelectorAll("[data-l10n]")) {
  element.textContent = t(element.dataset.l10n);
}
document.documentElement.lang = locale;
modelSearchInput.placeholder = t("Search");
for (const button of [preferencesButton, deleteSessionButton, newSessionButton,
  sidebarToggleButton, closeChatButton, optionsToggleButton]) {
  const key = button.getAttribute("title") || button.getAttribute("aria-label");
  if (key) {
    button.title = t(key);
    button.setAttribute("aria-label", t(key));
  }
}
copySessionTitleButton.textContent = t("Copy Title");
copyLastMessageButton.textContent = t("Copy Last Message");
modelProviderDialog.querySelector("h2").textContent = t("Edit Model Providers");
fetchOpenRouterModelsButton.textContent = t("Fetch Models");
closeModelProviderDialogButton.textContent = t("Done");
for (const option of document.querySelectorAll("option")) {
  option.textContent = t(option.textContent);
}

let worker = null;
let workerReady = false;
let keyAvailable = false;
let requestActive = false;
let preferenceWorkActive = false;
let timelineReady = false;
let workspaceState = null;
let preferencesPane = "authentication";
let openedPreferencesOnLoad = false;
let chatClosed = false;
let systemPromptText = "";
let selectedModelIds = new Set();
let modelSort = { requiredAscending: true, primary: "model_allowed", ascending: false };
let contextMenuSession = null;
let lastSelectedModelId = null;

function activeSession() {
  return workspaceState?.sessions.find(
    (session) => session.id === workspaceState.activeSessionId,
  ) ?? null;
}
function modelDisplayName(model) {
  return model?.name || model?.wire_model_id || model?.id || t("Model");
}
function modelProviderName(model) {
  return model?.provider_id === "openrouter" ? "OpenRouter" :
    (model?.provider_id || "OpenRouter");
}
function selectedModelFor(select) {
  return workspaceState?.models.find((model) => model.id === select.value) ?? null;
}
function formatModelPrice(value) {
  const number = Number(value);
  if (value === null || value === undefined || value === "" ||
      !Number.isFinite(number) || number < 0) return "—";
  return `$${(number * 1_000_000).toLocaleString(undefined, { maximumFractionDigits: 4 })}`;
}
function snapRoundLimit(value) {
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 20) return 20;
  if (number >= 200) return 200;
  return Math.round(number / 10) * 10;
}
function showWasmError(message) {
  statusElement.dataset.state = "error";
  statusElement.textContent = "Could not load Strappy WebAssembly.";
  wasmDetailElement.hidden = false;
  wasmDetailElement.textContent = message;
}
function installSharedAppearance(css) {
  if (typeof css !== "string" || !css.startsWith(":root{")) {
    throw new Error("The shared Strappy appearance is invalid.");
  }
  let style = $("#strappy-shared-appearance");
  if (!style) {
    style = document.createElement("style");
    style.id = "strappy-shared-appearance";
    document.head.append(style);
  }
  style.textContent = css;
  statusElement.dataset.appearanceSource = "shared-c";
}
function updateRoundLabels() {
  roundLimitValue.value = roundLimitInput.value;
  defaultRoundLimitValue.value = defaultRoundLimitInput.value;
}
function updateControls() {
  const available = workerReady && workspaceState !== null && !chatClosed;
  const busy = requestActive || preferenceWorkActive;
  const hosted = selectedModelFor(sessionModelInput)?.hosted_tools_enabled !== false;
  const defaultHosted = selectedModelFor(defaultModelInput)?.hosted_tools_enabled !== false;
  setKeyButton.disabled = !workerReady || busy;
  clearKeyButton.disabled = !workerReady || !keyAvailable || busy;
  promptInput.disabled = !available || busy;
  const promptIsEmpty = promptInput.value.trim() === "";
  sendButton.disabled = requestActive ? false :
    (!available || !keyAvailable || promptIsEmpty || busy);
  sendButton.textContent = t(requestActive ? "Cancel" : "Send");
  if (!keyAvailable) sendButton.title = "Enter OpenRouter API Key to Send";
  else sendButton.removeAttribute("title");
  newSessionButton.disabled = !workerReady || workspaceState === null || busy;
  deleteSessionButton.disabled = !available || busy;
  preferencesButton.disabled = !workerReady || workspaceState === null || busy;
  closeChatButton.disabled = !available;
  fetchModelsButton.disabled = !workerReady || !keyAvailable || busy;
  fetchOpenRouterModelsButton.disabled = !workerReady || !keyAvailable || busy;
  modelSearchInput.disabled = !workerReady || workspaceState === null || busy;
  for (const button of sessionList.querySelectorAll("button")) button.disabled = busy;
  for (const control of preferencesForm.elements) {
    control.disabled = !available || busy ||
      (control.id === "web-search-enabled" && !hosted) ||
      (control.id === "web-provider" && (!hosted || !webSearchInput.checked));
  }
  for (const control of defaultsForm.elements) {
    control.disabled = !workerReady || workspaceState === null || busy ||
      (control.id === "default-web-search-enabled" && !defaultHosted) ||
      (control.id === "default-web-provider" &&
        (!defaultHosted || !defaultWebSearchInput.checked));
  }
  for (const checkbox of modelList.querySelectorAll("input[type=checkbox]")) {
    checkbox.disabled = busy || checkbox.dataset.default === "true";
  }
}
function formatSessionDate(value) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "";
  return new Intl.DateTimeFormat(undefined, { dateStyle: "short", timeStyle: "short" }).format(date);
}
function renderSessionList() {
  sessionList.replaceChildren();
  const sessions = workspaceState?.sessions ?? [];
  if (sessions.length === 0) {
    const item = document.createElement("li");
    item.className = "session-empty";
    item.innerHTML = `<strong>${t("No conversations yet")}</strong>` +
      `<span>${t("Create a conversation to begin.")}</span>`;
    sessionList.append(item);
    return;
  }
  for (const session of sessions) {
    const item = document.createElement("li");
    const button = document.createElement("button");
    const name = document.createElement("span");
    const meta = document.createElement("span");
    button.type = "button";
    button.className = "session-button";
    if (requestActive && session.id === workspaceState.activeSessionId) button.classList.add("session-running");
    button.dataset.sessionId = String(session.id);
    button.setAttribute("aria-current",
      !chatClosed && session.id === workspaceState.activeSessionId ? "true" : "false");
    name.className = "session-name";
    name.textContent = session.name || t("Untitled Session");
    meta.className = "session-date";
    meta.textContent = [formatSessionDate(session.last_activity_at), session.model_name]
      .filter(Boolean).join(", ");
    button.title = session.last_message_text || name.textContent;
    button.append(name, meta);
    button.addEventListener("click", () => {
      chatClosed = false;
      timelineFrame.hidden = false;
      if (session.id !== workspaceState.activeSessionId) {
        worker?.postMessage({ type: "select-session", sessionId: session.id });
      } else renderWorkspace();
    });
    button.addEventListener("contextmenu", (event) => {
      event.preventDefault();
      contextMenuSession = session;
      sessionContextMenu.style.left = `${event.clientX}px`;
      sessionContextMenu.style.top = `${event.clientY}px`;
      sessionContextMenu.hidden = false;
    });
    item.append(button);
    sessionList.append(item);
  }
}
function populateModelSelect(select, selectedModelId) {
  select.replaceChildren();
  for (const model of workspaceState?.models ?? []) {
    if (!model.allowed && model.id !== selectedModelId) continue;
    const option = document.createElement("option");
    option.value = model.id;
    option.textContent = modelDisplayName(model);
    select.append(option);
  }
  if (![...select.options].some((option) => option.value === selectedModelId)) {
    const option = document.createElement("option");
    option.value = selectedModelId;
    option.textContent = selectedModelId || t("No Models Available");
    select.append(option);
  }
  select.value = selectedModelId;
}
function renderInspector() {
  const options = workspaceState?.activeOptions;
  populateModelSelect(sessionModelInput, options?.model_id || "");
  webSearchInput.checked = options?.web_search_enabled === true &&
    selectedModelFor(sessionModelInput)?.hosted_tools_enabled !== false;
  webProviderSelect.value = options?.web_provider || "auto";
  limitToOneToolInput.checked = options?.limit_to_one_tool === true;
  answerQualityInput.checked = options?.answer_quality_enabled === true;
  roundLimitInput.value = String(snapRoundLimit(options?.round_limit ?? 50));
  preferencesStatus.textContent = t("Changes apply to the selected chat.");
  updateRoundLabels();
}
function renderDefaults() {
  const options = workspaceState?.defaultOptions;
  populateModelSelect(defaultModelInput, options?.model_id || "");
  defaultWebSearchInput.checked = options?.web_search_enabled === true &&
    selectedModelFor(defaultModelInput)?.hosted_tools_enabled !== false;
  defaultWebProviderSelect.value = options?.web_provider || "auto";
  defaultLimitToOneToolInput.checked = options?.limit_to_one_tool === true;
  defaultAnswerQualityInput.checked = options?.answer_quality_enabled === true;
  defaultRoundLimitInput.value = String(snapRoundLimit(options?.round_limit ?? 50));
  updateRoundLabels();
}
function showPreferencesPane(pane) {
  preferencesPane = pane;
  const panes = { authentication: authenticationPane, models: modelsPane,
    defaults: defaultsPane, prompts: promptsPane };
  const tabs = { authentication: authenticationTab, models: modelsTab,
    defaults: defaultsTab, prompts: promptsTab };
  for (const [name, element] of Object.entries(panes)) element.hidden = name !== pane;
  for (const [name, element] of Object.entries(tabs)) {
    element.setAttribute("aria-selected", name === pane ? "true" : "false");
  }
  if (pane === "models") renderModelCatalog();
  if (pane === "defaults") renderDefaults();
  if (pane === "prompts") systemPrompt.value = systemPromptText;
  updateControls();
}

const modelSearchKeys = ["id", "provider_id", "wire_model_id", "canonical_slug",
  "hugging_face_id", "name", "description", "context_length", "created",
  "architecture_modality", "architecture_tokenizer", "architecture_instruct_type",
  "pricing_prompt", "pricing_completion", "pricing_request", "pricing_image",
  "pricing_audio", "pricing_web_search", "pricing_internal_reasoning",
  "pricing_input_cache_read", "pricing_input_cache_write",
  "top_provider_context_length", "top_provider_max_completion_tokens",
  "knowledge_cutoff", "expiration_date", "fetched_at"];
function compareText(left, right) {
  return String(left || "").localeCompare(String(right || ""), undefined, { sensitivity: "base" });
}
function compareNumber(left, right) {
  const l = Number(left) || 0;
  const r = Number(right) || 0;
  return l < r ? -1 : (l > r ? 1 : 0);
}
function compareModels(left, right, key) {
  if (key === "model_provider") return compareText(modelProviderName(left), modelProviderName(right));
  if (key === "model_allowed") return compareNumber(left.allowed || left.selected, right.allowed || right.selected);
  if (key === "model_name") return compareText(modelDisplayName(left), modelDisplayName(right));
  if (key === "model_id") return compareText(left.wire_model_id, right.wire_model_id);
  if (key === "model_context") return compareNumber(left.context_length, right.context_length);
  if (key === "model_prompt_price") return compareNumber(left.pricing_prompt, right.pricing_prompt);
  if (key === "model_completion_price") return compareNumber(left.pricing_completion, right.pricing_completion);
  return 0;
}
function sortedModels(models) {
  const descriptors = [["model_provider", modelSort.requiredAscending],
    [modelSort.primary, modelSort.ascending], ["model_id", true],
    ["model_completion_price", true], ["model_prompt_price", true]];
  return [...models].sort((left, right) => {
    const used = new Set();
    for (const [key, ascending] of descriptors) {
      if (used.has(key)) continue;
      used.add(key);
      const result = compareModels(left, right, key);
      if (result !== 0) return ascending ? result : -result;
    }
    return compareText(left.wire_model_id, right.wire_model_id);
  });
}
function renderModelCatalog() {
  const query = modelSearchInput.value.trim().toLocaleLowerCase();
  const matches = (workspaceState?.models ?? []).filter((model) => {
    const text = modelSearchKeys.map((key) => model[key] ?? "").join(" ").toLocaleLowerCase();
    return query === "" || text.includes(query);
  });
  const models = sortedModels(matches);
  modelList.replaceChildren();
  for (const model of models) {
    const row = document.createElement("tr");
    row.dataset.modelId = model.id;
    row.tabIndex = 0;
    row.setAttribute("aria-selected", selectedModelIds.has(model.id) ? "true" : "false");
    const allowed = document.createElement("td");
    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.checked = model.allowed === true || model.selected === true;
    checkbox.dataset.modelId = model.id;
    checkbox.dataset.default = model.selected === true ? "true" : "false";
    checkbox.setAttribute("aria-label", `${t("Use")} ${modelDisplayName(model)}`);
    allowed.append(checkbox);
    row.append(allowed);
    const values = [modelProviderName(model), modelDisplayName(model),
      model.wire_model_id || model.id, Number(model.context_length || 0).toLocaleString(),
      formatModelPrice(model.pricing_prompt), formatModelPrice(model.pricing_completion)];
    values.forEach((value, index) => {
      const cell = document.createElement("td");
      cell.textContent = value;
      if (index >= 3) cell.className = "numeric";
      if (index === 2) cell.className = "model-id-cell";
      if (index === 1) cell.title = model.description || model.wire_model_id || "";
      row.append(cell);
    });
    row.addEventListener("click", (event) => {
      if (event.target instanceof HTMLInputElement) return;
      if (event.shiftKey && lastSelectedModelId !== null) {
        const anchor = models.findIndex((candidate) => candidate.id === lastSelectedModelId);
        const current = models.findIndex((candidate) => candidate.id === model.id);
        if (!event.metaKey && !event.ctrlKey) selectedModelIds.clear();
        if (anchor >= 0 && current >= 0) {
          const first = Math.min(anchor, current);
          const last = Math.max(anchor, current);
          for (let index = first; index <= last; index += 1) {
            selectedModelIds.add(models[index].id);
          }
        }
      } else {
        if (!event.metaKey && !event.ctrlKey) selectedModelIds.clear();
        if (selectedModelIds.has(model.id)) selectedModelIds.delete(model.id);
        else selectedModelIds.add(model.id);
        lastSelectedModelId = model.id;
      }
      renderModelCatalog();
    });
    modelList.append(row);
  }
  if (models.length === 0) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 7;
    cell.textContent = query ? t("No matching models.") : t("No Models Available");
    row.append(cell);
    modelList.append(row);
  }
  for (const button of modelTable.querySelectorAll("thead button[data-sort]")) {
    const key = button.dataset.sort;
    const active = key === "model_provider" || key === modelSort.primary;
    const ascending = key === "model_provider" ? modelSort.requiredAscending : modelSort.ascending;
    button.closest("th").setAttribute("aria-sort", active ?
      (ascending ? "ascending" : "descending") : "none");
  }
  if (preferenceWorkActive) {
    modelCatalogStatus.textContent = t("Fetching models...");
  } else if (models.length === 0 && query) {
    modelCatalogStatus.textContent = t("No matching models.");
  } else if (query) {
    modelCatalogStatus.textContent = models.length === 1 ? t("1 model shown.") :
      formatLocalized("%lu models shown.", models.length);
  } else if (models.length === 0) {
    modelCatalogStatus.textContent = t("No Models Available");
  } else {
    modelCatalogStatus.textContent = models.length === 1 ? t("1 model available.") :
      formatLocalized("%lu models available.", models.length);
  }
  updateControls();
}
function renderWorkspace() {
  const session = activeSession();
  chatTitle.textContent = session?.name || t("Untitled Session");
  renderSessionList();
  renderInspector();
  renderDefaults();
  renderModelCatalog();
  updateControls();
}
function currentOptions(scope) {
  const defaults = scope === "defaults";
  return { modelId: defaults ? defaultModelInput.value : sessionModelInput.value,
    webProvider: defaults ? defaultWebProviderSelect.value : webProviderSelect.value,
    webSearchEnabled: defaults ? defaultWebSearchInput.checked : webSearchInput.checked,
    limitToOneTool: defaults ? defaultLimitToOneToolInput.checked : limitToOneToolInput.checked,
    answerQualityEnabled: defaults ? defaultAnswerQualityInput.checked : answerQualityInput.checked,
    roundLimit: Number(defaults ? defaultRoundLimitInput.value : roundLimitInput.value) };
}
function saveOptions(scope) {
  if (!workerReady || requestActive || workspaceState === null) return;
  const status = scope === "defaults" ? defaultsStatus : preferencesStatus;
  status.dataset.state = "running";
  status.textContent = "Saving…";
  worker?.postMessage({ type: "save-options", scope, options: currentOptions(scope) });
}
function loadTimelinePage(html) {
  timelineReady = false;
  timelineFrame.addEventListener("load", () => { timelineReady = true; }, { once: true });
  timelineFrame.srcdoc = html;
}
function applyTimelineScript(script) {
  if (!timelineReady || !script) { worker?.postMessage({ type: "reload-timeline" }); return; }
  try { timelineFrame.contentWindow.eval(script); }
  catch { timelineReady = false; worker?.postMessage({ type: "reload-timeline" }); }
}
function handleConversationEvent(message) {
  if (message.kind === "timeline-script") applyTimelineScript(message.payload);
  else if (message.kind === "timeline-reload") worker?.postMessage({ type: "reload-timeline" });
  else if (message.kind === "session-updated") worker?.postMessage({ type: "reload-workspace" });
}
function handleWorkerMessage(event) {
  const message = event.data;
  if (!message || typeof message.type !== "string") return;
  switch (message.type) {
    case "ready":
      workerReady = true;
      try { installSharedAppearance(message.appearanceCss); }
      catch (error) { showWasmError(error.message); workerReady = false; break; }
      statusElement.dataset.state = "ready";
      statusElement.textContent = message.message;
      wasmDetailElement.hidden = false;
      wasmDetailElement.textContent = message.detail;
      statusElement.dataset.databasePersistent = message.databasePersistent ? "true" : "false";
      statusElement.dataset.sessionCount = String(message.databaseSessionCount ?? 0);
      statusElement.dataset.sessionId = String(message.databaseSessionId ?? 0);
      statusElement.dataset.capabilityProfile = JSON.stringify(message.capabilityProfile ?? null);
      systemPromptText = message.capabilityProfile?.systemPrompt || "";
      break;
    case "workspace-state":
      workspaceState = { sessions: message.sessions || [], models: message.models || [],
        activeSessionId: Number(message.activeSessionId), activeOptions: message.activeOptions,
        defaultOptions: message.defaultOptions };
      statusElement.dataset.sessionCount = String(workspaceState.sessions.length);
      statusElement.dataset.sessionId = String(workspaceState.activeSessionId);
      renderWorkspace();
      if (!openedPreferencesOnLoad) {
        openedPreferencesOnLoad = true;
        showPreferencesPane("authentication");
        preferencesWindow.showModal();
      }
      break;
    case "timeline-page": loadTimelinePage(message.html); break;
    case "conversation-event": handleConversationEvent(message); break;
    case "key-state":
      keyAvailable = message.available === true;
      break;
    case "request-started": requestActive = true;
      renderSessionList(); break;
    case "response-complete": requestActive = false;
      promptInput.focus(); renderSessionList(); break;
    case "request-cancelled": requestActive = false;
      renderSessionList(); break;
    case "request-error": requestActive = false;
      renderSessionList(); break;
    case "preferences-saved":
      if (message.scope === "defaults") {
        defaultsStatus.dataset.state = "complete";
        defaultsStatus.textContent = t("Session defaults apply to new sessions");
      } else {
        preferencesStatus.dataset.state = "complete";
        preferencesStatus.textContent = t("Changes apply to the selected chat.");
      }
      break;
    case "model-refresh-started": preferenceWorkActive = true;
      modelCatalogStatus.textContent = t("Fetching models...");
      providerDialogStatus.textContent = t("Fetching models..."); break;
    case "model-refresh-complete": preferenceWorkActive = false;
      modelCatalogStatus.textContent = "OpenRouter models fetched.";
      providerDialogStatus.textContent = "OpenRouter models fetched."; break;
    case "model-refresh-error": preferenceWorkActive = false;
      modelCatalogStatus.textContent = message.message;
      providerDialogStatus.textContent = message.message; break;
    case "model-allowed-saved": modelCatalogStatus.textContent = "Model availability saved."; break;
    case "workspace-error":
      (preferencesPane === "models" ? modelCatalogStatus :
        (preferencesPane === "defaults" ? defaultsStatus : preferencesStatus)).textContent = message.message;
      break;
    case "error": showWasmError(message.message); workerReady = false; break;
    default: break;
  }
  updateControls();
}
function handleWorkerFailure() {
  workerReady = false; keyAvailable = false; requestActive = false;
  workspaceState = null; worker = null;
  showWasmError("The WebAssembly Worker stopped unexpectedly.");
  updateControls();
}
function startWorker() {
  if (!("Worker" in window)) { showWasmError("This browser does not support Web Workers."); return; }
  worker = new Worker(new URL("./worker.js", import.meta.url), { type: "module" });
  worker.addEventListener("message", handleWorkerMessage);
  worker.addEventListener("error", handleWorkerFailure, { once: true });
  updateControls();
}

keyForm.addEventListener("submit", (event) => {
  event.preventDefault();
  if (!workerReady || requestActive || apiKeyInput.value.trim() === "") return;
  worker.postMessage({ type: "set-key", key: apiKeyInput.value });
  apiKeyInput.value = "";
});
promptForm.addEventListener("submit", (event) => {
  event.preventDefault();
  if (requestActive) { worker?.postMessage({ type: "cancel-request" }); return; }
  const prompt = promptInput.value.trim();
  if (!workerReady || !keyAvailable || prompt === "") return;
  promptInput.value = "";
  worker.postMessage({ type: "submit-prompt", prompt });
});
promptInput.addEventListener("input", updateControls);
preferencesForm.addEventListener("submit", (event) => event.preventDefault());
defaultsForm.addEventListener("submit", (event) => event.preventDefault());
for (const control of [sessionModelInput, webSearchInput, webProviderSelect,
  limitToOneToolInput, answerQualityInput, roundLimitInput]) {
  control.addEventListener("change", () => {
    if (control === sessionModelInput && selectedModelFor(sessionModelInput)?.hosted_tools_enabled === false) {
      webSearchInput.checked = false;
    }
    updateRoundLabels(); updateControls(); saveOptions("session");
  });
}
for (const control of [defaultModelInput, defaultWebSearchInput, defaultWebProviderSelect,
  defaultLimitToOneToolInput, defaultAnswerQualityInput, defaultRoundLimitInput]) {
  control.addEventListener("change", () => {
    if (control === defaultModelInput && selectedModelFor(defaultModelInput)?.hosted_tools_enabled === false) {
      defaultWebSearchInput.checked = false;
    }
    updateRoundLabels(); updateControls(); saveOptions("defaults");
  });
}
roundLimitInput.addEventListener("input", updateRoundLabels);
defaultRoundLimitInput.addEventListener("input", updateRoundLabels);
authenticationTab.addEventListener("click", () => showPreferencesPane("authentication"));
modelsTab.addEventListener("click", () => showPreferencesPane("models"));
defaultsTab.addEventListener("click", () => showPreferencesPane("defaults"));
promptsTab.addEventListener("click", () => showPreferencesPane("prompts"));
modelSearchInput.addEventListener("input", renderModelCatalog);
modelTable.querySelector("thead").addEventListener("click", (event) => {
  const button = event.target.closest("button[data-sort]");
  if (!button) return;
  const key = button.dataset.sort;
  if (key === "model_provider") modelSort.requiredAscending = !modelSort.requiredAscending;
  else if (modelSort.primary === key) modelSort.ascending = !modelSort.ascending;
  else {
    modelSort.primary = key;
    modelSort.ascending = key !== "model_allowed" && key !== "model_context";
  }
  renderModelCatalog();
});
modelTable.addEventListener("keydown", (event) => {
  if (event.key !== " " || selectedModelIds.size === 0) return;
  event.preventDefault();
  const selected = (workspaceState?.models ?? []).filter((model) => selectedModelIds.has(model.id));
  const eligible = selected.filter((model) => !model.selected);
  if (eligible.length === 0) {
    modelCatalogStatus.textContent = t("Default model is always allowed."); return;
  }
  const allowed = eligible.some((model) => !model.allowed);
  worker?.postMessage({ type: "set-models-allowed",
    changes: eligible.map((model) => ({ modelId: model.id, allowed })) });
});
modelList.addEventListener("change", (event) => {
  const checkbox = event.target;
  if (!(checkbox instanceof HTMLInputElement) || checkbox.type !== "checkbox") return;
  worker?.postMessage({ type: "set-model-allowed", modelId: checkbox.dataset.modelId,
    allowed: checkbox.checked });
});
fetchModelsButton.addEventListener("click", () => modelProviderDialog.showModal());
fetchOpenRouterModelsButton.addEventListener("click", () =>
  worker?.postMessage({ type: "refresh-models" }));
closeModelProviderDialogButton.addEventListener("click", () => modelProviderDialog.close());
newSessionButton.addEventListener("click", () => {
  chatClosed = false; timelineFrame.hidden = false; worker?.postMessage({ type: "create-session" });
});
deleteSessionButton.addEventListener("click", () => {
  const session = activeSession();
  const title = session?.name || t("Untitled Session");
  if (!session || requestActive || !window.confirm(formatLocalized(
    "This will permanently delete \"%@\" and all of its messages. This cannot be undone.",
    title,
  ))) return;
  worker?.postMessage({ type: "delete-session", sessionId: session.id });
});
closeChatButton.addEventListener("click", () => {
  chatClosed = true; timelineFrame.hidden = true; workspace.classList.add("inspector-collapsed");
  renderWorkspace();
});
sidebarToggleButton.addEventListener("click", () => {
  workspace.classList.toggle("sidebar-collapsed");
  const collapsed = workspace.classList.contains("sidebar-collapsed");
  sidebarToggleButton.title = t(collapsed ? "Show Sidebar" : "Hide Sidebar");
  sidebarToggleButton.setAttribute("aria-label", sidebarToggleButton.title);
  sidebarToggleButton.querySelector("span").innerHTML = collapsed ? "&#xf054;" : "&#xf053;";
});
optionsToggleButton.addEventListener("click", () => workspace.classList.toggle("inspector-collapsed"));
preferencesButton.addEventListener("click", () => {
  showPreferencesPane(preferencesPane); preferencesWindow.showModal();
});
closePreferencesButton.addEventListener("click", () => preferencesWindow.close());
preferencesWindow.addEventListener("click", (event) => {
  if (event.target === preferencesWindow) preferencesWindow.close();
});
clearKeyButton.addEventListener("click", () => {
  apiKeyInput.value = ""; worker?.postMessage({ type: "clear-key" });
});
copySessionTitleButton.addEventListener("click", async () => {
  const title = contextMenuSession?.name || t("Untitled Session");
  await navigator.clipboard?.writeText(title);
  sessionContextMenu.hidden = true;
});
copyLastMessageButton.addEventListener("click", async () => {
  const message = contextMenuSession?.last_message_text || "";
  if (message) await navigator.clipboard?.writeText(message);
  sessionContextMenu.hidden = true;
});
document.addEventListener("click", (event) => {
  if (!sessionContextMenu.contains(event.target)) sessionContextMenu.hidden = true;
});
window.addEventListener("pagehide", () => {
  worker?.postMessage({ type: "shutdown" }); worker?.terminate(); worker = null;
});
startWorker();
