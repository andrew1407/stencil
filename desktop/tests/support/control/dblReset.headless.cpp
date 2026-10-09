// Headless checks for support/control/dblReset.hpp (browser twin js/ui/control/dblReset.js): a
// double-click puts a combo or a check back to its default through the signals a pick fires,
// a caption resets its box, a control that declared no default is left alone, and a colour chip
// opens its picker only once the double-click window passes, a second click resetting instead.
#include "../../../src/support/control/dblReset.hpp"
#include "../../../src/support/control/clickToToggle.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
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
  void waitMs(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 10);
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

  auto* chip = new QPushButton(&host);
  int opens = 0, resets = 0;
  stencil::support::wireColorChip(chip, [&opens] { ++opens; }, [&resets] { ++resets; });
  const int window = stencil::support::uiTimings().doubleClickMs;
  chip->click();
  check(opens == 0, "a chip's click waits out the double-click window before opening");
  waitMs(window + 80);
  check(opens == 1 && resets == 0, "a lone click opens the picker once the window passes");
  chip->click();
  chip->click();
  waitMs(window + 80);
  check(opens == 1 && resets == 1, "a double-click resets and never opens the picker");
  chip->click();
  ++stencil::support::dragsStarted();   // the second press dragged the chip away
  waitMs(window + 80);
  check(opens == 1 && resets == 1, "a drag starting inside the window keeps the picker shut");
  chip->click();
  waitMs(window + 80);
  check(opens == 2, "…and the next lone click opens it again");

  // The logo drop's half: what a drop on a widget resets, and the reset itself.
  using stencil::support::dropResetTarget;
  using stencil::support::resetToDefault;
  check(dropResetTarget(chip) == chip && resetToDefault(chip) && resets == 2,
        "a colour chip resets through its own function");
  auto* spin = new QSpinBox(&host);
  spin->setRange(1, 50);
  spin->setValue(9);
  stencil::support::setResetDefault(spin, 2);
  int spun = 0;
  QObject::connect(spin, &QSpinBox::valueChanged, [&spun](int) { ++spun; });
  check(dropResetTarget(spin->findChild<QLineEdit*>()) == spin, "a drop on a spin's inner field finds the spin");
  check(resetToDefault(spin) && spin->value() == 2 && spun == 1, "a spin takes its default through valueChanged");
  check(!resetToDefault(spin) && spun == 1, "…and a spin already at it fires nothing");
  auto* field = new QLineEdit(QStringLiteral("x*2"), &host);
  stencil::support::setResetDefault(field, QString());
  check(resetToDefault(field) && field->text().isEmpty(), "a formula field empties");
  auto* plain = new QLabel(&host);
  check(dropResetTarget(plain) == nullptr, "a widget with no default is no drop target");
  combo->setCurrentIndex(1);
  combo->setEnabled(false);
  check(dropResetTarget(combo) == nullptr && !resetToDefault(combo), "a disabled control is left alone");
  combo->setEnabled(true);
  check(dropResetTarget(combo) == combo && resetToDefault(combo) && combo->currentIndex() == 0,
        "a combo resets as a double-click does");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
