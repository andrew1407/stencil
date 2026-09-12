#pragma once
#include "theme.hpp"
#include <QtGlobal>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QVector>

class QAction;

// PORT of browser/js/ui/tipContent.js (and extension src/lib/tipContent.js) — keep the
// three rule-for-rule; tests/tipContent.headless.cpp runs the browser suite's cases.
namespace stencil::gui {

  struct TipBlock {
    enum class Kind { Row, Bullet, Text, Hint, Note };
    Kind kind = Kind::Text;
    QString term;  // Row only: the bolded left column
    QString text;  // the description (Row) or the whole line (everything else)
  };

  struct Tip {
    QString title;
    QStringList keys;  // combos, already display-formatted ("Ctrl+Z", "⇧⌘Z")
    QVector<TipBlock> blocks;
  };

  // A key combo and nothing else — what a trailing "(…)" must be to become keycaps.
  bool isKeyCombo(const QString& s);

  Tip parseTip(const QString& text);

  // Injectable so both modifier conventions (⌥⇧⌃⌘ vs spelled) are testable anywhere.
  constexpr bool kOnMac =
#ifdef Q_OS_MACOS
      true;
#else
      false;
#endif

  // Empty when there is nothing to show. `font` must be the type the tip is DRAWN with:
  // null = QToolTip::font(), wrong for AppTooltip whose body draws in the app font.
  QString renderTip(const QString& text, const Palette& pal, bool mac = kOnMac,
                    const QFont* font = nullptr);

  // Marker on every painted keycap <img>; the "+" carries its own and never shakes.
  inline constexpr const char* kKeycapClass = "stencil-tip-key";
  inline constexpr const char* kJoinerClass = "stencil-tip-plus";
  bool hasKeycaps(const QString& richText);

  // Same boxes with every keycap FACE blanked — diffing the two is how appTooltip finds the caps.
  QString blankKeycaps(const QString& richText);

  // Call once at startup and again on a theme change. Idempotent.
  void setTooltipPalette(const Palette& pal);

  // The PLAIN tooltip, kept so the html can be rebuilt in a new palette.
  inline constexpr const char* kPlainTipProperty = "stencilPlainTip";

  // Empty for text that must be left as is: empty, or already rich text.
  QString enrichedToolTip(const QString& plain, const QFont* font = nullptr);

  // Composed control tooltips — browser utils.js composeControlTitle: heading, " (combo)"
  // and a "— reason" line while disabled, recomposed on every enabled/shortcut change.
  QString composeControlTitle(const QString& base, const QString& combo, bool disabled,
                              const QString& reason);
  inline constexpr const char* kTipBaseProperty = "stencilTipBase";
  inline constexpr const char* kTipReasonProperty = "stencilTipReason";
  inline constexpr const char* kTipHotkeyProperty = "stencilTipHotkey";
  // Unset, the base is read off the current tooltip (its trailing "(combo)" stripped).
  void setTipBase(QObject* target, const QString& base);
  void setTipReason(QObject* target, const QString& reason);
  // A widget with no shortcut of its own wears `hotkey`'s (browser data-hk-title).
  void setTipHotkey(QWidget* target, QAction* hotkey);
  void syncControlTip(QObject* target);

  Palette currentPalette();

  // One combo as keycap chips joined by "+". `scale` shrinks the caps for table and info rows.
  QString comboKeycapsHtml(const QString& combo, const Palette& pal, bool mac = kOnMac,
                           qreal scale = 1.0);

}
