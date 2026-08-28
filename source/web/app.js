const statusElement = document.querySelector("#status");
const detailElement = document.querySelector("#detail");

function showError(message) {
  statusElement.dataset.state = "error";
  statusElement.textContent = "Could not load Strappy WebAssembly.";
  detailElement.hidden = false;
  detailElement.textContent = message;
}

function loadStrappyWorker() {
  if (!("Worker" in window)) {
    throw new Error("This browser does not support Web Workers.");
  }

  return new Promise((resolve, reject) => {
    const worker = new Worker(new URL("./worker.js", import.meta.url), {
      type: "module",
    });

    worker.addEventListener("message", (event) => {
      if (event.data?.type === "ready") {
        resolve(event.data);
        return;
      }

      if (event.data?.type === "error") {
        reject(new Error(event.data.message));
      }
    });

    worker.addEventListener("error", () => {
      reject(new Error("The WebAssembly Worker stopped unexpectedly."));
    });
  });
}

try {
  const result = await loadStrappyWorker();
  statusElement.dataset.state = "ready";
  statusElement.textContent = result.message;
  detailElement.hidden = false;
  detailElement.textContent = result.detail;
} catch (error) {
  showError(error instanceof Error ? error.message : "Unknown application error.");
}
