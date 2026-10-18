// The console text speed and what it paces (support/console): the range out of motion.json, the
// clamp, the reveal windows scaled off the speed, the typewriter stepping over a fake clock, the
// prompt line's history and Tab rules, and the setting's round trip. Browser twins:
// tests/ui/console/reveal.test.js, tests/ui/console/input.test.js, tests/ui/visuals/motionPrefs.test.js.
#include "fileStore.hpp"
#include "../../../src/support/console/PromptLine.hpp"
#include "../../../src/support/console/consoleGrammar.hpp"
#include "../../../src/support/console/TypewriterReveal.hpp"
#include "../../../src/support/console/consolePace.hpp"

#include <QGuiApplication>
#include <QJsonObject>
#include <QRegularExpression>
#include <cstdio>
#include <limits>

#include "../../support/check.hpp"

using namespace stencil;
using namespace stencil::support;
using stencil::gui::Settings;

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  qunsetenv("STENCIL_NO_ANIM");   // ctest sets it; the tuned clocks are checked with motion on

  std::printf("the speed:\n");
  {
    const ConsoleSpeedRange& r = consoleSpeedRange();
    check(r.min == 0.01 && r.max == 1.0 && r.def == 0.25 && r.step == 0.01, "the range is motion.json's consoleSpeed (0.01–1, default 0.25, step 0.01)");
    check(Settings().consoleSpeed == r.def, "the settings default is the table's");
    check(clampConsoleSpeed(0.001) == 0.01 && clampConsoleSpeed(4.0) == 1.0, "out of range clamps to the ends");
    check(clampConsoleSpeed(std::numeric_limits<double>::quiet_NaN()) == 0.25, "a non-number is the default");
    check(clampConsoleSpeed(0.456) == 0.46, "kept to hundredths");
    setConsoleSpeed(9.0);
    check(consoleSpeed() == 1.0, "the setter clamps");
    setConsoleSpeed(r.def);
    Settings s;
    s.consoleSpeed = 0.8;
    check(gui::fileStore::settingsFromJson(gui::fileStore::settingsToJson(s)).consoleSpeed == 0.8, "the speed round-trips");
    check(gui::fileStore::settingsFromJson(QJsonObject()).consoleSpeed == r.def, "an absent key is the default");
    QJsonObject old;
    old["consoleSpeed"] = 0.5;
    check(gui::fileStore::settingsFromJson(old).consoleSpeed == 0.5, "a stored speed is the user's, 0.5 included");
  }

  std::printf("the pace:\n");
  {
    const RevealFx& fx = revealFx();
    check(fx.stepMs == 13 && fx.jumps == 8 && fx.burstJumps == 2 && fx.quietMs == 250 && fx.burstMaxMs == 900 && fx.ceilingMs == 5000,
          "console/fx.json reveal is read");
    const RevealPace def = revealPace(0.5);
    check(def.stepMs == 13 && def.quietMs == 250 && def.burstMaxMs == 900 && !def.instant, "the default speed plays the clocks as tuned");
    qputenv("STENCIL_NO_ANIM", "1");
    check(revealPace(0.5).instant, "reduced motion lands every line whole");
    qunsetenv("STENCIL_NO_ANIM");
    const RevealPace slow = revealPace(0.1);
    check(slow.stepMs == 117 && slow.quietMs == 2250 && slow.burstMaxMs == 5000, "a slow speed scales by (1 − s) / s, capped at the ceiling");
    const RevealPace quick = revealPace(0.9);
    check(quick.stepMs == 1 && quick.quietMs == 250 && quick.burstMaxMs == 900, "a quick speed never drops under the floors");
    check(revealPace(1.0).instant, "the fastest speed is instant");
    RevealTracker t;
    RevealPlan p = t.plan(1000, 0.5);
    check(p.jumps == 8 && p.stepMs == 13, "the first line sweeps in eight jumps");
    p = t.plan(1100, 0.5);
    check(p.jumps == 2, "a line within the quiet window is a burst");
    for (qint64 at = 1300; at <= 2100; at += 200) p = t.plan(at, 0.5);
    check(p.jumps == 0, "past burstMaxMs of pouring it lands at once");
    p = t.plan(5000, 0.5);
    check(p.jumps == 8, "a pause starts the long form again");
    check(t.plan(5100, 1.0).jumps == 0, "instant at the fastest speed");
  }

  std::printf("the typewriter:\n");
  {
    TypewriterReveal tw;
    qint64 now = 10000;
    tw.setClock([&now] { return now; });
    tw.setSpeed([] { return 0.5; });
    int changes = 0;
    QObject::connect(&tw, &TypewriterReveal::changed, [&changes] { ++changes; });
    tw.enqueue(QStringList{QStringLiteral("abcdefghijklmnop"), QStringLiteral("second")}, ConsoleKind::PLAIN);
    check(tw.count() == 2 && tw.visible(0).isEmpty() && tw.visible(1).isEmpty() && !tw.settled(), "lines land blank and wait their turn");
    tw.step();
    check(tw.visible(0) == "ab" && tw.visible(1).isEmpty(), "the first jump cuts the first row alone: rows sweep one after another (ui.zig emit, reveal.zig revealNew)");
    for (int i = 0; i < 6; ++i) tw.step();
    check(tw.visible(0) == "abcdefghijklmn" && tw.visible(1).isEmpty() && !tw.settled(), "six more jumps show fourteen");
    tw.step();
    check(tw.visible(0) == "abcdefghijklmnop" && tw.visible(1).isEmpty() && !tw.settled(), "the eighth jump settles the first row");
    tw.step();
    check(tw.visible(1) == "sec", "the next row pours right after it, in the burst form");
    tw.step();
    check(tw.visible(1) == "second" && tw.settled(), "and settles on its second jump");
    now += 20;
    tw.enqueue(QStringList{QStringLiteral("third")}, ConsoleKind::PLAIN);
    tw.step();
    check(tw.visible(2) == "thi" && !tw.settled(), "the next print within the quiet window arrives in the burst form: two jumps, three chars each");
    tw.step();
    check(tw.settled() && tw.visible(2) == "third", "and settles on its second");
    tw.enqueue(QStringList{QString()}, ConsoleKind::MUTED);
    check(tw.settled() && tw.at(3).kind == ConsoleKind::MUTED, "an empty line is settled at once and keeps its kind");
    now += 10000;
    tw.enqueue(QStringList{QStringLiteral("/status")}, ConsoleKind::ECHO);
    check(tw.settled() && tw.visible(4) == "/status", "a command's echo lands whole");
    now += 20;
    tw.enqueue(QStringList{QStringLiteral("abcdefghijklmnop")}, ConsoleKind::PLAIN);
    tw.step();
    check(!tw.settled() && tw.visible(5) == "ab", "the reply right after the echo still sweeps in the long form, not a burst's");
    tw.settleNow();
    tw.settleNow();
    now += 10000;
    tw.setSpeed([] { return 1.0; });
    tw.enqueue(QStringList{QStringLiteral("instant")}, ConsoleKind::ECHO);
    tw.step();
    check(tw.settled() && tw.visible(6) == "instant", "at the fastest speed a line lands whole");
    check(changes >= 10, "every step told the view");
    QStringList many;
    for (int i = 0; i < TypewriterReveal::MAX_LINES + 5; ++i) many << QString::number(i);
    int trims = 0;
    QObject::connect(&tw, &TypewriterReveal::trimmed, [&trims] { ++trims; });
    tw.enqueue(many, ConsoleKind::PLAIN);
    check(tw.count() == TypewriterReveal::MAX_LINES && trims == 1 && tw.at(tw.count() - 1).text == QString::number(TypewriterReveal::MAX_LINES + 4),
          "the scrollback keeps the last 2000 lines and says it trimmed");
    tw.clear();
    check(tw.count() == 0 && trims == 2, "clear empties it");
  }

  std::printf("the prompt line:\n");
  {
    PromptLine line;
    line.insert("roate");
    line.left();
    line.left();
    line.left();
    line.insert("t");
    check(line.text() == "rotate" && line.caret() == 3, "insert at the caret");
    line.end();
    line.backspace();
    line.insert("e 1");
    check(line.take() == "rotate 1" && line.text().isEmpty(), "take hands the trimmed line back and clears");
    line.insert("flip");
    line.take();
    line.historyUp();
    check(line.text() == "flip", "Up recalls the last line");
    line.historyUp();
    check(line.text() == "rotate 1", "…and the one before");
    line.historyDown();
    line.historyDown();
    check(line.text().isEmpty(), "Down past the newest is an empty line");
    const auto names = [](const QString& prefix) { return QStringList{"color", "connect", "connections"}.filter(QRegularExpression("^" + prefix)); };
    QStringList listed;
    line.setText("/co");
    line.completeWith(names, [&listed](const QStringList& n) { listed = n; });
    check(line.text() == "/co" && listed == QStringList{"color", "connect", "connections"}, "an ambiguous Tab changes nothing and lists the options (line_edit/editing.zig)");
    line.setText("/conn");
    line.completeWith(names);
    check(line.text() == "/connect", "several extend to their common prefix, the slash kept");
    line.setText("connecti");
    line.completeWith(names);
    check(line.text() == "connections ", "one match fills in with a space after, no slash added");
    line.setText("/color x");
    line.completeWith(names);
    check(line.text() == "/color x", "only the first word completes");
    check(line.take() == "/color x" && line.history().size() == 3, "the history holds every line taken");
    line.setText("/crop 10 20 to end");
    line.wordLeft();
    check(line.caret() == 15, "a word left from the end: the start of 'end' (line_edit/words.zig)");
    line.wordLeft();
    line.wordLeft();
    check(line.caret() == 9, "two more: the start of '20'");
    line.wordRight();
    check(line.caret() == 11, "a word right crosses the blanks, then the word");
    line.deleteWordBack();
    check(line.text() == "/crop 10  to end" && line.caret() == 9, "delete the word before the caret");
    line.deleteWordForward();
    check(line.text() == "/crop 10  end" && line.caret() == 9, "delete the word after it, its blanks included");
    line.killToEnd();
    check(line.text() == "/crop 10 ", "kill to the end");
    line.end();
    line.killToStart();
    check(line.text().isEmpty() && line.caret() == 0, "kill to the start");
    line.insert("/color x");
    line.take();
    check(line.history().size() == 3, "a consecutive repeat is kept once");
    line.setText("/llm key abc123");
    check(line.take(consoleHistoryText) == "/llm key abc123" && line.history().last() == "/llm key", "a typed key is run whole and remembered bare");
    line.insert("typing");
    line.historyUp();
    line.historyDown();
    check(line.text() == "typing", "past the newest entry the typed line comes back");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
