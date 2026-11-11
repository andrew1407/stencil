// Headless checks for support/share/menuDoubleClick.hpp (browser twin:
// core/pointer/clickOrDouble.js): one release on a two-meaning row runs `single` after the double-click interval,
// a second release inside it runs `dbl` once instead, Enter runs `single` at once, every case
// closes the menu first, and a plain row still triggers as Qt's own.
#include "../../../src/support/share/menuDoubleClick.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QTest>

#include "../check.hpp"

using namespace stencil::support;

namespace {
  // A whole click: Qt's own row triggers only on a release its press began.
  void releaseOn(QMenu& menu, QAction* a) {
    const QPointF at = menu.actionGeometry(a).center();
    QMouseEvent press(QEvent::MouseButtonPress, at, menu.mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&menu, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, menu.mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&menu, &release);
  }
  void settle() { QTest::qWait(QApplication::doubleClickInterval() + 120); }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QStringList ran;

  {
    QMenu menu;
    auto* twice = new MenuDoubleClick(&menu);
    QAction* link = menu.addAction(QStringLiteral("Project link"));
    twice->add(link, [&] { ran << "share"; }, [&] { ran << "copy"; });
    menu.popup(QPoint(100, 100));
    QTest::qWaitForWindowExposed(&menu);
    releaseOn(menu, link);
    QApplication::processEvents();
    check(ran.isEmpty() && menu.isVisible(), "one release runs nothing inside the interval, the menu still up");
    settle();
    check(ran == QStringList{"share"} && !menu.isVisible(), "past it the single runs, the menu closed first");
  }

  ran.clear();
  {
    QMenu menu;
    auto* twice = new MenuDoubleClick(&menu);
    QAction* link = menu.addAction(QStringLiteral("Project link"));
    twice->add(link, [&] { ran << "share"; }, [&] { ran << "copy"; });
    menu.popup(QPoint(100, 100));
    QTest::qWaitForWindowExposed(&menu);
    releaseOn(menu, link);
    releaseOn(menu, link);
    settle();
    check(ran == QStringList{"copy"}, "a second release inside the interval runs the double, once");
  }

  ran.clear();
  {
    QMenu menu;
    auto* twice = new MenuDoubleClick(&menu);
    QAction* link = menu.addAction(QStringLiteral("Project link"));
    QAction* plain = menu.addAction(QStringLiteral("Other"));
    twice->add(link, [&] { ran << "share"; }, [&] { ran << "copy"; });
    QObject::connect(plain, &QAction::triggered, [&] { ran << "plain"; });
    link->trigger();
    QApplication::processEvents();
    check(ran == QStringList{"share"}, "a key (the action's own trigger) runs the single at once");
    menu.popup(QPoint(100, 100));
    QTest::qWaitForWindowExposed(&menu);
    releaseOn(menu, plain);
    QApplication::processEvents();
    check(ran == QStringList({"share", "plain"}), "a row without a double still triggers as Qt's own");
  }

  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
