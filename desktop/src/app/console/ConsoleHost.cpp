#include "ConsoleHost.hpp"
#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "popupGuards.hpp"
#include "ConsoleWindow.hpp"
#include "CliSkins.hpp"
#include "CliStage.hpp"
#include "LogoStage.hpp"
#include "consoleCommands.hpp"
#include "consoleGrammar.hpp"
#include "theme.hpp"
#include "modalReveal.hpp"
#include "hoverResync.hpp"
#include "../../support/console/consolePace.hpp"
#include "../../support/skinPrefs.hpp"

#include <QApplication>
#include <QDialog>
#include <QTimer>

// The console window and the way the shows and CLI mode clear; browser twin
// js/ui/console/modal.js and js/ui/logo/stageTrigger.js clearWay.

namespace stencil::gui {

  void ConsoleHost::buildWindow() {
    ConsoleWindow::Hooks h;
    h.run = [this](const QString& line, support::ConsoleDone done) {
      ConsoleContext ctx{*this, false};
      runConsoleLine(ctx, line, std::move(done));
    };
    h.completions = [](const QString& prefix) { return support::consoleCompletions(prefix, false); };
    h.speed = [this] { return speed(); };
    h.accent = [this] {
      const QColor a = accentPrimary(w.settings.accentColor);
      return a.isValid() ? a : QColor(DEFAULT_ACCENT_HEX);
    };
    h.danger = [this] { return themePalette(w.parts.theme.paintingDark(), w.settings.accentColor).danger; };
    h.gold = [] { return cliGold(); };
    // The field's ink is the theme's text, the shell's light grey on its black field under webcore.
    h.ink = [this] { return support::isWebcore() ? QColor(0xc0, 0xc0, 0xc0) : themePalette(w.parts.theme.paintingDark(), w.settings.accentColor).textMain; };
    h.muted = [this] { return support::isWebcore() ? QColor(0x80, 0x80, 0x80) : themePalette(w.parts.theme.paintingDark(), w.settings.accentColor).textMuted; };
    window = new ConsoleWindow(&w, std::move(h));
    QObject::connect(window.data(), &ConsoleWindow::closing, &w, [this] {
      support::dismissWindow(*window, w.buttonForAction(w.acts.console));
      support::resyncHover(&w);
    });
  }

  void ConsoleHost::openConsole() {
    if (cliModeOn()) return;   // the stage is the console while it is up
    if (!window) buildWindow();
    if (window->isVisible()) {   // the icon toggles, as the browser's consoleToggle does
      closeConsole();
      return;
    }
    // Its first open lands over the window's centre; later ones where it was left.
    if (!window->property("placed").toBool()) {
      window->setProperty("placed", true);
      window->move(w.geometry().center() - window->rect().center());
    }
    support::veilForReveal(*window);
    window->show();
    support::revealWindow(*window, w.buttonForAction(w.acts.console));
    window->focusPrompt();
    // A non-modal window is outside the hover resync filter: re-arm its cursor shapes once mapped.
    QTimer::singleShot(0, window.data(), [win = window] { if (win) support::resyncHover(win); });
  }

  void ConsoleHost::closeConsole() {
    if (window && window->isVisible()) window->reject();
  }

  bool ConsoleHost::consoleOpen() const { return window && window->isVisible(); }
  ConsoleWindow* ConsoleHost::consoleWindow() const { return window.data(); }

  void ConsoleHost::clearWay() {
    if (QWidget* menu = QApplication::activePopupWidget()) menu->close();
    QWidget* modal = QApplication::activeModalWidget();
    if (w.pop.active) w.dismissPopover();
    else if (auto* dlg = qobject_cast<QDialog*>(modal)) dlg->reject();
    else if (modal) modal->close();
    if (w.fs.active) w.parts.view.toggleFullscreen();
  }

  void ConsoleHost::closeEverything() {
    clearWay();
    for (const QPointer<QDialog>& d : w.pop.windows)
      if (d && d->isVisible()) d->reject();
    if (w.acts.chat && w.acts.chat->isChecked()) w.acts.chat->setChecked(false);
    closeOpenPopupMenus();
    closeConsole();
    if (auto* show = w.findChild<LogoStage*>(QStringLiteral("logoStage"))) show->dismiss();
  }

  bool ConsoleHost::windowBare() const {
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return false;
    if (w.pop.active || w.fs.active || consoleOpen()) return false;
    for (const QPointer<QDialog>& d : w.pop.windows)
      if (d && d->isVisible()) return false;
    return true;
  }

  double ConsoleHost::speed() const { return support::consoleSpeed(); }

  // `/reveal-speed [speed]` as appearance.zig answers it: shown or set, `off` instant, `on` the default.
  QStringList ConsoleHost::revealSpeed(const QString& arg, QString* err) {
    const support::ConsoleSpeedRange& r = support::consoleSpeedRange();
    const auto line = [&r](double speed, bool showing) {
      const QString verb = showing ? QStringLiteral("reveal speed is") : QStringLiteral("reveal speed");
      return speed >= r.max ? QStringLiteral("%1 %2 — output appears instantly").arg(verb).arg(speed) : QStringLiteral("%1 %2").arg(verb).arg(speed);
    };
    const QString word = arg.trimmed().toLower();
    if (word.isEmpty()) return {line(support::consoleSpeed(), true)};
    bool ok = true;
    const double want = word == QLatin1String("off") ? r.max : word == QLatin1String("on") ? r.def : word.toDouble(&ok);
    if (!ok || want < r.min || want > r.max) {
      *err = QStringLiteral("usage: /reveal-speed [%1 … %2] — %1 slowest, %2 instant (default %3)").arg(r.min).arg(r.max).arg(r.def);
      return {};
    }
    support::setConsoleSpeed(want);
    w.settings.consoleSpeed = want;
    SharedState::instance().saveSettings(w.settings, &w);
    return {line(want, false)};
  }


}  // namespace stencil::gui
