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
  if (
    pageResponse.headers.has("cross-origin-opener-policy") ||
    pageResponse.headers.has("cross-origin-embedder-policy")
  ) {
    throw new Error("The single-tab SAH-pool build unexpectedly requires COOP/COEP.");
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

}

async function verifyBrowserExecution() {
  const session = await webdriverRequest("/session", "POST", {
    capabilities: {
      alwaysMatch: {
        browserName: "chrome",
        "goog:chromeOptions": {
          args: [
            "--headless=new",
            "--no-sandbox",
            "--disable-dev-shm-usage",
            `--unsafely-treat-insecure-origin-as-secure=${baseUrl}`,
          ],
        },
        "goog:loggingPrefs": { browser: "ALL" },
      },
    },
  });
  const sessionId = session.sessionId;
  let persistentSessionState;

  try {
    await webdriverRequest(`/session/${sessionId}/url`, "POST", {
      url: `${baseUrl}/`,
    });

    const deadline = Date.now() + 60000;
    let lastStartupState;
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
              detail: detail?.textContent || "",
              databasePersistent: status?.dataset.databasePersistent || "false",
              sessionCount: Number(status?.dataset.sessionCount || 0),
              sessionId: Number(status?.dataset.sessionId || 0)
            };
          `,
          args: [],
        },
      );
      lastStartupState = result;

      if (result.state === "error") {
        throw new Error(`Browser application failed: ${result.detail}`);
      }
      if (result.state === "ready") {
        if (result.message !== "Hello from Strappy WebAssembly.") {
          throw new Error("Browser rendered an unexpected Wasm greeting.");
        }
        if (
          !result.detail.includes("persistent SQLite OPFS") ||
          result.databasePersistent !== "true" ||
          result.sessionCount < 1 ||
          result.sessionId < 1
        ) {
          throw new Error(
            `Browser did not initialize persistent SQLite through shared C: ${JSON.stringify(result)}`,
          );
        }
        persistentSessionState = {
          count: result.sessionCount,
          id: result.sessionId,
        };
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    if (Date.now() >= deadline) {
      const logs = await webdriverRequest(
        `/session/${sessionId}/se/log`,
        "POST",
        { type: "browser" },
      );
      throw new Error(
        `Timed out waiting for the WebAssembly Worker. Last state: ${JSON.stringify(lastStartupState)}. Browser logs: ${JSON.stringify(logs)}`,
      );
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
    const restartDeadline = Date.now() + 60000;
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
    const reloadDeadline = Date.now() + 60000;
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
              sessionStorageCount: sessionStorage.length,
              databasePersistent:
                document.querySelector("#status")?.dataset.databasePersistent || "false",
              databaseSessionCount: Number(
                document.querySelector("#status")?.dataset.sessionCount || 0),
              databaseSessionId: Number(
                document.querySelector("#status")?.dataset.sessionId || 0)
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
          reloadState.sessionStorageCount !== 0 ||
          reloadState.databasePersistent !== "true"
        ) {
          throw new Error("Reload did not forget the API key.");
        }
        if (
          reloadState.databaseSessionCount !== persistentSessionState.count ||
          reloadState.databaseSessionId !== persistentSessionState.id
        ) {
          throw new Error("The C-created SQLite session did not survive reload.");
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
await verifyBrowserExecution();
console.log(
  "PASS: browser loaded Wasm, retained its SQLite OPFS session, and forgot its API key.",
);
