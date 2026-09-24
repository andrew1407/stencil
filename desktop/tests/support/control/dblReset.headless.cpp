// Headless checks for support/control/dblReset.hpp (browser twin js/ui/control/dblReset.js): a
// double-click puts a combo or a check back to its default through the signals a pick fires,
// a caption resets its box, and a control that declared no default is left alone.
#include "../../../src/support/control/dblReset.hpp"
#include "../../../src/support/control/clickToToggle.hpp"

#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>

#include "../check.hpp"

namespace {
  void send(QWidget* w, QEvent::Type t) {
    const QPointF at(4, 4);
    QMouseEvent ev(t, at, w->mapToGlobal(at), Qt::LeftButton,
                   t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
  }
  // What the platform delivers for a double-click.
  void doubleClick(QWidget* w) {
    send(w, QEvent::MouseButtonPress);
    send(w, QEvent::MouseButtonRelease);
    send(w, QEvent::MouseButtonDblClick);
    send(w, QEvent::MouseButtonRelease);
    QApplication::processEvents();
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  stencil::support::installDblReset();
  QWidget host;
  host.show();

  auto* combo = new QComboBox(&host);
  for (const char* v : {"solid", "dashed", "dotted"}) combo->addItem(v, QString::fromLatin1(v));
  combo->setCurrentIndex(2);
  stencil::support::setResetDefault(combo, QStringLiteral("solid"));
  int activated = -1;
  QObject::connect(combo, &QComboBox::activated, [&](int i) { activated = i; });
  send(combo, QEvent::MouseButtonPress);
  combo->hidePopup();
  send(combo, QEvent::MouseButtonPress);
  QApplication::processEvents();
  check(combo->currentData().toString() == "solid", "two quick presses put the combo back to its default");
  check(activated == 0, "…through activated, the route a pick takes");

  auto* bare = new QComboBox(&host);
  bare->addItems({"a", "b"});
  bare->setCurrentIndex(1);
  send(bare, QEvent::MouseButtonPress);
  bare->hidePopup();
  send(bare, QEvent::MouseButtonPress);
  bare->hidePopup();
  check(bare->currentIndex() == 1, "a combo with no declared default is left alone");

  auto* box = new QCheckBox("Show", &host);
  box->show();
  stencil::support::setResetDefault(box, true);
  int clicks = 0;
  QObject::connect(box, &QCheckBox::clicked, [&] { ++clicks; });
  doubleClick(box);
  check(box->isChecked(), "a double-click on the box lands on its default, not on two toggles");
  check(clicks == 3, "…the reset being one more click through the box's own chain");

  auto* caption = new QLabel("Keep", &host);
  auto* other = new QCheckBox(&host);
  caption->show();
  other->show();
  other->setChecked(true);
  stencil::support::setResetDefault(other, false);
  stencil::support::captionToggles(caption, other);
  doubleClick(caption);
  check(!other->isChecked(), "a double-click on the caption resets the box it stands for");

  QMenu menu;
  QAction* row = menu.addAction("Show Points");
  row->setCheckable(true);
  stencil::support::setResetDefault(row, true);
  menu.popup(QPoint(10, 10));
  QApplication::processEvents();
  const QPointF at = menu.actionGeometry(row).center();
  for (QEvent::Type t : {QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}) {
    QMouseEvent ev(t, at, menu.mapToGlobal(at), Qt::LeftButton,
                   t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&menu, &ev);
  }
  QApplication::processEvents();
  check(row->isChecked(), "a double-click on a checkable menu row ends on its default");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
