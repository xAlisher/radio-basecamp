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

test("radio_ui: title header instantiates", async (app) => {
  // Module title (left) + subtitle. The delivery-status pill (right) is dynamic, so not asserted here.
  await app.expectTexts(["Radio", "Decentralized broadcast & discovery"]);
});

test("radio_ui: Stream-tab setup-form elements instantiate (#7)", async (app) => {
  await app.expectTexts(["Station name", "Visibility", "Public", "Private", "Start"]);
});

test("radio_ui: status pills instantiate (#8/#15)", async (app) => {
  // Status moved to header pills (Discovery + OBS + Onion). The OBS pill's default label is
  // deterministic; expectTexts ignores visibility so the hidden-until-streaming pill still matches.
  await app.expectTexts(["Waiting for OBS"]);
});
test("radio_ui: Listen-tab elements instantiate (#9)", async (app) => {
  // empty-state label + Add button (station rows + play need live backend data → real app)
  await app.expectTexts(["Listen", "Open to discover stations", "Add"]);
});

test("radio_ui: empty/transitional state copy", async (app) => {
  // Listen empty state + the always-present Activity panel header.
  await app.expectTexts(["Open to discover stations", "Activity"]);
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

test("radio_ui: Tor privacy toggle instantiates, onion default (T7)", async (app) => {
  // Onion-mode privacy control on the Stream form (epic #1), onion is the default option.
  await app.expectTexts(["Privacy", "Onion (Tor)", "Direct (LAN)"]);
});

test("radio_ui: activity log panel instantiates (#12)", async (app) => {
  // Append-only timestamped event log with copy/clear (qml-activitylog-component).
  await app.expectTexts(["Activity", "Clear"]);
});

// Tap-to-play with live stations needs delivery_module announces → cross-machine demo.
// Note: the Start->OBS-card click-through spawns MediaMTX + needs a non-gated backend, so it is
// verified in the running app / cross-machine demo, not this hermetic test.

run();
