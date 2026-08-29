import createStrappyModule from "./strappy.js";

const helloMessage = "Hello from Strappy WebAssembly.";

let apiKey = "";
let activeRequest = null;
let wasmReady = false;
let strappyModule = null;
let databasePersistent = false;
let databaseSessionCount = 0;
let databaseSessionId = 0;
let capabilityProfile = null;

async function installCapabilityResources(strappy) {
  strappy.FS.mkdirTree("/Resources");
  for (const resourceName of ["AssistantSets.json", "GuidanceTools.json"]) {
    const response = await fetch(new URL(`./Resources/${resourceName}`, import.meta.url));
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
    "strappy_web_capabilities_initialize",
    "number",
    ["string"],
    ["/Resources"],
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
  return strappyModule.ccall(
    "strappy_web_database_error", "string", [], []);
}

function requireDatabaseCall(name) {
  const succeeded = strappyModule.ccall(name, "number", [], []);
  if (succeeded !== 1) {
    throw new Error(databaseError() || `Database operation ${name} failed.`);
  }
}

function readDatabaseState() {
  databaseSessionCount = strappyModule.ccall(
    "strappy_web_database_session_count", "number", [], []);
  databaseSessionId = strappyModule.ccall(
    "strappy_web_database_last_session_id", "number", [], []);
}

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
  const moduleOptions = {
    locateFile(path) {
      return path === "sqlite3.wasm"
        ? new URL("./strappy.wasm", import.meta.url).href
        : path;
    },
  };
  if (globalThis.strappyTestWasmBinary instanceof Uint8Array) {
    moduleOptions.instantiateWasm = function instantiateWasm(imports, onSuccess) {
      return WebAssembly
        .instantiate(globalThis.strappyTestWasmBinary, imports)
        .then((result) => {
          onSuccess(result.instance, result.module);
          return result.instance.exports;
        });
    };
  }
  const strappy = await createStrappyModule(moduleOptions);
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

  await installCapabilityResources(strappy);
  initializeCapabilityProfile(strappy);

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
    requireDatabaseCall("strappy_web_database_initialize_persistent");
    readDatabaseState();
    if (databaseSessionCount === 0) {
      requireDatabaseCall("strappy_web_database_create_session");
      readDatabaseState();
    }
    databasePersistent = true;
  }

  wasmReady = true;
  self.postMessage({
    type: "ready",
    message: helloMessage,
    detail: databasePersistent
      ? "Shared C and persistent SQLite OPFS are ready."
      : "Shared C and temporary SQLite are ready.",
    databasePersistent,
    databaseSessionCount,
    databaseSessionId,
    capabilityProfile,
  });
} catch (error) {
  self.postMessage({
    type: "error",
    message: error instanceof Error ? error.message : "Unknown Worker error.",
  });
}
