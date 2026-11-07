// The aqua skin's icons (config/iconsAqua.json): glossy colour art on a 32 grid, installed into
// ui/icons.js so every glyph assembled afterwards wears it. Each copy mints its own gradient ids.
// Desktop twin: support/aqua/icons.cpp; extension twin: lib/aqua/rules.js.
import ART from '../../../../common/config/iconsAqua.json' with { type: 'json' };
import { setIconSkin } from '../icons.js';

export const AQUA_ICONS = ART;
let minted = 0;
// The art with its `%u` gradient ids made this copy's own.
export const aquaIconArt = (name) => {
  const art = ART.icons[name];
  return art ? art.replaceAll('%u', `aq${(minted += 1)}`) : '';
};

const TABLE = Object.freeze(Object.fromEntries(Object.keys(ART.icons).map((name) => [name, () => aquaIconArt(name)])));

export const setAquaIcons = (on) => setIconSkin(on ? TABLE : null, { grid: ART.size, crisp: false });
