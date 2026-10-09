#pragma once
// The Assistant flyout suites' common ground: the window with a working image, its assistant
// answered by a mock with one sepia plan, and the flyout's parts once it is open. The window's
// private members are passed in by the suite, the window's friend.
#include "../../MainWindowMenu.gui.hpp"
#include <QPlainTextEdit>

namespace stencil::guitest {

  // Shown at the flyout suites' size, holding the working image a plan has something to hit.
  inline bool showWithWorkingImage(MainWindow& win) {
    win.resize(1200, 800);
    win.show();
    if (!QTest::qWaitForWindowExposed(&win)) return false;
    win.openPathFromOS(guiTestImage());
    return QTest::qWaitFor([&win] { return win.findChild<CanvasWidget*>()->hasImage(); }, 5000);
  }

  // The assistant ON, on ollama, answered synchronously by `mock` (the seam LlmClient.headless.cpp
  // uses) with a real op plan in ollama's shape: built through QJsonDocument, as moc chokes on a
  // raw string literal.
  inline void armSepiaAssistant(gui::Settings& settings, std::unique_ptr<llm::LlmClient>& client,
                                MockChatTransport& mock) {
    settings.llmProvider = "ollama";
    settings.llmBaseUrl = "http://localhost:11434";
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Sepia applied\","
                      "\"actions\":[{\"op\":\"filter\",\"mode\":\"sepia\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    client = std::make_unique<llm::LlmClient>(&mock);
  }

  // The canvas menu with its Assistant flyout open, and the flyout's composer parts.
  struct AssistantFlyout {
    QMenu* menu = nullptr;
    QMenu* sub = nullptr;
    QWidget* panel = nullptr;
    QPlainTextEdit* input = nullptr;
    QToolButton* send = nullptr;
    QToolButton* more = nullptr;
    bool complete() const { return panel && input && send && more; }
  };

  // Opened from a tick of the menu's own loop; an incomplete one has closed the menu already.
  inline AssistantFlyout openAssistantFlyout() {
    AssistantFlyout f;
    f.menu = findMenu();
    if (!f.menu) return f;
    f.sub = openSub(f.menu, "Assistant");
    if (f.sub) {
      f.panel = f.sub->findChild<QWidget*>("chatMenuPanel");
      f.input = f.sub->findChild<QPlainTextEdit*>("chatMenuInput");
      f.send = f.sub->findChild<QToolButton*>("chatMenuSend");
      f.more = f.sub->findChild<QToolButton*>("chatMenuMore");
    }
    if (!f.complete()) f.menu->close();
    return f;
  }

}  // namespace stencil::guitest
