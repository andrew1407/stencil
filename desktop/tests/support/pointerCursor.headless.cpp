// The app-wide pointer cursor (src/support/guiHelpers.cpp installPointerCursor), the desktop twin of
// the browser's `cursor: pointer` on button, select, a checkbox or radio with its label, and
// .ctx-item: set at polish, never over a cursor a widget chose, and per item inside a menu.
#include "guiHelpers.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWidget>

#include "../support/check.hpp"

namespace {
  bool pointing(const QWidget* w) { return w->cursor().shape() == Qt::PointingHandCursor; }

  void moveOver(QMenu& menu, const QPoint& at) {
    QMouseEvent move(QEvent::MouseMove, QPointF(at), menu.mapToGlobal(QPointF(at)), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&menu, &move);
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  stencil::gui::installPointerCursor(&app);

  QWidget root;
  auto* rows = new QVBoxLayout(&root);
  auto* box = new QCheckBox(QStringLiteral("Save chats with projects"));
  auto* radio = new QRadioButton(QStringLiteral("Album"));
  auto* button = new QPushButton(QStringLiteral("Save"));
  auto* combo = new QComboBox;
  combo->addItems({QStringLiteral("A4"), QStringLiteral("A5")});
  auto* label = new QLabel(QStringLiteral("Provider"));
  auto* own = new QPushButton(QStringLiteral("OK"));
  own->setCursor(Qt::ForbiddenCursor);   // as the prompt's OK does while its text is refused
  for (QWidget* w : {static_cast<QWidget*>(box), static_cast<QWidget*>(radio), static_cast<QWidget*>(button),
                     static_cast<QWidget*>(combo), static_cast<QWidget*>(label), static_cast<QWidget*>(own)})
    rows->addWidget(w);
  root.show();

  check(pointing(box), "a checkbox and its label wear the pointer (browser .vs-inline-check)");
  check(pointing(radio), "a radio and its label wear the pointer");
  check(pointing(button), "a button wears the pointer");
  check(pointing(combo), "a selector wears the pointer");
  check(!label->testAttribute(Qt::WA_SetCursor), "a plain label keeps the arrow");
  check(own->cursor().shape() == Qt::ForbiddenCursor, "a cursor the widget chose is kept");

  QMenu menu;
  QAction* crop = menu.addAction(QStringLiteral("Crop"));
  QAction* sep = menu.addSeparator();
  QAction* off = menu.addAction(QStringLiteral("Unavailable"));
  off->setEnabled(false);
  menu.popup(QPoint(0, 0));
  moveOver(menu, menu.actionGeometry(crop).center());
  check(pointing(&menu), "a menu item wears the pointer (browser .ctx-item)");
  moveOver(menu, menu.actionGeometry(sep).center());
  check(!pointing(&menu), "a separator is not an item");
  moveOver(menu, menu.actionGeometry(crop).center());
  check(pointing(&menu), "back on an item, the pointer returns");
  moveOver(menu, menu.actionGeometry(off).center());
  check(!pointing(&menu), "a disabled item takes no click, so no pointer");
  menu.hide();

  return failures ? 1 : 0;
}
