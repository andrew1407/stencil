// The context menu's assistant flyout (llm/ChatMenuPanel + llm/chatMenuPanelParts.hpp).
// Browser twin browser/css/components/ctxAssistant.css: the flyout is the chat panel at MENU
// scale, so the prompt chips and the composer carry that sheet's own boxes — the numbers
// here are read straight off it. The dock's chips keep their own, larger, box.
#include "ChatDock.hpp"
#include "ChatMenuPanel.hpp"
#include "chatMenuPanelParts.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QToolButton>
#include <cstdio>

#include "support/check.hpp"

using namespace stencil::gui;

namespace {

  // browser css/components/ctxAssistant.css, and chat/cards.css for the base chip.
  constexpr int CSS_CHIP_FONT_PX = 12;      // .ctx-assist .chat-suggest font-size
  constexpr int CSS_CHIP_PAD_Y = 4;         // …and its padding
  constexpr int CSS_CHIP_PAD_X = 9;
  constexpr int CSS_INPUT_PAD_Y = 6;        // #ctx-assist-input padding
  constexpr int CSS_INPUT_PAD_X = 8;
  constexpr int CSS_INPUT_FONT_PX = 13;
  constexpr int CSS_INPUT_MIN_H = 44;       // …two rows, its resting height
  constexpr int CSS_DOCK_CHIP_PAD_X = 12;   // .chat-suggest, the dock's own scale

  QList<QPushButton*> chipsOf(QWidget* w) {
    return w->findChildren<QPushButton*>(QStringLiteral("chatSuggestChip"));
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM
  const Palette pal = themePalette(true, QStringLiteral("violet"));
  qApp->setStyleSheet(buildStylesheet(true, QStringLiteral("violet")));
  const QString sheet = qApp->styleSheet();

  std::printf("the flyout is the browser's .ctx-assist box:\n");
  {
    ChatMenuPanel panel(nullptr, {}, {}, {}, {}, {});
    panel.restyle(pal);
    panel.resize(panel.minimumWidth(), panel.sizeHint().height());
    panel.show();
    QTest::qWait(80);

    check(panel.layout()->contentsMargins() == MENU_CHAT_PADDING
              && MENU_CHAT_PADDING == QMargins(12, 2, 12, 4),
          "its padding is .ctx-assist's 2px 12px 4px");

    auto* input = panel.findChild<QPlainTextEdit*>(QStringLiteral("chatMenuInput"));
    check(input != nullptr, "the composer is there");
    check(input && input->minimumHeight() == CSS_INPUT_MIN_H,
          "the composer rests two rows tall, like #ctx-assist-input");
    check(input && input->font().pixelSize() == CSS_INPUT_FONT_PX,
          "…at the browser's 13px");
    // The composer row leaves the input what .ctx-assist-row leaves it there, plus the
    // 5px a per-row "…" hangs outside its bubble.
    check(input && input->width() == MENU_CHAT_INPUT_WIDTH + MENU_CHAT_ROW_MENU_OVERHANG,
          "…and the row leaves it the width the browser's does");
    check(MENU_CHAT_INPUT_WIDTH == 216, "which is 292px of content less two actions and a gap");
  }

  std::printf("the prompt chips are .ctx-assist .chat-suggest, not the dock's:\n");
  {
    ChatMenuPanel panel(nullptr, {}, {}, {}, {}, {});
    panel.restyle(pal);
    panel.resize(panel.minimumWidth(), panel.sizeHint().height());
    panel.show();
    QTest::qWait(80);

    const QList<QPushButton*> chips = chipsOf(&panel);
    check(chips.size() == 4, "all four suggestions are there");
    check(!chips.isEmpty() && chips.first()->font().pixelSize() == CSS_CHIP_FONT_PX,
          "a flyout chip takes the menu scale's 12px");

    // The boxes are the shared sheet's, so read them off it rather than off a private copy.
    check(sheet.contains(QStringLiteral("QWidget#chatMenuPanel QPushButton#chatSuggestChip")),
          "the flyout's chip box is in the shared sheet");
    check(sheet.contains(QStringLiteral("font-size: %1px; padding: %2px %3px;")
                             .arg(CSS_CHIP_FONT_PX)
                             .arg(CSS_CHIP_PAD_Y)
                             .arg(CSS_CHIP_PAD_X)),
          "…and it is the browser's font and padding");
    check(sheet.contains(QStringLiteral("QPushButton#chatSuggestChip { padding: %1px %2px;")
                             .arg(CSS_CHIP_PAD_Y)
                             .arg(CSS_DOCK_CHIP_PAD_X)),
          "the dock's chip keeps its own, wider, box");

    QWidget host;
    QWidget* dockChips = makeSuggestionChips(&host, 6, {});
    styleSuggestionChips(dockChips, pal);
    dockChips->resize(420, 200);
    dockChips->show();
    QTest::qWait(60);
    const QList<QPushButton*> dock = chipsOf(dockChips);
    check(dock.size() == chips.size() && !dock.isEmpty(), "the dock builds the same chips");
    if (!dock.isEmpty() && !chips.isEmpty())
      check(chips.first()->height() < dock.first()->height(),
            "…and the flyout's are the shorter of the two, as in the browser");
  }

  std::printf("the composer keeps the browser's gaps:\n");
  {
    check(MENU_CHAT_ROW_GAP == 6, "the input and the actions sit .ctx-assist-row's 6px apart");
    check(MENU_CHAT_ACTION_GAP == 2, "the action buttons .ctx-assist-actions' 2px");
    check(sheet.contains(QStringLiteral("border-radius: 6px; padding: %1px %2px; font-size: %3px;")
                             .arg(CSS_INPUT_PAD_Y)
                             .arg(CSS_INPUT_PAD_X)
                             .arg(CSS_INPUT_FONT_PX)),
          "and the composer's own padding is #ctx-assist-input's");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
