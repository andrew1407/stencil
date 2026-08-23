// Headless check of the rich control tooltips (src/support/tipContent.cpp) — the desktop
// port of browser/js/ui/tipContent.js, carrying that suite's cases (browser and extension
// tests/tipContent.test.js) so the three renderings of a tooltip cannot drift: a trailing
// "(combo)" becomes keycaps, "term — description" lines become aligned rows, "·" lists
// become bullets, a parenthesised line is a muted hint, and the "— reason" line is the
// note. Pure string work; no display needed.
#include "tipContent.hpp"

#include <QApplication>
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QRegularExpression>
#include <cstdio>

using stencil::gui::isKeyCombo;
using stencil::gui::parseTip;
using stencil::gui::renderTip;
using stencil::gui::themePalette;
using stencil::gui::Tip;
using stencil::gui::TipBlock;

#include "support/check.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // the keycaps are PAINTED, so this needs a GUI app
  const auto pal = themePalette(false);

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
    check(t.blocks.size() == 1 && t.blocks[0].kind == TipBlock::Kind::Note &&
              t.blocks[0].text == "Load an image to rotate",
          "the disabled-reason line renders as the note");
    const Tip k = parseTip("Fit to window\n(⌥0)");
    check(k.keys == QStringList{"⌥0"} && k.blocks.isEmpty(),
          "a line that was nothing but the combo goes away with it");
  }

  {  // The compare control: heading, bulleted rows, and a parenthesised key hint.
    const QString title =
        "Compare with original\n"
        "• None — normal editing\n"
        "• Original — the original only (crop + rotation)\n"
        "• Vertical split — original left, edit right\n"
        "(hold Alt+Shift+O to peek) (⌥O)\n"
        "— Load an image to compare";
    const Tip t = parseTip(title);
    check(t.title == "Compare with original", "heading");
    check(t.keys == QStringList{"⌥O"}, "the combo appended after the body still reaches the heading");
    int rows = 0, hints = 0, notes = 0;
    for (const TipBlock& b : t.blocks) {
      rows += b.kind == TipBlock::Kind::Row;
      hints += b.kind == TipBlock::Kind::Hint;
      notes += b.kind == TipBlock::Kind::Note;
    }
    check(rows == 3 && hints == 1 && notes == 1, "three rows, one hint, one note");
    check(t.blocks[0].term == "None" && t.blocks[0].text == "normal editing",
          "the • marker is stripped — the renderer draws it, so it never lands in the text");
    const QString html = renderTip(title, pal);
    // The rows' own table (cellpadding 1) — the tooltip's outer width-pinning wrapper and
    // the heading row are tables too.
    check(html.count("cellpadding=\"1\"") == 1,
          "consecutive rows share one table, so the columns line up");
    check(html.contains("<b>Compare with original</b>"), "the heading is bold");
    // Spelled out here: on a Mac the same combo renders ⌥⇧O, so a test that leaves the
    // convention to the machine passes on CI and fails on a Mac (see the platform case below).
    check(renderTip(title, pal, false).contains("alt=\"Alt\""), "the hint keeps its keycaps");
  }

  {  // "·" lists and "term — description" on the heading line.
    const Tip d = parseTip("Drag to reorder · drag out of the modal to disconnect");
    check(d.title == "Drag to reorder" && d.blocks.size() == 1 &&
              d.blocks[0].kind == TipBlock::Kind::Bullet,
          "the heading's · list becomes bullets under it");
    check(renderTip("a · b · c", pal).count(QString::fromUtf8("\u2022")) == 2,
          "each · part after the heading becomes its own bullet");
    const Tip s = parseTip("Shared server project — https://stencil.example/p/1");
    check(s.title == "Shared server project" && s.blocks.size() == 1 &&
              s.blocks[0].kind == TipBlock::Kind::Hint,
          "\"term — description\" on the heading line splits into heading + muted subtitle");
    // A row's description keeps its own "·" list instead of being torn into bullets.
    const Tip r = parseTip("x\nV split — left: original · right: current edit");
    check(r.blocks.size() == 1 && r.blocks[0].text == "left: original · right: current edit",
          "a row's description is left whole");
  }

  {  // Keycaps, gesture combos, and the verbs that must NOT wear one.
    const QString chained = renderTip("Undo (Ctrl+Shift+Z)", pal);
    check(chained.count("<img alt=") - chained.count("alt=\"+\"") == 3,
          "a \"+\" chain becomes one cap per key");
    check(renderTip("Save Project (Shift+click: without theme)", pal).contains("alt=\"click\""),
          "a modifier paired with a gesture word is one combo, not a chopped-up key");
    const QString verb = renderTip("Delete every saved project", pal);
    check(!verb.contains("<img"), "the app's own verbs are not keycaps");
    check(renderTip("hold Shift for all 7 points", pal, false).contains("alt=\"Shift\""),
          "…but a key word in a key context is");
  }

  {  // Everything interpolated is escaped, and nothing renders nothing.
    const QString html = renderTip("Rename <b>x</b> & \"y\"", pal);
    check(!html.contains("<b>x</b>") && html.contains("&lt;b&gt;x&lt;/b&gt; &amp;"),
          "markup in a tooltip is escaped, never rendered");
    check(renderTip("", pal).isEmpty() && renderTip("   \n\n", pal).isEmpty(),
          "an empty tooltip renders nothing at all");
  }

  {  // The app-wide filter must leave alone what is already someone else's HTML.
    check(stencil::gui::enrichedToolTip("<b>AI assistant</b><table></table>").isEmpty(),
          "a hand-composed rich tooltip (the chat gear's) is passed through untouched");
    check(stencil::gui::enrichedToolTip("").isEmpty(), "and an empty one is left empty");
    check(!stencil::gui::enrichedToolTip("Crop image (Alt+R)").isEmpty(), "a plain one is enriched");
  }

  {  // A "·" inside PARENTHESES is not a list separator. The compare tooltip's heading —
     // "Compare with the original (Alt+O cycles · hold Alt+Shift+O to peek)" — was split
     // mid-parenthesis, leaving "(Alt+O cycles" as the title and a stray ")" on a bullet.
    const Tip t = parseTip("Compare with the original (Alt+O cycles · hold Alt+Shift+O to peek)");
    check(t.title == "Compare with the original (Alt+O cycles \u00B7 hold Alt+Shift+O to peek)",
          "a parenthesised hint survives the \u00B7 split whole");
    check(t.blocks.isEmpty(), "…and leaves no dangling bullet behind");
    // Outside parentheses it still splits, which is what the separator is for.
    const Tip d = parseTip("Drag to reorder · drag out to disconnect");
    check(d.title == "Drag to reorder" && d.blocks.size() == 1, "a plain \u00B7 list still splits");
  }

  {  // A keycap is PAINTED (Qt's rich text gives a span no border and no radius), so the
     // check has to look inside the picture: it must carry the theme's cap face — not the
     // tooltip's own background, or the key would be invisible against it.
    for (const bool dark : {false, true}) {
      const auto p = themePalette(dark);
      const QString html = renderTip("Undo (Ctrl+Shift+Z)", p);
      check(html.count("<img alt=") - html.count("alt=\"+\"") == 3,
            dark ? "dark: one cap per key" : "light: one cap per key");
      static const QRegularExpression src("base64,([A-Za-z0-9+/=]+)\"");
      const auto m = src.match(html);
      QImage cap;
      check(m.hasMatch() &&
                cap.loadFromData(QByteArray::fromBase64(m.captured(1).toLatin1()), "PNG"),
            "the cap is a real image");
      // Most of a cap IS its face, so the commonest opaque colour is the one to check: it
      // has to be the theme's, and never the tooltip's own background — a key painted in
      // that would be invisible against the tooltip behind it.
      QHash<QRgb, int> seen;
      for (int y = 0; y < cap.height(); y++)
        for (int x = 0; x < cap.width(); x++)
          if (qAlpha(cap.pixel(x, y)) == 255) seen[cap.pixel(x, y)]++;
      QRgb face = 0;
      for (auto it = seen.cbegin(); it != seen.cend(); ++it)
        if (it.value() > seen.value(face, 0)) face = it.key();
      check(QColor(face) == p.bgContainer,
            dark ? "dark: the cap face is the theme's" : "light: the cap face is the theme's");
      check(QColor(face) != p.bgControls,
            dark ? "dark: the cap stands off the tooltip" : "light: the cap stands off the tooltip");
    }
  }

  {  // A modifier is spelled out on Windows/Linux and drawn on a Mac. Qt gives us glyphs
     // for the shortcuts IT composes, but a hand-written "Alt+O" used to stay a word — so
     // one tooltip showed ⌥ and "Alt" side by side.
    check(renderTip("Cycle compare (Alt+O)", pal, false).contains("alt=\"Alt\""),
          "Windows/Linux: modifiers are spelled out");
    check(renderTip("Cycle compare (Alt+O)", pal, true).contains("alt=\"\u2325\""),
          "macOS: Alt is drawn as \u2325");
    check(renderTip("Paste (Ctrl+V)", pal, true).contains("alt=\"\u2303\""),
          "macOS: a literal Ctrl is \u2303, not \u2318");
    // An already-glyphed combo (Qt's own NativeText) is never mapped twice.
    check(renderTip("Save (\u21E7\u2318S)", pal, true) == renderTip("Save (\u21E7\u2318S)", pal, false),
          "a combo Qt already glyphed renders the same either way");
  }

  {  // The caps are marked apart from the "+" between them (browser: .tip-key vs .tip-plus),
     // and can be blanked in boxes of the same size — that pair of renders is how the app
     // tooltip finds where Qt laid the caps out, so it can shake them.
    const QString html = renderTip("Undo (Ctrl+Shift+Z)", pal);
    check(html.count(QLatin1String(stencil::gui::kKeycapClass)) == 3 &&
              html.count(QLatin1String(stencil::gui::kJoinerClass)) == 2,
          "each key is marked a cap and each \"+\" a joiner");
    const QString bare = stencil::gui::blankKeycaps(html);
    check(!bare.isEmpty() && bare.length() < html.length(), "the cap faces blank out");
    check(bare.count("<img alt=") == html.count("<img alt=") &&
              bare.count("width=") == html.count("width="),
          "…leaving every box, and its size, exactly where it was");
    check(bare.count("alt=\"+\"") == html.count("alt=\"+\"") &&
              bare.contains(QLatin1String(stencil::gui::kJoinerClass)),
          "the joiners keep their picture, so a chord's caps stay separable");
    check(stencil::gui::blankKeycaps(renderTip("Bare hover text", pal)).isEmpty(),
          "a tooltip with no caps blanks to nothing at all");
  }

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}
