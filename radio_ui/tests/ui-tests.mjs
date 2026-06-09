import { resolve } from "node:path";

// Headless UI test (v3 framework). CI sets LOGOS_QT_MCP; interactive:
//   nix build .#test-framework -o result-mcp && node tests/ui-tests.mjs
// Hermetic: nix build .#integration-test -L
const root = process.env.LOGOS_QT_MCP || new URL("../result-mcp", import.meta.url).pathname;
const { test, run } = await import(resolve(root, "test-framework/framework.mjs"));

test("radio_ui: loads with both tabs", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["Stream", "Listen"]); },
    { timeout: 15000, interval: 500, description: "UI to load" }
  );
});

// Issue #7:  stream tab shows OBS setup card after a (mocked) start
// Issue #8:  status light transitions Waiting -> Receiving -> Live
// Issue #9:  Listen tab renders seeded stations; tap calls radio_module.play
// Issue #14: empty-state copy with no stations / no stream

run();
