import createStrappyModule from "./strappy.js";

const helloMessage = "Hello from Strappy WebAssembly.";
const resourceNames = [
  "AssistantSets.json",
  "BundledModels.json",
  "FontAwesomeIcons.json",
  "GuidanceSkills.json",
  "GuidanceTools.json",
  "SystemPrompt.json",
];

let apiKey = "";
let activeRequest = null;
let wasmReady = false;
let strappyModule = null;
let databasePersistent = false;
let databaseSessionCount = 0;
let databaseSessionId = 0;
let capabilityProfile = null;
let appearanceCss = "";
let startupStage = "loading WebAssembly";

async function installResources(strappy) {
  strappy.FS.mkdirTree("/Resources");
  for (const resourceName of resourceNames) {
    const response = await fetch(
      new URL(`./Resources/${resourceName}`, import.meta.url),
    );
    if (!response.ok) {
      throw new Error(`Could not load ${resourceName} (HTTP ${response.status}).`);
    }
    strappy.FS.writeFile(
      `/Resources/${resourceName}`,
      new Uint8Array(await response.arrayBuffer()),
    );
  }
}

function initializeCapabilityProfile(strappy) {
  const succeeded = strappy.ccall(
    "strappy_web_capabilities_initialize", "number", ["string"], ["/Resources"],
  );
  if (succeeded !== 1) {
    const message = strappy.ccall(
      "strappy_web_capabilities_last_error", "string", [], []);
    throw new Error(message || "Could not initialize the web capability profile.");
  }
  capabilityProfile = {
    defaultAssistantSet: strappy.ccall(
      "strappy_web_capabilities_default_assistant_set", "string", [], []),
    tools: JSON.parse(strappy.ccall(
      "strappy_web_capabilities_tools", "string", [], [])),
  };
}

function databaseError() {
  return strappyModule.ccall("strappy_web_database_error", "string", [], []);
}

function conversationError() {
  return strappyModule.ccall(
    "strappy_web_conversation_last_error", "string", [], []);
}

function requireDatabaseCall(name) {
  if (strappyModule.ccall(name, "number", [], []) !== 1) {
    throw new Error(databaseError() || `Database operation ${name} failed.`);
  }
}

function readDatabaseState() {
  databaseSessionCount = strappyModule.ccall(
    "strappy_web_database_session_count", "number", [], []);
  databaseSessionId = strappyModule.ccall(
    "strappy_web_database_last_session_id", "number", [], []);
}

function readAllocatedJson(name, argumentTypes = [], argumentsList = []) {
  const pointer = strappyModule.ccall(
    name, "number", argumentTypes, argumentsList,
  );
  if (!pointer) {
    throw new Error(databaseError() || `Database operation ${name} failed.`);
  }
  try {
    return JSON.parse(strappyModule.UTF8ToString(pointer));
  } finally {
    strappyModule.ccall("strappy_web_string_free", null, ["number"], [pointer]);
  }
}

function loadWorkspaceState() {
  const sessions = readAllocatedJson("strappy_web_database_list_sessions");
  const activeSession = sessions.find(
    (session) => session.id === Number(databaseSessionId),
  );
  if (!activeSession) {
    throw new Error("The active browser session was not found.");
  }
  return {
    sessions,
    activeSessionId: Number(databaseSessionId),
    activeOptions: readAllocatedJson(
      "strappy_web_database_load_session_options",
      ["number"],
      [databaseSessionId],
    ),
    defaultOptions: readAllocatedJson(
      "strappy_web_database_load_default_options",
    ),
  };
}

function postWorkspaceState() {
  const state = loadWorkspaceState();
  databaseSessionCount = state.sessions.length;
  self.postMessage({ type: "workspace-state", ...state });
}

function initializeConversation(sessionId) {
  const wasmSessionId = typeof sessionId === "bigint"
    ? sessionId
    : BigInt(sessionId);
  if (strappyModule.ccall(
    "strappy_web_conversation_initialize",
    "number",
    ["number"],
    [wasmSessionId],
  ) !== 1) {
    throw new Error(conversationError() || "Could not initialize the conversation.");
  }
  databaseSessionId = wasmSessionId;
}

function requireIdleWorkspace() {
  if (!wasmReady) {
    throw new Error("The Worker is not ready.");
  }
  if (activeRequest) {
    throw new Error("Wait for the active prompt to finish or cancel it first.");
  }
}

function selectSession(sessionId) {
  requireIdleWorkspace();
  const normalizedId = Number(sessionId);
  if (!Number.isSafeInteger(normalizedId) || normalizedId < 1) {
    throw new Error("The selected session is invalid.");
  }
  initializeConversation(normalizedId);
  postWorkspaceState();
  postTimeline();
}

