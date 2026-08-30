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
const expectedWebTools = [
  "datetime_from_iso8601",
  "datetime_to_iso8601",
  "fontawesome_confirm",
  "fontawesome_search",
  "memory_delete",
  "memory_read",
  "memory_save",
  "openrouter:web_fetch",
  "openrouter:web_search",
  "session_rename",
  "skill_read",
  "skills_list",
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

async function browserStorageContains(sessionId, canary) {
  return webdriverRequest(
    `/session/${sessionId}/execute/async`,
    "POST",
    {
      script: `
        const canary = arguments[0];
        const done = arguments[arguments.length - 1];
        const containsBytes = (bytes, needle) => {
          if (needle.length === 0) return false;
          outer: for (let offset = 0; offset <= bytes.length - needle.length; offset += 1) {
            for (let index = 0; index < needle.length; index += 1) {
              if (bytes[offset + index] !== needle[index]) continue outer;
            }
            return true;
          }
          return false;
        };
        (async () => {
          const needle = new TextEncoder().encode(canary);
          const root = await navigator.storage.getDirectory();
          const scanDirectory = async (directory) => {
            for await (const entry of directory.values()) {
              if (entry.kind === "directory") {
                if (await scanDirectory(entry)) return true;
                continue;
              }
              const bytes = new Uint8Array(await (await entry.getFile()).arrayBuffer());
              if (containsBytes(bytes, needle)) return true;
            }
            return false;
          };
          done(await scanDirectory(root));
        })().catch((error) => done({ error: String(error) }));
      `,
      args: [canary],
    },
  );
}

async function verifyStaticAssets() {
  const pageResponse = await fetch(`${baseUrl}/`);
  if (!pageResponse.ok) {
    throw new Error(`Entry page returned HTTP ${pageResponse.status}.`);
  }
  if (!pageResponse.headers.get("cache-control")?.includes("no-store")) {
    throw new Error("Entry page is missing Cache-Control: no-store.");
  }
  const pageHtml = await pageResponse.text();
  if (
    !pageHtml.includes("--line: transparent") ||
    pageHtml.includes("--line: ButtonBorder")
  ) {
    throw new Error("The entry page can flash a dark separator before Wasm loads.");
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

  const fontAwesomeBaseUrl =
    "https://cdn.jsdelivr.net/npm/@fortawesome/fontawesome-free@7.2.0/webfonts";
  if (
    !pageHtml.includes(`${fontAwesomeBaseUrl}/fa-solid-900.woff2`) ||
    pageHtml.includes("./Fonts/")
  ) {
    throw new Error("The entry page does not use pinned CDN Font Awesome fonts.");
  }
  const japaneseResponse = await fetch(
    `${baseUrl}/Resources/ja.lproj/Localizable.strings`,
  );
  const japaneseSource = await japaneseResponse.text();
  if (
    !japaneseResponse.ok ||
    !japaneseSource.includes('"Session Options" = "セッションオプション"') ||
    !japaneseSource.includes('"Cost In (1M)" = "入力コスト (100万)"')
  ) {
    throw new Error("The browser UI is missing its macOS-aligned Japanese strings.");
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
            "--host-resolver-rules=MAP cdn.jsdelivr.net ~NOTFOUND",
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
              sessionId: Number(status?.dataset.sessionId || 0),
              capabilityProfile: JSON.parse(
                status?.dataset.capabilityProfile || "null")
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
        if (result.capabilityProfile?.defaultAssistantSet !== "world_knowledge") {
          throw new Error("The C web profile did not default to World Knowledge.");
        }
        if ((result.capabilityProfile?.systemPrompt?.length || 0) < 100) {
          throw new Error("The C web profile did not publish its generated system prompt.");
        }
        const actualTools = (result.capabilityProfile?.tools || []).map((tool) =>
          tool.type === "function" ? tool.name : tool.type
        ).sort();
        if (JSON.stringify(actualTools) !== JSON.stringify(expectedWebTools)) {
          throw new Error(
            `The model-facing web tool schema is incorrect: ${JSON.stringify(actualTools)}`,
          );
        }
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

    const originalSessionId = persistentSessionState.id;
    const layoutState = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          const workspace = document.querySelector(".workspace");
          const left = document.querySelector(".sessions-panel");
          const center = document.querySelector(".chat-document");
          const right = document.querySelector(".inspector-panel");
          const leftRect = left.getBoundingClientRect();
          const centerRect = center.getBoundingClientRect();
          const rightRect = right.getBoundingClientRect();
          const selectedSession = document.querySelector(
            '.session-button[aria-current="true"]');
          const timelineBody = document.querySelector("#timeline")
            .contentDocument?.body;
          const timelineStyle = timelineBody
            ? getComputedStyle(timelineBody)
            : null;
          const borderWidth = (selector, side) => getComputedStyle(
            document.querySelector(selector))["border" + side + "Width"];
          return {
            columnCount: getComputedStyle(workspace).gridTemplateColumns
              .split(" ").length,
            ordered: leftRect.right <= centerRect.left &&
              centerRect.right <= rightRect.left,
            leftScroll: getComputedStyle(left).overflowY,
            rightScroll: getComputedStyle(right).overflowY,
            bodyScroll: getComputedStyle(document.body).overflow,
            sessionRows: document.querySelectorAll(".session-button").length,
            newDisabled: document.querySelector("#new-session").disabled,
            appearanceSource: document.querySelector("#status")
              .dataset.appearanceSource,
            panelColor: getComputedStyle(left).backgroundColor,
            barColor: getComputedStyle(document.querySelector(".panel-heading"))
              .backgroundColor,
            selectionColor: getComputedStyle(selectedSession).backgroundColor,
            timelineBackground: timelineStyle?.backgroundColor || "",
            timelineTextColor: timelineStyle?.color || "",
            leftSeparatorWidth: borderWidth(".sessions-panel", "Right"),
            leftSeparatorColor: getComputedStyle(left).borderRightColor,
            rightSeparatorWidth: borderWidth(".inspector-panel", "Left"),
            rightSeparatorColor: getComputedStyle(right).borderLeftColor,
            unwantedBorders: [
              borderWidth(".panel-heading", "Bottom"),
              borderWidth('.session-button[aria-current="true"]', "Top"),
              borderWidth(".composer", "Top"),
              borderWidth(".preferences-tab", "Top"),
              borderWidth("#preferences-form fieldset", "Top"),
              borderWidth("#prompt", "Top")
            ].filter((width) => width !== "0px"),
            groupedCorner: getComputedStyle(
              document.querySelector("#preferences-form fieldset"))
              .borderRadius,
            preferenceTabs: document.querySelectorAll(".preferences-tab").length,
            preferencesOpen: document.querySelector("#preferences-window").open,
            authenticationVisible:
              !document.querySelector("#authentication-pane").hidden,
            defaultsHidden: document.querySelector("#defaults-pane").hidden,
            modelsHidden: document.querySelector("#models-pane").hidden,
            promptsHidden: document.querySelector("#prompts-pane").hidden,
            closeIconFamily: getComputedStyle(
              document.querySelector("#close-preferences .fa-solid")).fontFamily,
            backdropFilter: getComputedStyle(
              document.querySelector("#preferences-window"), "::backdrop")
              .backdropFilter,
            gearDisabled: document.querySelector("#preferences-button").disabled,
            gearFamily: getComputedStyle(
              document.querySelector("#preferences-button .fa-solid")).fontFamily,
            modelSelectorEnabled: !document.querySelector("#session-model").disabled,
            modelChoices: document.querySelector("#session-model").options.length,
            composerControls: ["#sidebar-toggle", "#close-chat", "#options-toggle",
              "#send-prompt"].every((selector) => document.querySelector(selector)),
            composerStatusAbsent: document.querySelector("#conversation-status") === null,
            missingKeyTooltip: document.querySelector("#send-prompt").title,
            sidebarControlsAtBottom: document.querySelector(".session-toolbar")
              .contains(document.querySelector("#new-session")) &&
              document.querySelector(".session-toolbar")
                .contains(document.querySelector("#delete-session")),
            inspectorTitle: document.querySelector(".inspector-panel h2").textContent,
            preferenceLabels: [...document.querySelectorAll(".preferences-tab")]
              .map((tab) => tab.textContent)
          };
        `,
        args: [],
      },
    );
    if (
      layoutState.columnCount !== 3 ||
      !layoutState.ordered ||
      layoutState.leftScroll !== "auto" ||
      layoutState.rightScroll !== "auto" ||
      layoutState.bodyScroll !== "hidden" ||
      layoutState.sessionRows < 1 ||
      layoutState.newDisabled ||
      layoutState.appearanceSource !== "shared-c" ||
      layoutState.panelColor !== "rgb(240, 237, 242)" ||
      layoutState.barColor !== "rgb(216, 194, 229)" ||
      layoutState.selectionColor !== "rgb(137, 102, 154)" ||
      layoutState.timelineBackground !== "rgb(251, 250, 252)" ||
      layoutState.timelineTextColor !== "rgb(48, 46, 49)" ||
      layoutState.leftSeparatorWidth !== "1px" ||
      layoutState.leftSeparatorColor !== "rgb(164, 157, 166)" ||
      layoutState.rightSeparatorWidth !== "1px" ||
      layoutState.rightSeparatorColor !== "rgb(164, 157, 166)" ||
      layoutState.unwantedBorders.length !== 0 ||
      layoutState.groupedCorner === "0px" ||
      layoutState.preferenceTabs !== 4 ||
      !layoutState.preferencesOpen ||
      !layoutState.authenticationVisible ||
      !layoutState.defaultsHidden ||
      !layoutState.modelsHidden ||
      !layoutState.promptsHidden ||
      !layoutState.closeIconFamily.includes("FA7S") ||
      layoutState.backdropFilter !== "none" ||
      layoutState.gearDisabled ||
      !layoutState.gearFamily.includes("FA7S") ||
      !layoutState.modelSelectorEnabled ||
      layoutState.modelChoices < 1 ||
      !layoutState.composerControls ||
      !layoutState.composerStatusAbsent ||
      layoutState.missingKeyTooltip !== "Enter OpenRouter API Key to Send" ||
      !layoutState.sidebarControlsAtBottom ||
      layoutState.inspectorTitle !== "Session Options" ||
      JSON.stringify(layoutState.preferenceLabels) !==
        JSON.stringify(["Accounts", "Models", "Defaults", "Prompts"])
    ) {
      throw new Error(`Three-column workspace layout is invalid: ${JSON.stringify(layoutState)}`);
    }
    const windowControlState = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          const options = document.querySelector("#options-toggle");
          const sidebar = document.querySelector("#sidebar-toggle");
          const chat = document.querySelector(".chat-document");
          const expandedChatRect = chat.getBoundingClientRect();
          options.click();
          const inspectorCollapsed = getComputedStyle(
            document.querySelector(".inspector-panel")).display === "none";
          options.click();
          sidebar.click();
          const sidebarCollapsed = getComputedStyle(
            document.querySelector(".sessions-panel")).display === "none";
          const collapsedChatRect = chat.getBoundingClientRect();
          const chatExpandedIntoSidebar = collapsedChatRect.left < 1 &&
            collapsedChatRect.width > expandedChatRect.width;
          sidebar.click();
          const row = document.querySelector('.session-button[aria-current="true"]');
          row.dispatchEvent(new MouseEvent("contextmenu", {
            bubbles: true, clientX: 20, clientY: 20
          }));
          const contextActions = [...document.querySelectorAll(
            "#session-context-menu button")].map((button) => button.textContent);
          const contextVisible = !document.querySelector(
            "#session-context-menu").hidden;
          document.body.click();
          return { inspectorCollapsed, sidebarCollapsed, chatExpandedIntoSidebar,
            collapsedChatWidth: collapsedChatRect.width, contextActions, contextVisible };
        `,
        args: [],
      },
    );
    if (
      !windowControlState.inspectorCollapsed || !windowControlState.sidebarCollapsed ||
      !windowControlState.chatExpandedIntoSidebar ||
      !windowControlState.contextVisible ||
      JSON.stringify(windowControlState.contextActions) !==
        JSON.stringify(["Copy Title", "Copy Last Message"])
    ) {
      throw new Error(`The browser window controls are invalid: ${JSON.stringify(windowControlState)}`);
    }

    const modelPaneState = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          document.querySelector("#models-tab").click();
          const timelineHtml = document.querySelector("#timeline").srcdoc;
          return {
            windowOpen: document.querySelector("#preferences-window").open,
            catalogVisible: !document.querySelector("#models-pane").hidden,
            defaultsHidden: document.querySelector("#defaults-pane").hidden,
            authenticationHidden:
              document.querySelector("#authentication-pane").hidden,
            rows: document.querySelectorAll("#model-list tr[data-model-id]").length,
            fetchDisabled: document.querySelector("#fetch-models").disabled,
            columns: [...document.querySelectorAll("#model-table thead button")]
              .map((button) => button.textContent),
            initialSort: [...document.querySelectorAll("#model-table thead th")]
              .map((header) => header.getAttribute("aria-sort")),
            timelineFonts: timelineHtml.includes(
              "https://cdn.jsdelivr.net/npm/@fortawesome/" +
              "fontawesome-free@7.2.0/webfonts/fa-solid-900.woff2") &&
              timelineHtml.includes(
                "fontawesome-free@7.2.0/webfonts/fa-regular-400.woff2") &&
              timelineHtml.includes(
                "fontawesome-free@7.2.0/webfonts/fa-brands-400.woff2") &&
              !timelineHtml.includes("file://https://cdn.jsdelivr.net")
          };
        `,
        args: [],
      },
    );
    if (
      !modelPaneState.windowOpen || !modelPaneState.catalogVisible ||
      !modelPaneState.defaultsHidden || !modelPaneState.authenticationHidden ||
      modelPaneState.rows < 1 ||
      !modelPaneState.fetchDisabled || !modelPaneState.timelineFonts
      || JSON.stringify(modelPaneState.columns) !== JSON.stringify([
        "Use", "Provider", "Model", "ID", "Context", "Cost In (1M)",
        "Cost Out (1M)"])
      || modelPaneState.initialSort[0] !== "descending"
      || modelPaneState.initialSort[1] !== "ascending"
    ) {
      throw new Error(`The browser Models preference pane is invalid: ${JSON.stringify(modelPaneState)}`);
    }
    const promptPaneState = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          document.querySelector('#model-table button[data-sort="model_name"]').click();
          const nameSort = document.querySelector(
            '#model-table button[data-sort="model_name"]').closest("th")
              .getAttribute("aria-sort");
          document.querySelector("#prompts-tab").click();
          return {
            promptsVisible: !document.querySelector("#prompts-pane").hidden,
            modelsHidden: document.querySelector("#models-pane").hidden,
            promptLength: document.querySelector("#system-prompt").value.length,
            readOnly: document.querySelector("#system-prompt").readOnly,
            nameSort
          };`,
        args: [],
      },
    );
    if (
      !promptPaneState.promptsVisible || !promptPaneState.modelsHidden ||
      promptPaneState.promptLength < 100 || !promptPaneState.readOnly ||
      promptPaneState.nameSort !== "ascending"
    ) {
      throw new Error(`The browser Prompts/sort UI is invalid: ${JSON.stringify(promptPaneState)}`);
    }
    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          document.querySelector("#defaults-tab").click();
          document.querySelector("#default-answer-quality-enabled").checked = true;
          document.querySelector("#default-round-limit").value = "70";
          document.querySelector("#default-round-limit").dispatchEvent(
            new Event("change", { bubbles: true }));
        `,
        args: [],
      },
    );
    const defaultsDeadline = Date.now() + 5000;
    let defaultsSaved = false;
    while (Date.now() < defaultsDeadline) {
      defaultsSaved = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `return document.querySelector("#default-answer-quality-enabled").checked &&
            document.querySelector("#default-round-limit").value === "70" &&
            document.querySelector("#defaults-status").textContent ===
              "Session defaults apply to new sessions";`,
          args: [],
        },
      );
      if (defaultsSaved) break;
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (!defaultsSaved) {
      throw new Error("The Preferences window did not save session defaults.");
    }
    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      { script: `document.querySelector("#close-preferences").click();`, args: [] },
    );

    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `document.querySelector("#new-session").click();`,
        args: [],
      },
    );
    const createDeadline = Date.now() + 5000;
    let createdState;
    while (Date.now() < createDeadline) {
      createdState = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `
            return {
              count: Number(document.querySelector("#status").dataset.sessionCount),
              id: Number(document.querySelector("#status").dataset.sessionId),
              rows: document.querySelectorAll(".session-button").length
            };
          `,
          args: [],
        },
      );
      if (
        createdState.count === persistentSessionState.count + 1 &&
        createdState.id !== originalSessionId &&
        createdState.rows === createdState.count
      ) {
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (
      createdState.count !== persistentSessionState.count + 1 ||
      createdState.id === originalSessionId ||
      createdState.rows !== createdState.count
    ) {
      throw new Error("The browser UI did not create and select a second chat.");
    }
    const inheritedDefaults = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `return {
          answerQuality: document.querySelector("#answer-quality-enabled").checked,
          roundLimit: document.querySelector("#round-limit").value
        };`,
        args: [],
      },
    );
    if (!inheritedDefaults.answerQuality || inheritedDefaults.roundLimit !== "70") {
      throw new Error(`A new chat did not inherit modal defaults: ${JSON.stringify(inheritedDefaults)}`);
    }
    persistentSessionState = { count: createdState.count, id: createdState.id };

    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          document.querySelector("#web-provider").value = "native";
          document.querySelector("#answer-quality-enabled").checked = true;
          document.querySelector("#round-limit").value = "80";
          document.querySelector("#round-limit").dispatchEvent(
            new Event("change", { bubbles: true }));
        `,
        args: [],
      },
    );
    const preferencesDeadline = Date.now() + 5000;
    let preferencesSaved = false;
    while (Date.now() < preferencesDeadline) {
      preferencesSaved = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `
            return document.querySelector("#preferences-status").textContent ===
              "Changes apply to the selected chat." &&
              document.querySelector("#web-provider").value === "native" &&
              document.querySelector("#answer-quality-enabled").checked &&
              document.querySelector("#round-limit").value === "80";
          `,
          args: [],
        },
      );
      if (preferencesSaved) break;
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (!preferencesSaved) {
      throw new Error("The configuration inspector did not save and reload its values.");
    }

    await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          const originalId = String(arguments[0]);
          [...document.querySelectorAll(".session-button")]
            .find((button) => button.dataset.sessionId === originalId)?.click();
        `,
        args: [originalSessionId],
      },
    );
    const switchDeadline = Date.now() + 5000;
    let switched = false;
    while (Date.now() < switchDeadline) {
      switched = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `return Number(document.querySelector("#status").dataset.sessionId) === arguments[0];`,
          args: [originalSessionId],
        },
      );
      if (switched) break;
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (!switched) {
      throw new Error("The session list did not switch the main chat document.");
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
              sendDisabled: document.querySelector("#send-prompt").disabled,
              sendTooltip: document.querySelector("#send-prompt").title,
              bodyContainsKey: document.body.textContent.includes(arguments[0]),
              timelineReady:
                document.querySelector("#timeline").contentDocument
                  ?.querySelector("#messages") !== null,
              rendererReady: typeof document.querySelector("#timeline")
                .contentWindow?.appendMessage === "function",
              rendererOwnsPage:
                document.querySelector("#timeline").contentDocument
                  ?.querySelector("meta[name=viewport]") !== null
            };
          `,
          args: [canaryKey],
        },
      );
      if (storedState.timelineReady && storedState.sendTooltip === "") {
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (
      !storedState.sendDisabled ||
      storedState.sendTooltip !== "" ||
      storedState.bodyContainsKey ||
      !storedState.timelineReady ||
      !storedState.rendererReady ||
      !storedState.rendererOwnsPage
    ) {
      throw new Error("Worker key handoff did not reach the expected volatile state.");
    }
    const keyedPromptState = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          const prompt = document.querySelector("#prompt");
          const send = document.querySelector("#send-prompt");
          prompt.value = "Test prompt";
          prompt.dispatchEvent(new Event("input", { bubbles: true }));
          const result = { disabled: send.disabled, tooltip: send.title };
          prompt.value = "";
          prompt.dispatchEvent(new Event("input", { bubbles: true }));
          return result;
        `,
        args: [],
      },
    );
    if (keyedPromptState.disabled || keyedPromptState.tooltip !== "") {
      throw new Error(`The keyed Send control is invalid: ${JSON.stringify(keyedPromptState)}`);
    }
    const providerEditorState = await webdriverRequest(
      `/session/${sessionId}/execute/sync`,
      "POST",
      {
        script: `
          document.querySelector("#models-tab").click();
          document.querySelector("#fetch-models").click();
          const result = {
            open: document.querySelector("#model-provider-dialog").open,
            title: document.querySelector("#model-provider-dialog h2").textContent,
            provider: document.querySelector(".provider-row span").textContent,
            fetchEnabled: !document.querySelector("#fetch-openrouter-models").disabled
          };
          document.querySelector("#close-model-provider-dialog").click();
          return result;
        `,
        args: [],
      },
    );
    if (
      !providerEditorState.open ||
      providerEditorState.title !== "Edit Model Providers" ||
      providerEditorState.provider !== "OpenRouter" ||
      !providerEditorState.fetchEnabled
    ) {
      throw new Error(`The model-provider editor is invalid: ${JSON.stringify(providerEditorState)}`);
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
              sendDisabled: document.querySelector("#send-prompt").disabled,
              sendTooltip: document.querySelector("#send-prompt").title
            };
          `,
          args: [],
        },
      );
      if (clearState.sendDisabled &&
          clearState.sendTooltip === "Enter OpenRouter API Key to Send") {
        clearVerified = true;
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (!clearVerified) {
      throw new Error("Explicit key clearing did not disable prompt submission.");
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
      const sendState = await webdriverRequest(
        `/session/${sessionId}/execute/sync`,
        "POST",
        {
          script: `
            const send = document.querySelector("#send-prompt");
            return { disabled: send.disabled, tooltip: send.title };
          `,
          args: [],
        },
      );
      if (sendState.disabled && sendState.tooltip === "") {
        secondKeySet = true;
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (!secondKeySet) {
      throw new Error("Worker did not accept a new volatile key.");
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
              sendDisabled: document.querySelector("#send-prompt")?.disabled,
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
          !reloadState.sendDisabled ||
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
        const persistedCanary = await browserStorageContains(sessionId, canaryKey);
        if (persistedCanary?.error) {
          throw new Error(`Could not inspect OPFS credential state: ${persistedCanary.error}`);
        }
        if (persistedCanary === true) {
          throw new Error("The API key was found in origin-private file storage.");
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
  "PASS: browser enforced its C capability profile, retained its SQLite OPFS session, and forgot its API key.",
);
