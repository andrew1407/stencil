// One capture run: the frozen step table an app script declares, filtered by `--only`,
// each step told which theme it is taken in, and the size budget checked at the end.
import { makeThemePicker } from '../theme/selector.mjs';
import { checkBudget, sizesOf, sizesTable } from '../imageBudget.mjs';
import { quantizePng } from '../gifTools.mjs';
import { shoot } from './shots.mjs';

// `--only <name>[,<name>…]` limits a run to the steps whose name starts with one of them.
const onlyArg = () => {
  const at = process.argv.indexOf('--only');
  return at >= 0 ? process.argv[at + 1].split(',').filter(Boolean) : null;
};

// `beforeShot(target)` tidies a page first (a passing toast is not the subject); `quantize`
// puts every still on the 256-colour palette.
export function makeShotRunner({ config, out, beforeShot = null, quantize = false }) {
  const only = onlyArg();
  const themeOf = makeThemePicker(config.theme);
  const wanted = (name) => !only || only.some((prefix) => name.startsWith(prefix));

  return {
    out,
    wanted,
    themeOf,
    shot: async (target, name, opts = {}) => {
      if (beforeShot) await beforeShot(target);
      const file = await shoot(target, out, name, opts);
      return quantize ? quantizePng(file) : file;
    },

    // Steps run in table order and share one context object; a skipped step changes nothing.
    async play(steps, ctx = {}) {
      for (const { name, run } of steps) {
        if (!wanted(name)) continue;
        await run(ctx, themeOf(name), name);
      }
      return ctx;
    },

    async finish() {
      const sizes = await sizesOf(out);
      checkBudget(sizes, config.budget);
      console.log(sizesTable(sizes));
    },
  };
}
