import { resolve } from "node:path";

// Headless UI test (v3 framework). CI sets LOGOS_QT_MCP; interactive:
//   nix build .#test-framework -o result-mcp && node tests/ui-tests.mjs
// Hermetic: nix build .#integration-test -L
// Framework supports click(text)/expectTexts/screenshot/property-inspection — NOT text typing.
// CAVEAT: expectTexts = findByProperty("text") — matches elements by text REGARDLESS of
// visibility (proves they exist/instantiate in the QML tree, not that they're visibly rendered).
const root = process.env.LOGOS_QT_MCP || new URL("../result-mcp", import.meta.url).pathname;
const { test, run } = await import(resolve(root, "test-framework/framework.mjs"));

test("radio_ui: QML loads, both tab labels instantiate", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["Stream", "Listen"]); },
    { timeout: 15000, interval: 500, description: "UI to load" }
  );
});

test("radio_ui: Stream-tab setup-form elements instantiate (#7)", async (app) => {
  await app.expectTexts(["Station name", "Visibility", "Public", "Private", "Start"]);
});

test("radio_ui: status light instantiates with default-state label (#8)", async (app) => {
  // streamState defaults to "idle" → stateLabel() = "Waiting for OBS…". expectTexts ignores
  // visibility, so this proves the status label exists/binds; live transitions need the real app.
  await app.expectTexts(["Waiting for OBS…"]);
});
test("radio_ui: Listen-tab elements instantiate (#9)", async (app) => {
  // empty-state label + Add button (station rows + play need live backend data → real app)
  await app.expectTexts(["Listen", "Open to discover stations", "Add"]);
});

test("radio_ui: empty/transitional state copy (#14)", async (app) => {
  // Stream status (idle→"Waiting for OBS…") + Listen empty state both instantiate.
  await app.expectTexts(["Waiting for OBS…", "Open to discover stations"]);
});

test("radio_ui: failed start surfaces an error banner (#15)", async (app) => {
  // mediamtx isn't on PATH in the standalone-app sandbox, so clicking Start fails →
  // the error must be surfaced (not a silent dead-end). This drives the REAL backend.
  await app.click("Start");
  await app.waitFor(
    async () => { await app.expectTexts(["Broadcast server (MediaMTX) isn't available on this system."]); },
    { timeout: 10000, interval: 500, description: "error banner" }
  );
});

// Tap-to-play with live stations needs delivery_module announces → cross-machine demo.
// Note: the Start->OBS-card click-through spawns MediaMTX + needs a non-gated backend, so it is
// verified in the running app / cross-machine demo, not this hermetic test.

run();
