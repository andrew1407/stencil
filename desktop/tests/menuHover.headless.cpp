// Hovering a context-menu ROW (support/iconMotion.hpp + support/MenuHotkeys.hpp). A QAction
// has no Enter/Leave, so both are driven off QMenu::hovered and the menu's mouse moves —
// which re-fire for the row already hovered, and reach every menu in the caused stack. The
// browser's twin is a plain `:hover` (css/animations/iconHover.css): the play starts once
// when the pointer arrives, never restarts while it stays, and is cancelled when it goes.
#include "MenuHotkeys.hpp"
#include "iconMotion.hpp"
#include "iconSet.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QLabel>
#include <QMouseEvent>
#include <QTest>
#include <QVariantAnimation>
#include <cstdio>

#include "support/check.hpp"

using namespace stencil::gui;

namespace {

  QVariantAnimation* animOf(QAction* a) {
    ActionIconMotionRunner* r = icm::runnerOfAction(a);
    return r ? r->findChild<QVariantAnimation*>() : nullptr;
  }

  bool running(QAction* a) {
    QVariantAnimation* an = animOf(a);
    return an && an->state() == QAbstractAnimation::Running;
  }

  int frame(QAction* a) {
    QVariantAnimation* an = animOf(a);
    return an ? an->currentTime() : -1;
  }

  void moveTo(QMenu* m, QPoint p) {
    QMouseEvent ev(QEvent::MouseMove, QPointF(p), m->mapToGlobal(p), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
    QApplication::sendEvent(m, &ev);
  }

  void moveInto(QMenu* m, QAction* a, int dx = 0) {
    moveTo(m, m->actionGeometry(a).center() + QPoint(dx, 0));
  }

  QAction* addRow(QMenu* m, const char* glyph, const QString& text, const QString& combo) {
    QAction* a = m->addAction(themedIcon(QString::fromLatin1(glyph), QColor(Qt::black), 18), text);
    if (!combo.isEmpty()) a->setShortcut(QKeySequence(combo));
    return a;
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM
  installIconMotion();

  // `plus` is a SETTLE (it draws itself once and ends at rest); `palette` is one too, so the
  // opener row's own play is visible while its flyout is up.
  QMenu menu;
  QAction* one = addRow(&menu, "plus", QStringLiteral("One"), QStringLiteral("Ctrl+1"));
  QAction* two = addRow(&menu, "trash", QStringLiteral("Two"), QStringLiteral("Ctrl+2"));
  auto* sub = new QMenu(QStringLiteral("Sub"), &menu);
  QAction* opener = menu.addMenu(sub);
  opener->setIcon(themedIcon(QStringLiteral("palette"), QColor(Qt::black), 18));
  QAction* inner = addRow(sub, "plus", QStringLiteral("Inner"), QStringLiteral("Ctrl+3"));

  stencil::support::MenuHotkeyChips chips(&menu);
  menu.popup(QPoint(60, 60));
  QTest::qWait(120);

  const IconMotionSpec* plus = iconMotionFor(QStringLiteral("plus"));
  check(plus && !plus->hold, "`plus` is a settle, so it is the one that can replay");

  std::printf("a row's icon plays once when the pointer arrives:\n");
  {
    moveInto(&menu, one);
    QTest::qWait(40);
    check(running(one), "entering the row starts its play");
    const int at = frame(one);
    moveInto(&menu, one, 4);
    check(running(one) && frame(one) >= at, "moving inside the row does not restart it");
    QTest::qWait(30);
    const int later = frame(one);
    moveInto(&menu, one, 8);
    moveInto(&menu, one, 12);
    check(running(one) && frame(one) >= later, "…however many moves it takes");
  }

  std::printf("…and stops when the pointer leaves it:\n");
  {
    moveInto(&menu, two);
    check(!running(one), "leaving cancels the play rather than letting it finish");
    QTest::qWait(20);
    moveInto(&menu, one);
    QTest::qWait(20);
    check(running(one), "and coming back plays it afresh");
  }

  std::printf("a flyout's row belongs to the flyout, not to the menu behind it:\n");
  {
    moveInto(&menu, opener);
    QTest::qWait(700);
    check(sub->isVisible(), "the submenu is up");
    const int openerFrame = frame(opener);
    moveInto(sub, inner);
    QTest::qWait(40);
    check(running(inner), "the row inside it plays");
    check(frame(opener) >= openerFrame,
          "the opener stays hovered: hovered() reaching the parent is not a new row for it");
    const int at = frame(inner);
    moveInto(sub, inner, 4);
    check(running(inner) && frame(inner) >= at,
          "and a move inside the flyout row does not replay it either");

    moveInto(&menu, opener);   // back out of the flyout onto the row that opened it
    QTest::qWait(20);
    check(!running(opener), "opener and flyout are one hover, so coming back is not a new one");
  }

  menu.close();

  std::printf("the keycap chip shakes on the same terms:\n");
  {
    // Its own menu and chips: a shake animation is built per row on first hover, and only
    // the rows this block hovers may have one.
    QMenu bare;
    QAction* first = addRow(&bare, "plus", QStringLiteral("First"), QStringLiteral("Ctrl+1"));
    QAction* second = addRow(&bare, "trash", QStringLiteral("Second"), QStringLiteral("Ctrl+2"));
    stencil::support::MenuHotkeyChips chips(&bare);
    bare.popup(QPoint(60, 60));
    QTest::qWait(120);

    moveInto(&bare, first);
    QTest::qWait(30);
    QList<QVariantAnimation*> live = chips.findChildren<QVariantAnimation*>();
    check(live.size() == 1 && live.first()->state() == QAbstractAnimation::Running,
          "the row's keycap shakes once when the pointer arrives");
    const int at = live.isEmpty() ? 0 : live.first()->currentTime();
    moveInto(&bare, first, 5);
    moveInto(&bare, first, 9);
    check(!live.isEmpty() && live.first()->currentTime() >= at,
          "moving inside the row does not restart the shake");
    moveInto(&bare, second);
    check(!live.isEmpty() && live.first()->state() != QAbstractAnimation::Running,
          "leaving the row stops it");
    // A bare QMenu owns no QLabel of its own, so every one here is a chip (TipBody is
    // MOC-free, the AppTooltip.hpp idiom, so findChildren cannot name it).
    for (QLabel* label : bare.findChildren<QLabel*>())
      check(static_cast<TipBody*>(label)->capOffset() == 0,
            "…and settles every keycap back to its resting place");
    bare.close();
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
