#pragma once
#include "theme.hpp"
#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <QVector>

// Rich control tooltips — the desktop rendering of browser/js/ui/tipContent.js.
//
// Every control here already carries its tooltip as ONE plain string
// ("Rotate image left (Alt+R)", "Compare with original\n• None — normal editing\n…").
// Qt prints that flat, exactly as the browser's native `title` popup used to, so this
// parses the same conventions the browser does and renders them as Qt rich text: a bold
// heading, keycaps for the shortcut, term/description rows, bullets, a muted hint line
// and the disabled-reason note.
//
// PORT of browser/js/ui/tipContent.js — keep the two rule-for-rule (the extension carries
// the same port in src/lib/tipContent.js). tests/tipContent.headless.cpp runs the browser
// suite's cases, so the three cannot drift.
namespace stencil::gui {

  // One parsed block of a tooltip body, in source order.
  struct TipBlock {
    enum class Kind { Row, Bullet, Text, Hint, Note };
    Kind kind = Kind::Text;
    QString term;  // Row only: the bolded left column
    QString text;  // the description (Row) or the whole line (everything else)
  };

  // A tooltip's structure: the heading, the shortcut it carries, and its body.
  struct Tip {
    QString title;
    QStringList keys;  // combos, already display-formatted ("Ctrl+Z", "⇧⌘Z")
    QVector<TipBlock> blocks;
  };

  // Whether `s` is a key combo and nothing else — what a trailing "(…)" must be to
  // become keycaps rather than stay part of the heading.
  bool isKeyCombo(const QString& s);

  // Parse a plain tooltip string into its structure. Lines are separated by '\n'.
  Tip parseTip(const QString& text);

  // True on macOS — where a modifier is DRAWN (⌥⇧⌃⌘), not spelled. Injectable so the
  // rendering can be tested for both conventions on any machine.
  constexpr bool kOnMac =
#ifdef Q_OS_MACOS
      true;
#else
      false;
#endif

  // Render a plain tooltip string as Qt rich text, coloured for `pal`. Returns an empty
  // string when there is nothing to show (the caller then leaves the tooltip alone).
  // `mac` draws modifiers as Apple glyphs; it defaults to the platform.
  QString renderTip(const QString& text, const Palette& pal, bool mac = kOnMac);

  // Class marker carried by every painted keycap <img>, so a rendered tooltip can be asked
  // whether it shows any caps at all — appTooltip shakes only the ones that do. The "+"
  // between caps carries its own marker and never shakes (browser: .tip-key vs .tip-plus).
  inline constexpr const char* kKeycapClass = "stencil-tip-key";
  inline constexpr const char* kJoinerClass = "stencil-tip-plus";
  bool hasKeycaps(const QString& richText);

  // The same rich text with every keycap FACE blanked — same boxes, same layout, nothing
  // drawn in them. Rendering both and diffing is how appTooltip finds where Qt put the
  // caps. Empty when the tip draws none.
  QString blankKeycaps(const QString& richText);

  // Install the app-wide tooltip enrichment: every plain `setToolTip` is re-rendered
  // through renderTip. Call once at startup and again on a theme change, so keycaps and
  // muted text follow the palette. Idempotent.
  void setTooltipPalette(const Palette& pal);

  // Where a widget keeps the PLAIN tooltip it was given, so the rendered html can be
  // rebuilt in a new palette (setTooltipPalette) instead of keeping the colours it was
  // first drawn with. Set by the application's QEvent::ToolTipChange filter.
  inline constexpr const char* kPlainTipProperty = "stencilPlainTip";

  // The rich rendering of a plain tooltip in the palette last given to
  // setTooltipPalette — what the application's QEvent::ToolTipChange filter substitutes.
  // Returns an empty string for text that must be left exactly as it is: empty, or
  // already rich text (something composed its own HTML, e.g. the chat provider tooltip).
  QString enrichedToolTip(const QString& plain);

}
