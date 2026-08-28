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
            const detail = document.querySelector("#detail");
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
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error("Timed out waiting for the WebAssembly Worker.");
  } finally {
    await webdriverRequest(`/session/${sessionId}`, "DELETE");
  }
}

await verifyStaticAssets();
await verifyBrowserExecution();
console.log("PASS: browser loaded Strappy Wasm and called shared C code.");