function createSession() {
  requireIdleWorkspace();
  requireDatabaseCall("strappy_web_database_create_session");
  readDatabaseState();
  initializeConversation(databaseSessionId);
  postWorkspaceState();
  postTimeline();
}

function deleteSession(sessionId) {
  requireIdleWorkspace();
  const normalizedId = Number(sessionId);
  if (!Number.isSafeInteger(normalizedId) || normalizedId < 1) {
    throw new Error("The selected session is invalid.");
  }
  if (strappyModule.ccall(
    "strappy_web_database_delete_session",
    "number",
    ["number"],
    [BigInt(normalizedId)],
  ) !== 1) {
    throw new Error(databaseError() || "Could not delete the session.");
  }
  readDatabaseState();
  if (databaseSessionCount === 0) {
    requireDatabaseCall("strappy_web_database_create_session");
    readDatabaseState();
  }
  initializeConversation(databaseSessionId);
  postWorkspaceState();
  postTimeline();
}

function renameSession(sessionId, name) {
  requireIdleWorkspace();
  const normalizedName = typeof name === "string" ? name.trim() : "";
  if (normalizedName === "") {
    throw new Error("Enter a session name.");
  }
  if (strappyModule.ccall(
    "strappy_web_database_rename_session",
    "number",
    ["number", "string"],
    [BigInt(Number(sessionId)), normalizedName],
  ) !== 1) {
    throw new Error(databaseError() || "Could not rename the session.");
  }
  postWorkspaceState();
}

function saveOptions(message) {
  requireIdleWorkspace();
  const options = message.options;
  if (!options || typeof options !== "object") {
    throw new Error("Session options are missing.");
  }
  const roundLimit = Number(options.roundLimit);
  if (!Number.isInteger(roundLimit) || roundLimit < 1 || roundLimit > 100) {
    throw new Error("Round limit must be between 1 and 100.");
  }
  const editsDefaults = message.scope === "defaults";
  if (!editsDefaults) {
    const normalizedName = typeof message.name === "string" ? message.name.trim() : "";
    if (normalizedName === "") {
      throw new Error("Enter a session name.");
    }
    if (strappyModule.ccall(
      "strappy_web_database_rename_session",
      "number",
      ["number", "string"],
      [databaseSessionId, normalizedName],
    ) !== 1) {
      throw new Error(databaseError() || "Could not rename the session.");
    }
  }
  const name = editsDefaults
    ? "strappy_web_database_update_default_options"
    : "strappy_web_database_update_session_options";
  const argumentTypes = editsDefaults
    ? ["string", "number", "number", "number", "number"]
    : ["number", "string", "number", "number", "number", "number"];
  const values = [
    String(options.webProvider || "auto"),
    options.webSearchEnabled ? 1 : 0,
    options.limitToOneTool ? 1 : 0,
    options.answerQualityEnabled ? 1 : 0,
    roundLimit,
  ];
  if (!editsDefaults) {
    values.unshift(databaseSessionId);
  }
  if (strappyModule.ccall(name, "number", argumentTypes, values) !== 1) {
    throw new Error(databaseError() || "Could not save session options.");
  }
  postWorkspaceState();
  self.postMessage({
    type: "preferences-saved",
    scope: editsDefaults ? "defaults" : "session",
  });
}

function runWorkspaceOperation(operation) {
  try {
    operation();
  } catch (error) {
    self.postMessage({
      type: "workspace-error",
      message: error instanceof Error ? error.message : "The workspace operation failed.",
    });
  }
}

function loadTimeline() {
  const pointer = strappyModule.ccall(
    "strappy_web_conversation_load_timeline", "number", [], []);
  if (!pointer) {
    throw new Error(conversationError() || "Could not render the conversation.");
  }
  try {
    return strappyModule.UTF8ToString(pointer);
  } finally {
    strappyModule.ccall("strappy_web_string_free", null, ["number"], [pointer]);
  }
}

function postTimeline() {
  self.postMessage({ type: "timeline-page", html: loadTimeline() });
}

function postKeyState() {
  self.postMessage({ type: "key-state", available: apiKey !== "" });
}

function cancelActiveRequest() {
  if (activeRequest) {
    strappyModule?.ccall("strappy_web_conversation_cancel", null, [], []);
  }
}

function clearKey() {
  cancelActiveRequest();
  apiKey = "";
  postKeyState();
}

