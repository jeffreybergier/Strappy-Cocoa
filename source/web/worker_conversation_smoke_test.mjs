import { readFile } from "node:fs/promises";

const canaryKey = "not-a-real-conversation-key";
const prompt = "Exercise the complete shared conversation path.";
const answer = "Shared runtime answer.";
globalThis.strappyTestWasmBinary = await readFile(
  new URL("./build-release/strappy.wasm", import.meta.url),
);

const workerListeners = new Map();
const workerMessages = [];
let workerMessageWaiter = null;
let capturedRequest = null;
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
globalThis.fetch = async (url, options) => {
  const parsedUrl = new URL(url);
  if (parsedUrl.pathname.includes("/Resources/")) {
    const resourceName = parsedUrl.pathname.split("/").at(-1);
    const bytes = await readFile(
      new URL(`./build-release/Resources/${resourceName}`, import.meta.url),
    );
    return new Response(bytes, { status: 200 });
  }
  capturedRequest = { url, options };
  return new Response(JSON.stringify({
    id: "resp-web-conversation",
    object: "response",
    created_at: 1700000000,
    model: "deepseek/deepseek-v4-flash-latest",
    status: "completed",
    output: [{
      type: "message",
      id: "msg-web-conversation",
      role: "assistant",
      status: "completed",
      content: [{ type: "output_text", text: answer, annotations: [] }],
    }],
    usage: { input_tokens: 4, output_tokens: 3, total_tokens: 7 },
  }), {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "X-Request-Id": "request-web-conversation",
    },
  });
};

await import("./build-release/worker.js?conversation-success-test");

async function waitForWorkerMessage(type, startIndex = 0) {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const message = workerMessages.slice(startIndex).find(
      (item) => item.type === type,
    );
    if (message) {
      return message;
    }
    await new Promise((resolve) => {
      workerMessageWaiter = resolve;
      setTimeout(resolve, 25);
    });
    workerMessageWaiter = null;
  }
  const summary = workerMessages.map((message) => ({
    type: message.type,
    kind: message.kind,
    message: message.message,
  }));
  throw new Error(
    `Timed out waiting for Worker message ${type}: ${JSON.stringify(summary)}`,
  );
}

const readyMessage = await waitForWorkerMessage("ready");
if (
  !readyMessage.appearanceCss?.includes("--accent:#8e1bcf") ||
  !readyMessage.appearanceCss.includes("--selection:#89669a") ||
  !readyMessage.appearanceCss.includes("--panel:#f0edf2")
) {
  throw new Error("The Worker did not publish the shared C Strappy appearance.");
}
const initialWorkspace = await waitForWorkerMessage("workspace-state");
if (
  initialWorkspace.sessions.length !== 1 ||
  initialWorkspace.activeSessionId !== initialWorkspace.sessions[0].id
) {
  throw new Error("The Worker did not publish its initial database-backed session.");
}
const originalSessionId = initialWorkspace.activeSessionId;
const workspaceOperationStart = workerMessages.length;
const workerMessageListener = workerListeners.get("message");
workerMessageListener({ data: { type: "create-session" } });
const createdWorkspace = await waitForWorkerMessage(
  "workspace-state",
  workspaceOperationStart,
);
if (
  createdWorkspace.sessions.length !== 2 ||
  createdWorkspace.activeSessionId === originalSessionId
) {
  throw new Error("Creating a chat did not select a second persisted session.");
}
const createdSessionId = createdWorkspace.activeSessionId;
const saveOperationStart = workerMessages.length;
workerMessageListener({
  data: {
    type: "save-options",
    scope: "session",
    name: "Configured browser chat",
    options: {
      webProvider: "native",
      webSearchEnabled: true,
      limitToOneTool: true,
      answerQualityEnabled: true,
      roundLimit: 7,
    },
  },
});
await waitForWorkerMessage("preferences-saved", saveOperationStart);
const configuredWorkspace = await waitForWorkerMessage(
  "workspace-state",
  saveOperationStart,
);
if (
  configuredWorkspace.activeOptions.web_provider !== "native" ||
  configuredWorkspace.activeOptions.limit_to_one_tool !== true ||
  configuredWorkspace.activeOptions.answer_quality_enabled !== true ||
  configuredWorkspace.activeOptions.round_limit !== 7 ||
  configuredWorkspace.sessions.find((session) => session.id === createdSessionId)
    ?.name !== "Configured browser chat"
) {
  throw new Error("The selected chat configuration was not read back from SQLite.");
}
const defaultsOperationStart = workerMessages.length;
workerMessageListener({
  data: {
    type: "save-options",
    scope: "defaults",
    options: {
      webProvider: "exa",
      webSearchEnabled: true,
      limitToOneTool: false,
      answerQualityEnabled: true,
      roundLimit: 9,
    },
  },
});
await waitForWorkerMessage("preferences-saved", defaultsOperationStart);
const defaultsWorkspace = await waitForWorkerMessage(
  "workspace-state",
  defaultsOperationStart,
);
if (
  defaultsWorkspace.defaultOptions.web_provider !== "exa" ||
  defaultsWorkspace.defaultOptions.answer_quality_enabled !== true ||
  defaultsWorkspace.defaultOptions.round_limit !== 9
) {
  throw new Error("New-chat defaults were not read back from SQLite.");
}
const selectOperationStart = workerMessages.length;
workerMessageListener({
  data: { type: "select-session", sessionId: originalSessionId },
});
const selectedWorkspace = await waitForWorkerMessage(
  "workspace-state",
  selectOperationStart,
);
if (selectedWorkspace.activeSessionId !== originalSessionId) {
  throw new Error("The Worker did not switch back to the requested chat.");
}
const initialMessageCount = workerMessages.length;
workerMessageListener({ data: { type: "set-key", key: canaryKey } });
workerMessageListener({ data: { type: "submit-prompt", prompt } });
await waitForWorkerMessage("response-complete", initialMessageCount);

if (capturedRequest === null) {
  throw new Error("The shared conversation runtime did not call Fetch.");
}
const headers = new Headers(capturedRequest.options.headers);
if (headers.get("Authorization") !== `Bearer ${canaryKey}`) {
  throw new Error("The credential callback did not supply the temporary key.");
}
const requestBody = new TextDecoder().decode(capturedRequest.options.body);
if (requestBody.includes(canaryKey) || !requestBody.includes(prompt)) {
  throw new Error("The request body did not preserve the prompt/key boundary.");
}
const timelinePages = workerMessages
  .slice(initialMessageCount)
  .filter((message) => message.type === "timeline-page");
const finalTimeline = timelinePages.at(-1)?.html || "";
if (!finalTimeline.includes(answer) || !finalTimeline.includes(prompt)) {
  throw new Error("The normalized C-rendered timeline omitted conversation data.");
}

console.log("Complete shared Worker conversation smoke test passed.");
