#include "chatSheets.hpp"
#include "../aqua/gradient.hpp"
#include "../aqua/still.hpp"

// The chat surfaces' sheet text; the widgets that wear each one live under src/llm/ and src/app/chat/.

namespace stencil::support {

  namespace {
    // Aqua's close is the window's red light (qss/aqua/windows.qss #modalClosePill): the title bar's own
    // transparent-button rule would outrank the app sheet, so it rides here. pal.danger is --aq-red.
    QString aquaCloseLight(const gui::Palette& pal) {
      if (!isAqua()) return QString();
      const SkinGlass& g = skinGlass();
      return QStringLiteral(
                 // The dock lights are 17 px glass beads beside the 13 px close light (browser
                 // css/aqua/windows.css); the retry is a 23 px one (.chat-retry-cta).
                 "#chatTitleBar QToolButton{min-width:17px;max-width:17px;min-height:17px;max-height:17px;"
                 "padding:0;border:1px solid %2;border-radius:8px;background:%3;}"
                 "QToolButton#chatRetry{min-width:23px;max-width:23px;min-height:23px;max-height:23px;"
                 "padding:0;border:1px solid %2;border-radius:11px;background:%3;}"
                 "#chatTitleBar QToolButton[chatClose=\"true\"]{min-width:13px;max-width:13px;min-height:13px;"
                 "max-height:13px;margin:0 4px 0 2px;border:1px solid #00000060;border-radius:7px;background:%1;}")
          .arg(aquaCloseLightQss(pal.danger), g.edge.name(), aquaQssGradient(g.glassStops));
    }
    // browser css/webcore/chat.css: square white wells (--wc-well), the user's on --wc-light, no
    // tail; an error is framed and worded in --danger.
    QString webcoreCardSheet(const gui::Palette& pal) {
      const QString well = QStringLiteral("border:1px solid;border-color:%1 %2 %2 %1;border-radius:0;")
                               .arg(pal.borderMain.name(), skinBevel().hilight.name());
      return QStringLiteral(
                 "#chatCardUser{background:%2;%1}"
                 "#chatCardAssistant, #chatCardMuted, #chatResult{background:%3;%1}"
                 "#chatCardMuted QLabel, #chatResult QLabel{color:%4;background:transparent;}"
                 "#chatCardError{background:%3;border:1px solid %5;border-radius:0;}"
                 "#chatCardError QLabel{color:%5;background:transparent;}")
          .arg(well, pal.bgCoordHover.name(), pal.inputBg.name(), pal.textMuted.name(), pal.danger.name());
    }
  }  // namespace

  QString chatDockSheet(const gui::Palette& pal) {
    return stillUnderAqua(QStringLiteral(
               "#chatTitleBar{background:%1;border:1px solid %2;border-bottom:1px solid %2;}"
               // .chat-hbtn has no padding/border, so the 13 px glyph fills the 23 px chip.
               "#chatTitleBar QToolButton{padding:0;border:none;background:transparent;"
               "border-radius:5px;}"
               "#chatTitleBar QToolButton:hover{background:%6;}"
               "#chatBody{background:%1;border:1px solid %2;border-top:none;}"
               "#chatBody QScrollArea{background:%1;border:none;}"
               "#chatInputArea{background:%1;border-top:1px solid %2;}"
               "#chatBody QScrollArea > QWidget > QWidget{background:transparent;}"
               "#chatInput{background:%3;color:%4;border:1px solid %2;border-radius:8px;"
               "padding:6px 8px;font-size:14px;}"
               "#chatInput:focus{border:1px solid %5;}"
               // --text-muted through the STYLESHEET — under QSS a palette colour loses.
               "QLabel#chatNoteLabel{color:%7;background:transparent;}"
               // browser .chat-attach-chip
               "#chatAttachChip{background:%3;border:1px solid %2;border-radius:6px;}"
               "#chatAttachChip QLabel{color:%4;font-size:11px;background:transparent;}"
               "#chatAttachRemove{border:none;background:transparent;color:%4;"
               "font-size:13px;padding:0 2px;}"
               "#chatAttachRemove:hover{color:%5;}")
        .arg(pal.bgControls.name(), pal.borderMain.name(), pal.inputBg.name(),
             pal.inputText.name(), pal.accent.name(), pal.bgContainer.name(),
             QStringLiteral("rgba(%1,%2,%3,%4)")
                 .arg(pal.textMuted.red())
                 .arg(pal.textMuted.green())
                 .arg(pal.textMuted.blue())
                 .arg(pal.textMuted.alphaF())))
        + aquaCloseLight(pal);
  }

