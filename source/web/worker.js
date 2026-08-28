import createStrappyModule from "./strappy.js";

const helloMessage = "Hello from Strappy WebAssembly.";

try {
  const strappy = await createStrappyModule();
  const utf8Length = new TextEncoder().encode(helloMessage).byteLength;
  const isValidUtf8 = strappy.ccall(
    "strappy_utf8_validate",
    "number",
    ["string", "number"],
    [helloMessage, utf8Length],
  );

  if (isValidUtf8 !== 1) {
    throw new Error("Shared C code rejected the UTF-8 greeting.");
  }

  self.postMessage({
    type: "ready",
    message: helloMessage,
    detail: "strappy_utf8_validate() returned success from shared C code.",
  });
} catch (error) {
  self.postMessage({
    type: "error",
    message: error instanceof Error ? error.message : "Unknown Worker error.",
  });
}
