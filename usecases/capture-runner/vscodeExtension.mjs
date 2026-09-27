// Captures usecases/docs/vscode-extension/img/* from a real VS Code running the extension out of
// the tree (--extensionDevelopmentPath), driven over its Chromium debug port. One launch per
// theme the shots ask for; constants live in config/vscodeExtension.json.
//   node usecases/capture-runner/vscodeExtension.mjs [--only <name>]
import { VsCodeHost } from './lib/vscodeHost.mjs';
import { makeActionSteps } from './vscode/actionSteps.mjs';
import { PARK, config, runner, still } from './vscode/view.mjs';
import { CHECK_STEPS, STC_STEPS } from './vscode/stcSteps.mjs';
import { EMIT_STEPS } from './vscode/emitSteps.mjs';
import { API_STEPS, PALETTE_STEPS } from './vscode/apiSteps.mjs';

const STEPS = Object.freeze([
  ...STC_STEPS,
  ...EMIT_STEPS,
  ...API_STEPS,
  ...makeActionSteps({ config, runner, still }),
  ...PALETTE_STEPS,
  ...CHECK_STEPS,
]);

// One VS Code per theme, dark first: a colour theme is a launch-time setting.
for (const theme of ['dark', 'light']) {
  const steps = STEPS.filter((step) => runner.wanted(step.name) && runner.themeOf(step.name) === theme);
  if (!steps.length) continue;
  console.log(`vscode extension, ${theme}`);
  const host = await VsCodeHost.launch(config, theme);
  await host.page.mouse.move(PARK.x, PARK.y);
  await runner.play(steps, { host, page: host.page });
  await host.stop();
}
runner.finish();
