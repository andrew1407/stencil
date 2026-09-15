// One capture run: the frozen step table an app script declares, filtered by `--only`,
// each step told which theme it is taken in, and the size budget checked at the end.
import { makeThemePicker } from './themeSelector.mjs';
import { checkBudget, sizesTable } from './imageBudget.mjs';
import { shoot } from './shots.mjs';

// `--only <name>` limits a run to the steps whose name starts with it, while iterating.
const onlyArg = () => {
  const at = process.argv.indexOf('--only');
  return at >= 0 ? process.argv[at + 1] : null;
};

export function makeShotRunner({ config, out }) {
  const only = onlyArg();
  const themeOf = makeThemePicker(config.theme);
  const wanted = (name) => !only || name.startsWith(only);

  return {
    out,
    wanted,
    themeOf,
    shot: (target, name, opts = {}) => shoot(target, out, name, opts),

    // Steps run in table order and share one context object; a skipped step changes nothing.
    async play(steps, ctx = {}) {
      for (const { name, run } of steps) {
        if (!wanted(name)) continue;
        await run(ctx, themeOf(name), name);
      }
      return ctx;
    },

    finish() {
      checkBudget(out, config.budget);
      console.log(sizesTable(out));
    },
  };
}
