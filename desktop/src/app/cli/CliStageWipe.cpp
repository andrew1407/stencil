#include "CliStage.hpp"
#include "CliPeek.hpp"
#include "../../support/console/TypewriterReveal.hpp"
#include "../../support/console/consolePace.hpp"
#include "../../support/motionPrefs.hpp"

#include <QTimer>

// The accent recolour wipe (cli/src/console/render/logoFx/press.zig wipeRecolor): the new accent
// jumps left to right over the banner and the echoes while the wordmark lights letter by letter
// and the icon turns as a clock; the rule runs ahead. A keystroke settles it at once.

namespace stencil::gui {

  bool CliStage::wipeRunning() const { return wipe->isActive(); }

  // The rightmost accented column: the banner's width or the longest echo in the scrollback.
  int CliStage::wipeReachNow() const {
    int reach = cliFx().iconCols;
    for (const QVector<BannerCell>& row : bannerCells(worn, false, nullptr)) reach = qMax(reach, int(row.size()));
    for (int i = 0; i < reveal->count(); ++i) {   // every run in the accent: a head, a section (press.zig accentReach)
      const support::TypewriterReveal::Line& l = reveal->at(i);
      reach = qMax(reach, l.kind == support::ConsoleKind::SECTION ? int(l.text.size()) : l.head);
    }
    return reach;
  }

  void CliStage::startWipe(const QColor& to) {
    settleWipe();
    if (cliRestyles(worn)) {   // a restyled skin wears no accent of its own: the new one lands at once
      paintedAccent = to;
      update();
      return;
    }
    wipeFrom = paintedAccent;
    wipeTo = to;
    wipeReach = wipeReachNow();
    int letters = 0;
    for (const QChar ch : cliWordmark(worn))
      if (ch != QLatin1Char(' ')) ++letters;
    // The wipe runs at the Zig console's own tempo (press.zig); speed 1 lands it at once.
    const double s = support::clampConsoleSpeed(hooks.speed ? hooks.speed() : support::consoleSpeed());
    wipeEvents = cliwipe::wipeSchedule(wipeReach, letters);
    wipeIndex = 0;
    wipeX = 0;
    litLetters = -1;
    if (support::motionReduced() || s >= support::consoleSpeedRange().max || wipeEvents.isEmpty()) {
      paintedAccent = to;
      update();
      return;
    }
    wipe->start(wipeEvents.first().atMs);
  }

  void CliStage::wipeStep() {
    if (wipeIndex >= wipeEvents.size()) return settleWipe();
    const cliwipe::Event e = wipeEvents.at(wipeIndex++);
    if (e.wash) wipeX = e.value;
    else litLetters = e.value;
    update();
    if (wipeIndex >= wipeEvents.size()) return settleWipe();
    wipe->start(qMax(1, wipeEvents.at(wipeIndex).atMs - e.atMs));
  }

  void CliStage::settleWipe() {
    if (!wipe->isActive() && wipeEvents.isEmpty()) return;
    wipe->stop();
    wipeEvents.clear();
    if (wipeTo.isValid()) paintedAccent = wipeTo;
    wipeTo = QColor();
    wipeX = 0;
    litLetters = -1;
    update();
  }

  void CliStage::onThemeChanged() {
    const QColor now = hooks.accent ? hooks.accent() : paintedAccent;
    if (now != paintedAccent && now != wipeTo) startWipe(now);
    else update();
  }

  void CliStage::refreshStatus() {
    peekView->refresh();
    update();
  }

}  // namespace stencil::gui
