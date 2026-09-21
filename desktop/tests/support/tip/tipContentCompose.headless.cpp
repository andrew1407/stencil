// The rest of the rendering, and composeControlTitle: the bindings that keep a control's tooltip
// composed as its enabled state or chord changes, and the width the html pins in its own font.
#include "tipContentParts.hpp"

void composeCases(const Palette& pal) {
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
      // Most of a cap IS its face, so the commonest opaque colour is the one to check: it has to be the
      // theme's, never the tooltip's own background, which would be invisible against the tooltip behind it.
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
    check(html.count(QLatin1String(stencil::gui::KEYCAP_CLASS)) == 3 &&
              html.count(QLatin1String(stencil::gui::JOINER_CLASS)) == 2,
          "each key is marked a cap and each \"+\" a joiner");
    const QString bare = stencil::gui::blankKeycaps(html);
    check(!bare.isEmpty() && bare.length() < html.length(), "the cap faces blank out");
    check(bare.count("<img alt=") == html.count("<img alt=") &&
              bare.count("width=") == html.count("width="),
          "…leaving every box, and its size, exactly where it was");
    check(bare.count("alt=\"+\"") == html.count("alt=\"+\"") &&
              bare.contains(QLatin1String(stencil::gui::JOINER_CLASS)),
          "the joiners keep their picture, so a chord's caps stay separable");
    check(stencil::gui::blankKeycaps(renderTip("Bare hover text", pal)).isEmpty(),
          "a tooltip with no caps blanks to nothing at all");
  }

  {  // composeControlTitle — the browser's utils.js: heading + " (combo)" + the "— reason"
     // line while disabled — and the bindings that keep a control's tooltip composed as its
     // enabled state or chord changes.
    using stencil::gui::composeControlTitle;
    check(composeControlTitle("Image Filter", "\u2325B", true, "Load an image to apply a filter") ==
              "Image Filter (\u2325B)\n\u2014 Load an image to apply a filter",
          "heading, combo and the reason while disabled");
    check(composeControlTitle("Image Filter", "\u2325B", false, "Load an image to apply a filter") ==
              "Image Filter (\u2325B)",
          "the reason stays off an enabled control");
    check(composeControlTitle("Undo", "", true, "Nothing to undo") == "Undo\n\u2014 Nothing to undo",
          "no combo, no parentheses");
    check(composeControlTitle("", "\u2325B", false, "") == "(\u2325B)", "a bare combo");

    QAction crop("Crop Image\u2026");
    crop.setShortcut(QKeySequence("Ctrl+Shift+X"));
    stencil::gui::setTipBase(&crop, "Crop image");
    stencil::gui::setTipReason(&crop, "Load an image to crop");
    QString sc = crop.shortcut().toString(QKeySequence::NativeText);
    check(crop.toolTip() == "Crop image (" + sc + ")", "an action wears its own chord");
    crop.setEnabled(false);
    check(crop.toolTip() == "Crop image (" + sc + ")\n\u2014 Load an image to crop",
          "disabling it adds the reason line");
    crop.setEnabled(true);
    check(crop.toolTip() == "Crop image (" + sc + ")", "…which leaves when it is enabled again");
    crop.setShortcut(QKeySequence("Alt+X"));
    sc = crop.shortcut().toString(QKeySequence::NativeText);
    check(crop.toolTip() == "Crop image (" + sc + ")", "a rebound chord reaches the tooltip");

    // A reason given to an action that only ever had "text (combo)" keeps that text as
    // the heading — the browser reads data-title off the title the same way.
    QAction undo("Undo");
    undo.setShortcut(QKeySequence("Ctrl+Z"));
    const QString z = undo.shortcut().toString(QKeySequence::NativeText);
    undo.setToolTip("Undo (" + z + ")");
    stencil::gui::setTipReason(&undo, "Nothing to undo");
    undo.setEnabled(false);
    check(undo.toolTip() == "Undo (" + z + ")\n\u2014 Nothing to undo",
          "the heading is read off a plain tooltip, its combo stripped");

    // A widget has no chord of its own: it wears the cycle action's, and says why it is grey.
    QWidget filter;
    stencil::gui::setTipBase(&filter, "Image Filter");
    stencil::gui::setTipHotkey(&filter, &crop);
    stencil::gui::setTipReason(&filter, "Load an image to apply a filter");
    check(filter.toolTip() == "Image Filter (" + sc + ")", "a widget wears another action's chord");
    filter.setEnabled(false);
    check(filter.toolTip() == "Image Filter (" + sc + ")\n\u2014 Load an image to apply a filter",
          "a disabled widget says why");
    const Tip t = parseTip(filter.toolTip());
    check(t.title == "Image Filter" && t.keys == QStringList{sc} && t.blocks.size() == 1 &&
              t.blocks[0].kind == TipBlock::Kind::NOTE,
          "…and it all parses as heading + keycap + note");
    crop.setShortcut(QKeySequence("Ctrl+Shift+X"));
    check(filter.toolTip().contains("(" + crop.shortcut().toString(QKeySequence::NativeText) + ")"),
          "rebinding the action re-caps the widget too");
  }

  {  // The html pins its width in the font it is DRAWN in: the app tooltip's body (the app
     // font, 13pt on macOS) is not QToolTip's (11pt there), and a width measured in the
     // small one broke "Image Filter" — and a ⇧⌘X chord — onto two lines.
    auto pinned = [](const QString& html) {
      return QRegularExpression("<table width=\"(\\d+)\"").match(html).captured(1).toInt();
    };
    QFont big = QToolTip::font();
    big.setPointSizeF(qMax(1.0, big.pointSizeF()) * 2);
    const int small = pinned(renderTip("Image Filter", pal, false));
    const int large = pinned(renderTip("Image Filter", pal, false, &big));
    check(small > 0 && large > small, "the pinned width follows the font it is measured in");
    check(pinned(stencil::gui::enrichedToolTip("Image Filter", &big)) == large,
          "enrichedToolTip measures in the font it is given");
    check(renderTip("Crop image (Ctrl+Shift+X)", pal, false).contains("white-space: nowrap"),
          "the keycap row never breaks between its caps");
    // The reason is its own row under the heading, in the warning colour (browser .tip-note).
    const QString note = renderTip("Crop image (Ctrl+Shift+X)\n\u2014 Load an image to crop", pal, false);
    check(note.contains("<div style=\"color:" + pal.warning.name() + "; margin-top:5px;\">Load an image to crop</div>"),
          "the disabled reason is an amber row of its own");
  }
}
