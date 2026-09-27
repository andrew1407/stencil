// Alt+hover on any selector (support/menu/comboAltPeek, browser twin ui/tip/altPeek.js) over a
// live SearchComboBox and a plain QComboBox: both entry orders, release outside / inside with
// the linger close, the glide between selectors, blur, a disabled combo and a click-open list.
#include "comboAltPeek.hpp"
#include "SearchCombo.hpp"
#include "altPeek.hpp"
#include "uiTimings.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListView>
#include <QTest>

#include "../check.hpp"

using stencil::gui::SearchComboBox;
using stencil::support::comboPopup;

void runAltPeekGestureChecks();

namespace {
  constexpr int POLL_SETTLE_MS = 250;   // the filter polls every 80 ms

  void enter(QWidget* w) {
    const QPointF at(4, 4);
    QEnterEvent ev(at, at, w->mapToGlobal(at));
    QApplication::sendEvent(w, &ev);
  }
  // Where cocoa sends Key_Alt: the open list holds the keys while it is up.
  void altKey(QWidget* host, QEvent::Type t) {
    QWidget* to = QApplication::activePopupWidget() ? QApplication::activePopupWidget() : host;
    QKeyEvent ev(t, Qt::Key_Alt, t == QEvent::KeyPress ? Qt::AltModifier : Qt::NoModifier);
    static quint64 stamp = 1;
    ev.setTimestamp(++stamp);
    QApplication::sendEvent(to, &ev);
  }
  void hold(QWidget* host) { altKey(host, QEvent::KeyPress); }
  void letGo(QWidget* host) { altKey(host, QEvent::KeyRelease); }
  QPoint centreOf(QWidget* w) { return w->mapToGlobal(w->rect().center()); }
  QPoint away(QWidget* host) { return host->mapToGlobal(QPoint(host->width() - 4, host->height() - 4)); }
  SearchComboBox* makeCombo(QWidget* host) {
    auto* c = new SearchComboBox(host, /*searchable=*/false);
    c->addItems({"one", "two", "three"});
    c->setFixedWidth(90);
    host->layout()->addWidget(c);
    return c;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  runAltPeekGestureChecks();

  stencil::support::installComboAltPeek();
  QWidget host;
  auto* row = new QHBoxLayout(&host);
  row->setContentsMargins(8, 8, 8, 300);
  SearchComboBox* a = makeCombo(&host);
  SearchComboBox* b = makeCombo(&host);
  auto* plain = new QComboBox(&host);
  plain->addItems({"x", "y"});
  row->addWidget(plain);
  auto* off = makeCombo(&host);
  off->setEnabled(false);
  row->addStretch(1);
  host.resize(520, 360);
  host.show();
  host.activateWindow();
  (void)QTest::qWaitForWindowExposed(&host);

  QCursor::setPos(away(&host));
  // A real display without Accessibility rights ignores the warp; the live half needs it.
  if (QCursor::pos() != away(&host)) {
    std::printf("cursor cannot be placed on this platform: live checks skipped\n%s\n",
                failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
  }
  hold(&host);
  enter(a);
  check(comboPopup(a) != nullptr, "entering a selector with Alt held opens its list");
  letGo(&host);
  QTest::qWait(POLL_SETTLE_MS);
  check(comboPopup(a) == nullptr, "releasing Alt with the cursor outside the list closes it");

  QTest::qWait(200);   // SearchComboBox ignores a reopen 150 ms after a close
  QCursor::setPos(centreOf(b));
  hold(&host);
  check(comboPopup(b) != nullptr, "pressing Alt while resting on a selector opens its list");
  QWidget* list = comboPopup(b);
  QCursor::setPos(centreOf(list));
  letGo(&host);
  QTest::qWait(POLL_SETTLE_MS);
  check(comboPopup(b) != nullptr, "released with the cursor inside the list, it lingers open");
  QCursor::setPos(away(&host));
  QTest::qWait(POLL_SETTLE_MS + stencil::support::uiTimings().lingerCloseMs);
  check(comboPopup(b) == nullptr, "…and closes once the cursor leaves the list");

  hold(&host);
  QCursor::setPos(centreOf(a));
  enter(a);
  QCursor::setPos(centreOf(b));
  QTest::qWait(POLL_SETTLE_MS);
  check(comboPopup(a) == nullptr && comboPopup(b) != nullptr,
        "gliding with Alt onto another selector moves the peek there");
  QCursor::setPos(centreOf(plain));
  QTest::qWait(POLL_SETTLE_MS);
  check(comboPopup(b) == nullptr && comboPopup(plain) != nullptr,
        "…a plain QComboBox peeks the same way");
  QCursor::setPos(away(&host));   // Fusion lays a plain combo's list over the trigger
  QTest::qWait(POLL_SETTLE_MS);
  check(comboPopup(plain) != nullptr, "leaving the list with Alt still held keeps the peek");
  QEvent blur(QEvent::ApplicationDeactivate);
  QCoreApplication::sendEvent(qApp, &blur);
  QTest::qWait(POLL_SETTLE_MS);
  check(comboPopup(plain) == nullptr, "the app losing focus (Alt+Tab) counts as the release");

  QCursor::setPos(centreOf(off));
  enter(off);
  check(comboPopup(off) == nullptr, "a disabled selector opens nothing");

  QCursor::setPos(centreOf(a));
  enter(a);
  QListView* rows = a->popupList();
  QTest::qWait(50);
  const QRect second = rows->visualRect(rows->model()->index(1, 0));
  QTest::mouseClick(rows->viewport(), Qt::LeftButton, Qt::AltModifier, second.center());
  QTest::qWait(POLL_SETTLE_MS);
  check(a->currentIndex() == 1 && comboPopup(a) == nullptr, "clicking a row in a peek picks it");
  letGo(&host);

  QCursor::setPos(away(&host));
  b->showPopup();
  QTest::qWait(50);
  hold(&host);
  letGo(&host);
  QTest::qWait(POLL_SETTLE_MS);
  check(comboPopup(b) != nullptr, "a click-opened list ignores the Alt release");
  hold(&host);
  QCursor::setPos(centreOf(a));
  QTest::qWait(POLL_SETTLE_MS);
  enter(a);
  check(comboPopup(b) != nullptr && comboPopup(a) == nullptr,
        "…and nothing glides over it or peeks on top of it");
  letGo(&host);
  b->hidePopup();

  std::printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