async function submitPrompt(prompt) {
  if (!wasmReady) {
    self.postMessage({ type: "request-error", message: "The Worker is not ready." });
    return;
  }
  if (apiKey === "") {
    self.postMessage({ type: "request-error", message: "Enter an OpenRouter API key first." });
    return;
  }
  if (typeof prompt !== "string" || prompt.trim() === "") {
    self.postMessage({ type: "request-error", message: "Enter a prompt first." });
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
      "strappy_web_conversation_submit_prompt",
      "number",
      ["string"],
      [prompt.trim()],
      { async: true },
    );
    if (activeRequest !== requestMarker) {
      return;
    }
    if (succeeded === 1) {
      postTimeline();
      postWorkspaceState();
      self.postMessage({ type: "response-complete" });
      return;
    }
    const message = conversationError() || "The conversation request failed.";
    if (message.toLowerCase().includes("cancel")) {
      self.postMessage({ type: "request-cancelled" });
    } else {
      self.postMessage({ type: "request-error", message });
    }
  } catch (error) {
    self.postMessage({
      type: "request-error",
      message: error instanceof Error ? error.message : "The conversation failed.",
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
    case "submit-prompt":
      void submitPrompt(message.prompt);
      break;
    case "start-test":
      void submitPrompt("Reply with exactly: Strappy transport works.");
      break;
    case "cancel-request":
      cancelActiveRequest();
      break;
    case "reload-timeline":
      try {
        postTimeline();
      } catch (error) {
        self.postMessage({
          type: "request-error",
          message: error instanceof Error ? error.message : "Could not reload the timeline.",
        });
      }
      break;
    case "reload-workspace":
      runWorkspaceOperation(postWorkspaceState);
      break;
    case "select-session":
      runWorkspaceOperation(() => selectSession(message.sessionId));
      break;
    case "create-session":
      runWorkspaceOperation(createSession);
      break;
    case "delete-session":
      runWorkspaceOperation(() => deleteSession(message.sessionId));
      break;
    case "rename-session":
      runWorkspaceOperation(() => renameSession(message.sessionId, message.name));
      break;
    case "save-options":
      runWorkspaceOperation(() => saveOptions(message));
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
  const moduleOptions = {
    locateFile(path) {
      return path === "sqlite3.wasm"
        ? new URL("./strappy.wasm", import.meta.url).href
        : path;
    },
  };
  if (globalThis.strappyTestWasmBinary instanceof Uint8Array) {
    moduleOptions.instantiateWasm = function instantiateWasm(imports, onSuccess) {
      return WebAssembly.instantiate(globalThis.strappyTestWasmBinary, imports)
        .then((result) => {
          onSuccess(result.instance, result.module);
          return result.instance.exports;
        });
    };
  }
  const strappy = await createStrappyModule(moduleOptions);
  strappyModule = strappy;
  strappy.strappyCopyCredential = () => apiKey;

  const utf8Length = new TextEncoder().encode(helloMessage).byteLength;
  if (strappy.ccall(
    "strappy_utf8_validate", "number", ["string", "number"],
    [helloMessage, utf8Length],
  ) !== 1) {
    throw new Error("Shared C code rejected the UTF-8 greeting.");
  }

  startupStage = "loading runtime resources";
  await installResources(strappy);
  startupStage = "validating browser capabilities";
  initializeCapabilityProfile(strappy);
  appearanceCss = strappy.ccall(
    "strappy_web_appearance_css_variables", "string", [], []);
  if (!appearanceCss) {
    throw new Error("Could not load the shared Strappy appearance.");
  }
  startupStage = "initializing SQLite";
  requireDatabaseCall("strappy_web_database_initialize_temporary");
  if (
    typeof navigator !== "undefined" &&
    typeof navigator.storage?.getDirectory === "function" &&
    typeof strappy.installOpfsSAHPoolVfs === "function"
  ) {
    await strappy.installOpfsSAHPoolVfs({
      directory: ".strappy-opfs-sahpool",
      initialCapacity: 6,
    });
    strappy.ccall("strappy_web_database_enable_persistence", null, [], []);
    requireDatabaseCall("strappy_web_database_initialize_persistent");
    databasePersistent = true;
  }
  startupStage = "loading the browser session";
  readDatabaseState();
  if (databaseSessionCount === 0) {
    requireDatabaseCall("strappy_web_database_create_session");
    readDatabaseState();
  }
  initializeConversation(databaseSessionId);

  startupStage = "rendering the conversation";
  wasmReady = true;
  self.postMessage({
    type: "ready",
    message: helloMessage,
    detail: databasePersistent
      ? "Shared C and persistent SQLite OPFS are ready."
      : "Shared C and temporary SQLite are ready.",
    databasePersistent,
    databaseSessionCount,
    databaseSessionId: Number(databaseSessionId),
    capabilityProfile,
    appearanceCss,
  });
  postWorkspaceState();
  postTimeline();
} catch (error) {
  self.postMessage({
    type: "error",
    message: `${startupStage}: ${
      error instanceof Error ? error.message : "Unknown Worker error."
    }`,
  });
}