  // Browser .chat-msg parity: the ROLE is colour and side; role/body ride as widget PROPERTIES.
  QString chatCardStyleSheet(const gui::Palette& pal, bool swapped) {
    if (isWebcore()) return webcoreCardSheet(pal);
    const auto rgba = [](const QColor& c, double a) {
      return QStringLiteral("rgba(%1,%2,%3,%4)")
          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
    };
    // Flattened at the tail corner (browser .chat-swapped). Sheet-wide: a per-card local QSS
    // shifted that card's wrapped-label height.
    const QString flatBL =
        QStringLiteral("border-top-left-radius:10px;border-top-right-radius:10px;"
                       "border-bottom-left-radius:0;border-bottom-right-radius:10px;");
    const QString flatBR =
        QStringLiteral("border-top-left-radius:10px;border-top-right-radius:10px;"
                       "border-bottom-left-radius:10px;border-bottom-right-radius:0;");
    const QString& userRadii = swapped ? flatBL : flatBR;
    const QString& otherRadii = swapped ? flatBR : flatBL;
    return QStringLiteral(
               "#chatCardUser{background:%3;border:1px solid %4;%10}"
               "#chatCardAssistant{background:%2;border:1px solid %1;%11}"
               // --text-muted through the STYLESHEET — under QSS a palette colour loses.
               "#chatCardMuted{background:%8;border:1px solid %1;border-radius:10px;}"
               "#chatCardMuted QLabel{color:%7;background:transparent;}"
               // browser .chat-msg-error; the label rule matters — a stylesheet colour beats applyDangerText.
               "#chatCardError{background:%6;border:1px solid %5;%11}"
               "#chatCardError QLabel{color:%9;background:transparent;}"
               // browser .chat-result: the variant card, its label in --text-muted
               "#chatResult{background:%2;border:1px solid %1;border-radius:8px;}"
               "#chatResult QLabel{color:%7;background:transparent;}")
        .arg(pal.borderMain.name(), pal.bgContainer.name(),
             // browser color-mix(accent 14%/32%)
             rgba(pal.accent, 0.14), rgba(pal.accent, 0.32),
             // color-mix(danger 45%/10%) + `color: var(--danger)`
             rgba(pal.danger, 0.45), rgba(pal.danger, 0.10),
             rgba(pal.textMuted, pal.textMuted.alphaF()), pal.bgControls.name(),
             pal.danger.name())
        .arg(userRadii, otherRadii);
  }

  QString chatMenuPanelSheet() {
    return QStringLiteral(
        "#chatMenuPanel QScrollArea{background:transparent;border:none;}"
        "#chatMenuPanel QScrollArea > QWidget > QWidget{background:transparent;}");
  }

  // browser .chat-drop-cue
  QString chatDropCueSheet(const QColor& accent, const QColor& fill) {
    return QStringLiteral("#chatDropCue{border:2px dashed %1;border-radius:10px;background:%2;}")
        .arg(accent.name(), fill.name());
  }

  QString chatDropCueTextSheet(const QColor& accent) {
    return QStringLiteral("color:%1;background:transparent;font-weight:600;").arg(accent.name());
  }

  QString chatHeaderTitleSheet(const QColor& text) {
    return QStringLiteral("color:%1;background:transparent;").arg(text.name());
  }

