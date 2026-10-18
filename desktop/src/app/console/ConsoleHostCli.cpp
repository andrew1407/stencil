#include "ConsoleHost.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "CliStage.hpp"
#include "DockChrome.hpp"
#include "LogoHoverFx.hpp"
#include "ConsoleWindow.hpp"
#include "Notifications.hpp"
#include "ToastStack.hpp"
#include "ChatPlanTarget.hpp"
#include "consoleCommands.hpp"
#include "consoleGrammar.hpp"
#include "theme.hpp"
#include "hoverResync.hpp"
#include "logoStageRules.hpp"
#include "../../support/motion/toastShine.hpp"
#include "../../support/skinPrefs.hpp"
#include "../../support/console/TypewriterReveal.hpp"

#include <QTimer>

// CLI mode: the stage over the whole window, entered once everything else has closed. Browser
// twin js/ui/cli/mode.js; the gate is js/console/shell/gate.js.

namespace stencil::gui {

  namespace {
    constexpr int WAY_POLL_MS = 16;
    constexpr int WAY_TRIES_MAX = 60;   // a second of closing flights, then the stage opens anyway
  }

  void ConsoleHost::buildStage() {
    CliStage::Hooks h;
    h.accent = [this] {
      const QColor a = accentPrimary(w.settings.accentColor);
      return a.isValid() ? a : QColor(DEFAULT_ACCENT_HEX);
    };
    h.accentKey = [this] { return w.settings.accentColor; };
    h.dark = [this] { return w.parts.theme.paintingDark(); };
    h.cycleAccent = [this] { w.parts.theme.cycleAccentPreset(); };
    h.run = [this](const QString& line, support::ConsoleDone done) {
      ConsoleContext ctx{*this, true};
      runConsoleLine(ctx, line, std::move(done));
    };
    h.completions = [](const QString& prefix) { return support::consoleCompletions(prefix, true); };
    h.status = [this] {
      const Status s = status();
      return CliStatus{s.text, s.name, s.color};
    };
    h.editingPicture = [this] { return w.canvas->hasImage() ? w.canvas->renderToImage(true) : QImage(); };
    // The export's original: the picture as cropped and rotated, without the filter and the lines (browser 'original').
    h.originalPicture = [this] { return w.canvas->hasImage() ? w.canvas->renderToImage(QStringLiteral("original")) : QImage(); };
    h.speed = [this] { return speed(); };
    h.leave = [this] { leaveCli(); };
    h.customAccent = [this](const QString& hex) { ChatPlanTarget(w).setAccent(hex); };
    h.previewAccent = [this](const QString& hex) { w.parts.theme.previewAccent(hex); };
    h.endAccentPreview = [this] { w.parts.theme.endAccentPreview(); };
    h.holdMs = support::cliModeConfig().holdMs;
    stage = new CliStage(&w, std::move(h));
  }

  void ConsoleHost::enterCli() {
    if (cliModeOn()) return;
    closeEverything();
    if (!wayPoll) {
      wayPoll = new QTimer(&w);
      wayPoll->setSingleShot(true);
      QObject::connect(wayPoll, &QTimer::timeout, &w, [this] {
        if (!windowBare() && wayTries++ < WAY_TRIES_MAX) {
          wayPoll->start(WAY_POLL_MS);
          return;
        }
        if (!stage) buildStage();
        coverChrome(true);
        stage->enter();
        if (!w.notify) return;
        w.notify->show(support::logoStageConfig().toast, Notifications::Level::SUCCESS, 3000, /*special=*/true);
        support::installToastShine(w.notify->toasts()->lastToast());
      });
    }
    wayTries = 0;
    wayPoll->start(0);
  }

  void ConsoleHost::leaveCli() {
    if (wayPoll) wayPoll->stop();
    if (!stage || !stage->isOpen()) return;
    stage->leave();
    coverChrome(false);
    support::resyncHover(&w);
  }

  bool ConsoleHost::cliModeOn() const { return stage && stage->isOpen(); }
  CliStage* ConsoleHost::cliStage() const { return stage.data(); }

  std::optional<QString> ConsoleHost::cliGate(const QString& what) const {
    if (!cliModeOn()) return std::nullopt;
    return QStringLiteral("CLI mode is on — %1 waits until /%2").arg(what, support::cliModeConfig().offCommand);
  }

  // What a logo show does to the chrome it covers (ToolbarBuilderHeader coverChrome): the header
  // mark's overlay and the dock edges re-raise themselves on layout passes, so they stand down.
  void ConsoleHost::coverChrome(bool on) {
    w.showCovered = on;
    if (w.tools.logoFx) asLogoFx(w.tools.logoFx)->standDown(on);
    w.parts.dockChrome.positionPanelGrip();
    w.parts.dockChrome.positionChatEdge();
  }

  void ConsoleHost::rebuildWindowForSkin() {
    const bool wasOpen = window->isVisible();
    const QRect geometry = window->geometry();
    const QVector<support::ConsoleLine> kept = window->scrollbackLines();
    delete window.data();
    buildWindow();
    window->clearOutput();
    window->print(kept);
    window->revealModel().settleNow();
    window->setProperty("placed", true);
    window->setGeometry(geometry);
    if (wasOpen) {
      window->show();
      window->focusPrompt();
    }
  }

  void ConsoleHost::onThemeApplied() {
    if (window && window->builtForWebcore() != support::isWebcore()) rebuildWindowForSkin();
    else if (window) window->repaintAll();
    if (stage && stage->isOpen()) stage->onThemeChanged();
  }

  void ConsoleHost::onCanvasChanged() {
    if (stage && stage->isOpen()) stage->refreshStatus();
  }

}  // namespace stencil::gui
