import { readFile } from "node:fs/promises";

const canaryKey = "not-a-real-expired-conversation-key";
globalThis.strappyTestWasmBinary = await readFile(
  new URL("./build-release/strappy.wasm", import.meta.url),
);

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
globalThis.fetch = async (url) => {
  const parsedUrl = new URL(url);
  if (parsedUrl.pathname.includes("/Resources/")) {
    const resourceName = parsedUrl.pathname.split("/").at(-1);
    const bytes = await readFile(
      new URL(`./build-release/Resources/${resourceName}`, import.meta.url),
    );
    return new Response(bytes, { status: 200 });
  }
  return new Response(JSON.stringify({
    error: {
      message: `Expired credential ${canaryKey}`,
    },
  }), {
    status: 401,
    headers: { "Content-Type": "application/json" },
  });
};

await import("./build-release/worker.js?conversation-invalid-key-test");

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
  throw new Error(
    `Timed out waiting for Worker message ${type}: ${JSON.stringify(
      workerMessages.map((message) => ({
        type: message.type,
        message: message.message,
      })),
    )}`,
  );
}

await waitForWorkerMessage("ready");
const firstRequestMessage = workerMessages.length;
const workerMessageListener = workerListeners.get("message");
workerMessageListener({ data: { type: "set-key", key: canaryKey } });
workerMessageListener({
  data: { type: "submit-prompt", prompt: "Test an expired credential." },
});
const errorMessage = await waitForWorkerMessage(
  "request-error",
  firstRequestMessage,
);

if (
  !errorMessage.message.includes("HTTP 401") ||
  errorMessage.message.includes(canaryKey)
) {
  throw new Error(
    `The Worker did not safely report an invalid or expired key: ${errorMessage.message}`,
  );
}

console.log("Worker invalid/expired-key smoke test passed.");