  // browser .chat-jump-btn: its hover fill comes from the app-wide generic `button:hover`
  // rule, which QSS has no equivalent of, so it is stated here. pal.textKey doubles as --accent-2.
  QString chatJumpButtonSheet(const gui::Palette& pal) {
    return stillUnderAqua(QStringLiteral(
               "QToolButton{border:1px solid %1;border-radius:14px;background:%2;}"
               "QToolButton:hover{border-color:%3;background:%4;}")
        .arg(pal.borderMain.name(), pal.bgControls.name(), pal.accent.name(),
             pal.textKey.name()));
  }

  // Badge on the gear's corner: filled dot + a subtle ring for legibility.
  QString chatStatusDotSheet(const QString& color) {
    return QStringLiteral("background:%1;border-radius:3px;border:1px solid rgba(255,255,255,160);")
        .arg(color);
  }

  // Quiet solid chips: normal border/card background/text, accent border + a
  // faint accent fill on hover. The boxes are qss/app/modals.qss's; only the palette is live.
  QString chatSuggestChipSheet(const gui::Palette& pal) {
    // A skin's chip is its own button (browser .chat-suggest: aqua glass, webcore raised): the overlay's.
    if (isAqua() || isWebcore()) return QString();
    return QStringLiteral("QPushButton{border:1px solid %1;background:%2;color:%3;}"
                          "QPushButton:hover{border-color:%4;background:rgba(%5,%6,%7,26);}")
        .arg(pal.borderMain.name(), pal.bgContainer.name(), pal.textMain.name(),
             pal.accent.name())
        .arg(pal.accent.red())
        .arg(pal.accent.green())
        .arg(pal.accent.blue());
  }

  QString chatThumbPreviewSheet(const QColor& bg, const QColor& line) {
    return QStringLiteral("#chatThumbPreview{background:%1;border:1px solid %2;border-radius:10px;}")
        .arg(bg.name(), line.name());
  }

  // browser .chat-row-menu-btn
  QString chatRowMenuButtonSheet(const QColor& chip, const QColor& border, double restOpacity) {
    const auto rgba = [](const QColor& c, double a) {
      return QStringLiteral("rgba(%1,%2,%3,%4)")
          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
    };
    // Under a skin the trigger is its control: aqua's 21 px glass bead, webcore's raised button
    // (the overlay's own QToolButton rule, which an empty local sheet leaves standing).
    if (isWebcore()) return QString();
    if (isAqua()) {
      const SkinGlass& g = skinGlass();
      return QStringLiteral("QToolButton{padding:0;border:1px solid %1;border-radius:10px;background:%2;}")
          .arg(g.edge.name(), aquaQssGradient(g.glassStops));
    }
    return stillUnderAqua(QStringLiteral("QToolButton{padding:1px;background:%1;"
                          "border:1px solid %2;border-radius:10px;}"
                          "QToolButton:hover{background:%3;border-color:%4;}")
        .arg(rgba(chip, restOpacity), rgba(border, restOpacity), chip.name(), border.name()));
  }

  QString chatPlacementActiveSheet(const QColor& chip) {
    // Aqua's current dock is a blue gel bead; webcore's stays the raised button the rest are (browser
    // .chat-dock-btn-active under css/aqua/ and css/webcore/).
    if (isWebcore()) return QString();
    if (isAqua()) {
      const SkinGlass& g = skinGlass();
      return QStringLiteral("background:%1;border:1px solid %2;border-radius:8px;")
          .arg(aquaQssGradient(g.gelStops), g.gelEdge.name());
    }
    return QStringLiteral("background:%1;border:none;border-radius:5px;").arg(chip.name());
  }

  QString chatToastSheet(const QString& edge) {
    return QStringLiteral(
               "#chatToast{background:rgba(40,46,60,242);border:1px solid rgba(255,255,255,42);"
               "border-left:3px solid %1;border-radius:8px;}"
               "#chatToast QLabel{color:white;background:transparent;}")
        .arg(edge);
  }

}  // namespace stencil::support
