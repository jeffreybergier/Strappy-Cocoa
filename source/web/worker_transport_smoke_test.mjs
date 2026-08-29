import { readFile } from "node:fs/promises";

const canaryKey = "not-a-real-worker-key";
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

console.log("Worker Fetch cancellation smoke test passed.");
