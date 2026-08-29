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

await waitForWorkerMessage("ready");
const initialMessageCount = workerMessages.length;
const workerMessageListener = workerListeners.get("message");
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
