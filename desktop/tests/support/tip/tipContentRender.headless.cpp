// Rendering a parsed tip to HTML: the bulleted rows, the aligned term/description table, the
// keycaps it paints, the Mac glyph forms and the escaping.
#include "tipContentParts.hpp"

void renderCases(const Palette& pal) {
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
      rows += b.kind == TipBlock::Kind::ROW;
      hints += b.kind == TipBlock::Kind::HINT;
      notes += b.kind == TipBlock::Kind::NOTE;
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

  {  // A secondary line reads as a sentence of its own: "Servers — a saved session
     // A reason beginning lowercase is sentence-cased under the heading, but never when its first token is a
     // value rather than a word. Browser twin: tip/content.js sentenceCase, the same cases in its own test.
    const auto firstBlock = [](const QString& title) {
      const Tip t = parseTip(title);
      return t.blocks.isEmpty() ? QString() : t.blocks[0].text;
    };
    check(firstBlock("Servers — a saved session expired, reconnect to sign in again") ==
              "A saved session expired, reconnect to sign in again",
          "the muted line under the heading is sentence-cased");
    check(firstBlock("Crop image\n— add an image first") == "Add an image first",
          "…and so is the disabled reason");
    check(firstBlock("Drag to reorder · drag out of the modal to disconnect") ==
              "Drag out of the modal to disconnect",
          "…and a trailing · hint");
    // A URL, a filename, an identifier, a code fragment: written as they mean.
    check(firstBlock("Shared server project — http://localhost:8090") == "http://localhost:8090",
          "a URL keeps its case");
    check(firstBlock("Open Project — .stencil files only") == ".stencil files only",
          "…and a filename");
    check(firstBlock("Formula — f(x,y) transforms the page") == "f(x,y) transforms the page",
          "…and a code fragment");
    // A LONE word is a value (an axis letter, a mode name), not a sentence.
    check(firstBlock("Axis · x") == "x", "a lone word is left alone");
    check(firstBlock("Servers — Reconnect to sign in again") == "Reconnect to sign in again",
          "already capitalised stays as it is");
  }

  {  // "·" lists and "term — description" on the heading line.
    const Tip d = parseTip("Drag to reorder · drag out of the modal to disconnect");
    check(d.title == "Drag to reorder" && d.blocks.size() == 1 &&
              d.blocks[0].kind == TipBlock::Kind::HINT,
          "a single trailing · piece has nothing to enumerate against, so it's a hint, not a bulleted list of one");
    check(renderTip("a · b · c", pal).count(QString::fromUtf8("\u2022")) == 2,
          "each · part after the heading becomes its own bullet");
    const Tip s = parseTip("Shared server project — https://stencil.example/p/1");
    check(s.title == "Shared server project" && s.blocks.size() == 1 &&
              s.blocks[0].kind == TipBlock::Kind::HINT,
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
}
