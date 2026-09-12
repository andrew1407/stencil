#pragma once
// The context-menu chat metrics (browser .ctx-assist parity), private to the chatMenuPanel*.cpp TUs.
#include <QGuiApplication>
#include <QScreen>

namespace stencil::gui {

  // Context-menu chat metrics (browser .ctx-assist parity): a flyout that is a
  // real chat WINDOW — height min(72vh, 600px), transcript 300..min(60vh, 520px).
  // 325, not 320: 5px of room for the per-row "…" that hangs outside the bubble.
  inline constexpr int MENU_CHAT_WIDTH = 325;
  inline constexpr int MENU_CHAT_TOTAL_MAX = 600;
  inline constexpr int MENU_CHAT_TRANSCRIPT_MIN = 300;
  inline constexpr int MENU_CHAT_TRANSCRIPT_MAX = 520;
  inline constexpr int MENU_CHAT_COMPOSER_HEIGHT = 96;  // initial composer slot
  inline constexpr int MENU_CHAT_COMPOSER_MIN = 62;     // splitter floor for it
  // A mirrored row fades while its dust flies, like the dock's cards
  // (chatDock.cpp CHAT_LEAVE_MS / browser motion.js CHAT_LEAVE_MS).
  inline constexpr int CHAT_ROW_LEAVE_MS = 260;
  inline constexpr int MENU_CHAT_BUTTON_EDGE = 30;      // the dock's action-button box
  inline constexpr int MENU_CHAT_ICON = 20;

  // The transcript takes what the composer leaves inside that window, floored
  // and capped as above — always bounded by the screen so the flyout can never
  // overflow a short display.
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
