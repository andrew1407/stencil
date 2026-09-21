#pragma once
// The context-menu chat metrics (browser .ctx-assist parity), private to the ChatMenuPanel*.cpp TUs.
#include <QGuiApplication>
#include <QMargins>
#include <QScreen>

namespace stencil::gui {

  // Context-menu chat metrics (browser .ctx-assist parity): a flyout that is a
  // real chat WINDOW — height min(72vh, 600px), transcript 300..min(60vh, 520px).
  inline constexpr int MENU_CHAT_TOTAL_MAX = 600;
  inline constexpr int MENU_CHAT_TRANSCRIPT_MIN = 300;
  inline constexpr int MENU_CHAT_TRANSCRIPT_MAX = 520;
  inline constexpr int MENU_CHAT_COMPOSER_HEIGHT = 96;  // initial composer slot
  inline constexpr int MENU_CHAT_COMPOSER_MIN = 62;     // splitter floor for it
  // A mirrored row fades while its dust flies, like the dock's cards
  // (ChatDock.cpp CHAT_LEAVE_MS / browser surface/motion.js CHAT_LEAVE_MS).
  inline constexpr int CHAT_ROW_LEAVE_MS = 260;
  inline constexpr int MENU_CHAT_BUTTON_EDGE = 30;      // the dock's action-button box
  inline constexpr int MENU_CHAT_ICON = 20;

  // The flyout's own boxes, from css/components/ctxAssistant.css. The chips and the composer
  // are the chat panel at MENU scale there, and these are the numbers that scales it.
  inline constexpr QMargins MENU_CHAT_PADDING{12, 2, 12, 4};   // .ctx-assist padding
  inline constexpr int MENU_CHAT_ROW_GAP = 6;         // .ctx-assist-row gap
  inline constexpr int MENU_CHAT_ACTION_GAP = 2;      // .ctx-assist-actions gap
  inline constexpr int MENU_CHAT_INPUT_MIN_H = 44;    // #ctx-assist-input: two rows
  inline constexpr int MENU_CHAT_INPUT_WIDTH = 216;   // …and what the row leaves it there
  inline constexpr int MENU_CHAT_CHIP_GAP = 4;        // .ctx-assist .chat-empty gap
  // A per-row "..." hangs 5px outside its bubble, so the transcript is that much wider than the
  // browser's. The composer carries the browser's TWO buttons, so the input measures the same.
  inline constexpr int MENU_CHAT_ROW_MENU_OVERHANG = 5;
  inline constexpr int MENU_CHAT_ACTION_COUNT = 2;
  inline constexpr int MENU_CHAT_WIDTH =
      MENU_CHAT_INPUT_WIDTH + MENU_CHAT_ACTION_COUNT * MENU_CHAT_BUTTON_EDGE
      + (MENU_CHAT_ACTION_COUNT - 1) * MENU_CHAT_ACTION_GAP
      + MENU_CHAT_ROW_GAP + MENU_CHAT_ROW_MENU_OVERHANG + 2 * MENU_CHAT_PADDING.left();

  // The transcript takes what the composer leaves inside that window, floored and capped as above -
  // always bounded by the screen so the flyout can never overflow a short display.
  inline int menuChatTranscriptHeight() {
    const QScreen* screen = QGuiApplication::primaryScreen();
    const int avail = screen ? screen->availableGeometry().height() : 0;
    const int total = avail > 0 ? qMin(int(avail * 0.72), MENU_CHAT_TOTAL_MAX)
                                : MENU_CHAT_TOTAL_MAX;
    const int cap = avail > 0 ? qMin(int(avail * 0.60), MENU_CHAT_TRANSCRIPT_MAX)
                              : MENU_CHAT_TRANSCRIPT_MAX;
    const int floor = qMin(MENU_CHAT_TRANSCRIPT_MIN, cap);
    return qMax(140, qBound(floor, total - MENU_CHAT_COMPOSER_HEIGHT, cap));
  }

}  // namespace stencil::gui
