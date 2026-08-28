import { makeOpenRouterTestRequest } from "./openrouter_transport.js";

const baseUrl = process.env.STRAPPY_WEB_BASE_URL;
const webdriverUrl = process.env.STRAPPY_WEBDRIVER_URL;
const expectedResources = [
  "AssistantSets.json",
  "BundledModels.json",
  "FontAwesomeIcons.json",
  "GuidanceSkills.json",
  "GuidanceTools.json",
  "SystemPrompt.json",
];

if (!baseUrl || !webdriverUrl) {
  throw new Error(
    "STRAPPY_WEB_BASE_URL and STRAPPY_WEBDRIVER_URL must be configured.",
  );
}

async function webdriverRequest(path, method = "GET", body) {
  const response = await fetch(`${webdriverUrl}${path}`, {
    method,
    headers: body === undefined ? undefined : { "Content-Type": "application/json" },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await response.text();
  const payload = text ? JSON.parse(text) : { value: null };

  if (!response.ok || payload.value?.error) {
    const message = payload.value?.message || `${response.status} ${response.statusText}`;
    throw new Error(`WebDriver request failed: ${message}`);
  }
  return payload.value;
}

async function verifyStaticAssets() {
  const pageResponse = await fetch(`${baseUrl}/`);
  if (!pageResponse.ok) {
    throw new Error(`Entry page returned HTTP ${pageResponse.status}.`);
  }
  if (!pageResponse.headers.get("cache-control")?.includes("no-store")) {
    throw new Error("Entry page is missing Cache-Control: no-store.");
  }

  const wasmResponse = await fetch(`${baseUrl}/strappy.wasm`);
  if (!wasmResponse.ok) {
    throw new Error(`Wasm module returned HTTP ${wasmResponse.status}.`);
  }
  if (wasmResponse.headers.get("content-type") !== "application/wasm") {
    throw new Error("Wasm module is not served as application/wasm.");
  }
  const wasmBytes = new Uint8Array(await wasmResponse.arrayBuffer());
  if (
    wasmBytes.length < 4 ||
    wasmBytes[0] !== 0x00 ||
    wasmBytes[1] !== 0x61 ||
    wasmBytes[2] !== 0x73 ||
    wasmBytes[3] !== 0x6d
  ) {
    throw new Error("Wasm output does not have a valid binary header.");
  }

  for (const resourceName of expectedResources) {
    const response = await fetch(`${baseUrl}/Resources/${resourceName}`);
    if (!response.ok) {
      throw new Error(`${resourceName} returned HTTP ${response.status}.`);
    }
    if (!response.headers.get("content-type")?.includes("application/json")) {
      throw new Error(`${resourceName} is not served as JSON.`);
    }
    const resource = await response.json();
    if (!resource || typeof resource !== "object") {
      throw new Error(`${resourceName} does not contain a JSON object.`);
    }
    if (
      resourceName === "AssistantSets.json" &&
      !resource.sets?.some((assistantSet) => assistantSet.id === "world_knowledge")
    ) {
      throw new Error("AssistantSets.json does not contain World Knowledge.");
    }
  }

  const databaseGuidanceResponse = await fetch(
    `${baseUrl}/Resources/GuidanceDatabase.json`,
  );
  if (databaseGuidanceResponse.status !== 404) {
    throw new Error("Web build unexpectedly packages database guidance.");
  }

  const transportResponse = await fetch(`${baseUrl}/openrouter_transport.js`);
  if (!transportResponse.ok) {
    throw new Error(`OpenRouter transport returned HTTP ${transportResponse.status}.`);
  }
}

async function verifyTransportModule() {
  const canaryKey = "not-a-real-phase-two-key";
  let capturedUrl;
  let capturedOptions;
  const fetchImplementation = async (url, options) => {
    capturedUrl = url;
    capturedOptions = options;
    return {
      ok: true,
      status: 200,
      headers: new Headers({ "Content-Type": "application/json" }),
      async json() {
        return {
          model: "example/test-model",
          status: "completed",
          output: [{
            type: "message",
            content: [{ type: "output_text", text: "Strappy transport works." }],
          }],
        };
      },
    };
  };
  const result = await makeOpenRouterTestRequest(
    canaryKey,
    new AbortController().signal,
    fetchImplementation,
  );
  const requestBody = JSON.parse(capturedOptions.body);
  if (capturedUrl !== "https://openrouter.ai/api/v1/responses") {
    throw new Error("Transport used an unexpected OpenRouter endpoint.");
  }
  if (capturedOptions.headers.Authorization !== `Bearer ${canaryKey}`) {
    throw new Error("Transport did not place the key in the authorization header.");
  }
  if (
    capturedOptions.headers.Accept !== "application/json" ||
    capturedOptions.headers["X-OpenRouter-Title"] !== "Strappy"
  ) {
    throw new Error("Transport did not use the browser-safe OpenRouter headers.");
  }
  if ("X-OpenRouter-Metadata" in capturedOptions.headers) {
    throw new Error("Transport used an OpenRouter header rejected by browser CORS.");
  }
  if (
    requestBody.model !== "openrouter/free" ||
    requestBody.stream !== false ||
    requestBody.store !== false ||
    requestBody.max_output_tokens !== 256
  ) {
    throw new Error("Transport request does not match the bounded native profile.");
  }
  if (
    result.httpStatus !== 200 ||
    result.status !== "completed" ||
    result.model !== "example/test-model" ||
    result.outputText !== "Strappy transport works."
  ) {
    throw new Error("Transport did not interpret the successful JSON response.");
  }

  try {
    await makeOpenRouterTestRequest(
      canaryKey,
      new AbortController().signal,
      async () => ({
        ok: true,
        status: 200,
        headers: new Headers({ "Content-Type": "application/json" }),
        async json() {
          return { status: "failed", error: { message: canaryKey } };
        },
      }),
    );
    throw new Error("Failed JSON response unexpectedly succeeded.");
  } catch (error) {
    if (!(error instanceof Error) || !error.message.includes("response failure")) {
      throw new Error("Failed JSON response did not produce a safe error.");
    }
    if (error.message.includes(canaryKey)) {
      throw new Error("Failed JSON response exposed the API key.");
    }
  }

  const cancellationController = new AbortController();
  const cancellationPromise = makeOpenRouterTestRequest(
    canaryKey,
    cancellationController.signal,
    async (_url, options) => new Promise((_resolve, reject) => {
      options.signal.addEventListener("abort", () => {
        reject(new DOMException("Cancelled", "AbortError"));
      }, { once: true });
    }),
  );
  cancellationController.abort();
  try {
    await cancellationPromise;
    throw new Error("Cancelled request unexpectedly succeeded.");
  } catch (error) {
    if (!(error instanceof DOMException) || error.name !== "AbortError") {
      throw new Error("Transport did not preserve request cancellation.");
    }
  }

  try {
    await makeOpenRouterTestRequest(
      canaryKey,
      new AbortController().signal,
      async () => ({ ok: false, status: 401 }),
    );
    throw new Error("Invalid-key response unexpectedly succeeded.");
  } catch (error) {
    if (!(error instanceof Error) || !error.message.includes("HTTP 401")) {
      throw new Error("Invalid-key response did not produce a useful error.");
    }
    if (error.message.includes(canaryKey)) {
      throw new Error("Invalid-key error exposed the API key.");
    }
  }
}

async function verifyBrowserExecution() {
  const session = await webdriverRequest("/session", "POST", {
    capabilities: {
      alwaysMatch: {
        browserName: "chrome",
        "goog:chromeOptions": {
          args: ["--headless=new", "--no-sandbox", "--disable-dev-shm-usage"],
        },
      },
    },
  });
  const sessionId = session.sessionId;

  try {
    await webdriverRequest(`/session/${sessionId}/url`, "POST", {
      url: `${baseUrl}/`,
    });

    const deadline = Date.now() + 20000;
    while (Date.now() < deadline) {
      const result = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `
            const status = document.querySelector("#status");
            const detail = document.querySelector("#wasm-detail");
            return {
              state: status?.dataset.state || "loading",
              message: status?.textContent || "",
              detail: detail?.textContent || ""
            };
          `,
          args: [],
        },
      );

      if (result.state === "error") {
        throw new Error(`Browser application failed: ${result.detail}`);
      }
      if (result.state === "ready") {
        if (result.message !== "Hello from Strappy WebAssembly.") {
          throw new Error("Browser rendered an unexpected Wasm greeting.");
        }
        if (!result.detail.includes("strappy_utf8_validate() returned success")) {
          throw new Error("Browser did not confirm the shared C function call.");
        }
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    if (Date.now() >= deadline) {
      throw new Error("Timed out waiting for the WebAssembly Worker.");
    }

    const canaryKey = "not-a-real-browser-key";
    const keyState = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          const input = document.querySelector("#api-key");
          input.value = arguments[0];
          document.querySelector("#key-form").requestSubmit();
          return input.value;
        `,
        args: [canaryKey],
      },
    );
    if (keyState !== "") {
      throw new Error("Main-thread API-key input was not cleared after handoff.");
    }

    const keyDeadline = Date.now() + 5000;
    let storedState;
    while (Date.now() < keyDeadline) {
      storedState = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `
            return {
              status: document.querySelector("#transport-status").textContent,
              testDisabled: document.querySelector("#test-request").disabled,
              bodyContainsKey: document.body.textContent.includes(arguments[0])
            };
          `,
          args: [canaryKey],
        },
      );
      if (!storedState.testDisabled) {
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (storedState.testDisabled || storedState.bodyContainsKey) {
      throw new Error("Worker key handoff did not reach the expected volatile state.");
    }

    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `document.querySelector("#clear-key").click();`,
        args: [],
      },
    );
    const clearDeadline = Date.now() + 5000;
    let clearVerified = false;
    while (Date.now() < clearDeadline) {
      const clearState = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `
            return {
              status: document.querySelector("#transport-status").textContent,
              testDisabled: document.querySelector("#test-request").disabled
            };
          `,
          args: [],
        },
      );
      if (clearState.testDisabled && clearState.status.includes("cleared")) {
        clearVerified = true;
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (!clearVerified) {
      throw new Error("Explicit key clearing did not disable the transport.");
    }

    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `document.querySelector("#shutdown-worker").click();`,
        args: [],
      },
    );
    const shutdownState = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          return {
            status: document.querySelector("#transport-status").textContent,
            restartHidden: document.querySelector("#restart-worker").hidden,
            testDisabled: document.querySelector("#test-request").disabled
          };
        `,
        args: [],
      },
    );
    if (
      shutdownState.restartHidden ||
      !shutdownState.testDisabled ||
      !shutdownState.status.includes("shut down")
    ) {
      throw new Error("Worker shutdown did not clear and disable transport state.");
    }

    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `document.querySelector("#restart-worker").click();`,
        args: [],
      },
    );
    const restartDeadline = Date.now() + 20000;
    let restartVerified = false;
    while (Date.now() < restartDeadline) {
      const restartReady = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `return document.querySelector("#status").dataset.state === "ready";`,
          args: [],
        },
      );
      if (restartReady) {
        restartVerified = true;
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    if (!restartVerified) {
      throw new Error("Worker did not become ready after restart.");
    }

    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          const input = document.querySelector("#api-key");
          input.value = arguments[0];
          document.querySelector("#key-form").requestSubmit();
        `,
        args: [canaryKey],
      },
    );
    const secondKeyDeadline = Date.now() + 5000;
    let secondKeySet = false;
    while (Date.now() < secondKeyDeadline) {
      const testDisabled = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `return document.querySelector("#test-request").disabled;`,
          args: [],
        },
      );
      if (!testDisabled) {
        secondKeySet = true;
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (!secondKeySet) {
      throw new Error("Restarted Worker did not accept a new volatile key.");
    }

    await webdriverRequest(`/session/${sessionId}/refresh`, "POST", {});
    const reloadDeadline = Date.now() + 20000;
    while (Date.now() < reloadDeadline) {
      const reloadState = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `
            return {
              ready: document.querySelector("#status")?.dataset.state === "ready",
              testDisabled: document.querySelector("#test-request")?.disabled,
              keyValue: document.querySelector("#api-key")?.value || "",
              localStorageCount: localStorage.length,
              sessionStorageCount: sessionStorage.length
            };
          `,
          args: [],
        },
      );
      if (reloadState.ready) {
        if (
          !reloadState.testDisabled ||
          reloadState.keyValue !== "" ||
          reloadState.localStorageCount !== 0 ||
          reloadState.sessionStorageCount !== 0
        ) {
          throw new Error("Reload did not forget the API key.");
        }
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error("Timed out waiting for the Worker after reload.");
  } finally {
    await webdriverRequest(`/session/${sessionId}`, "DELETE");
  }
}

await verifyStaticAssets();
await verifyTransportModule();
await verifyBrowserExecution();
console.log("PASS: browser loaded Wasm and preserved the volatile key boundary.");
