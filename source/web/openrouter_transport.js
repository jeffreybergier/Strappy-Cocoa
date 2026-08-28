// This direct Fetch probe exists only for Phase 2. Phase 3 replaces its
// request and response policy with the shared C client, leaving Fetch as the
// browser transport adapter.
const OPENROUTER_RESPONSES_URL = "https://openrouter.ai/api/v1/responses";
const TEST_MODEL = "openrouter/free";
const TEST_INPUT = "Reply with exactly: Strappy transport works.";
const MAX_OUTPUT_TOKENS = 256;
const MAX_DISPLAY_TEXT_LENGTH = 2000;

function httpErrorMessage(status) {
  switch (status) {
    case 401:
      return "OpenRouter rejected the API key (HTTP 401).";
    case 402:
      return "OpenRouter reported that payment or credits are required (HTTP 402).";
    case 403:
      return "OpenRouter refused this request (HTTP 403).";
    case 429:
      return "OpenRouter rate-limited the request (HTTP 429).";
    default:
      return `OpenRouter returned HTTP ${status}.`;
  }
}

function responseOutputText(payload) {
  if (typeof payload.output_text === "string") {
    return payload.output_text.slice(0, MAX_DISPLAY_TEXT_LENGTH);
  }
  if (!Array.isArray(payload.output)) {
    return "";
  }

  const text = [];
  for (const item of payload.output) {
    if (!Array.isArray(item?.content)) {
      continue;
    }
    for (const part of item.content) {
      if (part?.type === "output_text" && typeof part.text === "string") {
        text.push(part.text);
      }
    }
  }
  return text.join("\n").slice(0, MAX_DISPLAY_TEXT_LENGTH);
}

export async function makeOpenRouterTestRequest(
  apiKey,
  signal,
  fetchImplementation = fetch,
) {
  let response;
  try {
    response = await fetchImplementation(OPENROUTER_RESPONSES_URL, {
      method: "POST",
      headers: {
        Accept: "application/json",
        Authorization: `Bearer ${apiKey}`,
        "Content-Type": "application/json",
        "X-OpenRouter-Title": "Strappy",
      },
      body: JSON.stringify({
        model: TEST_MODEL,
        input: TEST_INPUT,
        max_output_tokens: MAX_OUTPUT_TOKENS,
        store: false,
        stream: false,
      }),
      cache: "no-store",
      credentials: "omit",
      redirect: "error",
      referrerPolicy: "no-referrer",
      signal,
    });
  } catch (error) {
    if (error instanceof DOMException && error.name === "AbortError") {
      throw error;
    }
    throw new Error(
      "The browser could not reach OpenRouter. This may be a network or CORS failure.",
    );
  }

  if (!response.ok) {
    throw new Error(httpErrorMessage(response.status));
  }
  const contentType = response.headers?.get("content-type") || "";
  if (!contentType.toLowerCase().includes("application/json")) {
    throw new Error("OpenRouter did not return a JSON response.");
  }

  let payload;
  try {
    payload = await response.json();
  } catch {
    throw new Error("OpenRouter returned invalid JSON.");
  }
  if (!payload || typeof payload !== "object" || Array.isArray(payload)) {
    throw new Error("OpenRouter returned an invalid response object.");
  }
  if (payload.status === "failed" || payload.error) {
    throw new Error("OpenRouter reported a response failure.");
  }

  return {
    httpStatus: response.status,
    model: typeof payload.model === "string" ? payload.model : "",
    status: typeof payload.status === "string" ? payload.status : "unknown",
    outputText: responseOutputText(payload),
  };
}
