#pragma once
// The script flyout's metrics (browser css/components/ctxScript.css), private to the
// ScriptMenuPanel*.cpp TUs.
#include <QGuiApplication>
#include <QScreen>

namespace stencil::gui {

  // A real editor WINDOW at menu scale: 372px wide — a directive with its arguments on one
  // line, and the four actions in one row — and min(58vh, 440px) tall, with the editor
  // flexing into whatever the diagnostics strip and the action row leave over.
  inline constexpr int MENU_SCRIPT_WIDTH = 372;
  inline constexpr int MENU_SCRIPT_MAX_HEIGHT = 440;
  inline constexpr double MENU_SCRIPT_SCREEN_SHARE = 0.58;
  inline constexpr int MENU_SCRIPT_EDITOR_MIN = 150;
  inline constexpr int MENU_SCRIPT_FONT_PX = 12;
  inline constexpr int MENU_SCRIPT_LINE_HEIGHT_PCT = 155;   // browser line-height 1.55
  inline constexpr int MENU_SCRIPT_PAD_X = 8;               // the flyout's tighter gutter
  inline constexpr int MENU_SCRIPT_PAD_Y = 6;
  inline constexpr int MENU_SCRIPT_ICON = 13;
  inline constexpr int MENU_SCRIPT_INDENT = 2;              // Tab inserts two spaces

  inline int menuScriptHeight() {
    const QScreen* screen = QGuiApplication::primaryScreen();
    const int avail = screen ? screen->availableGeometry().height() : 0;
    return avail > 0 ? qMin(int(avail * MENU_SCRIPT_SCREEN_SHARE), MENU_SCRIPT_MAX_HEIGHT)
                     : MENU_SCRIPT_MAX_HEIGHT;
  }

}  // namespace stencil::gui
