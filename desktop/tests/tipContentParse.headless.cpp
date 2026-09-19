// Parsing a composed `title`: the trailing "(combo)" that becomes keycaps, and the last-line note.
#include "tipContentParts.hpp"

void parseCases() {
  {  // A trailing "(…)" becomes the heading's keycaps — but only when it really is a combo.
    const Tip t = parseTip("Fit to window (Alt+0)");
    check(t.title == "Fit to window" && t.keys == QStringList{"Alt+0"}, "combo lifted off the heading");
    check(parseTip("Undo (⇧⌘Z)").keys == QStringList{"⇧⌘Z"}, "the Mac glyph form");
    check(parseTip("Save name (Enter)").keys == QStringList{"Enter"}, "a bare named key");
    const Tip prose = parseTip("Incognito (choose before adding an image)");
    check(prose.keys.isEmpty() && prose.title == "Incognito (choose before adding an image)",
          "prose in parentheses stays part of the heading");
    check(isKeyCombo("Alt+Shift+O") && isKeyCombo("F5") && !isKeyCombo("add an image first"),
          "isKeyCombo tells a combo from a sentence");
  }

  {  // The shortcut is appended AFTER the body, so it must be found on the last line.
    const Tip t = parseTip("Rotate image left\n— Load an image to rotate");
    check(t.blocks.size() == 1 && t.blocks[0].kind == TipBlock::Kind::NOTE &&
              t.blocks[0].text == "Load an image to rotate",
          "the disabled-reason line renders as the note");
    const Tip k = parseTip("Fit to window\n(⌥0)");
    check(k.keys == QStringList{"⌥0"} && k.blocks.isEmpty(),
          "a line that was nothing but the combo goes away with it");
  }
}
