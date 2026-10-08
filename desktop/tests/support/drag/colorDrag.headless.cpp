// Dragging a colour swatch onto another (support/drag/colorDrag, browser twin ui/drag/colorDrag.js) on
// live chips and a table of swatch cells: the colour each target takes, the glow while a drag is live,
// a drop through the target's own apply, and the drops that do nothing; a cell drag clicks no cell.
#include "colorDrag.hpp"
#include "iconDrag.hpp"
#include "uiTimings.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>

#include "../check.hpp"

using namespace stencil::support;

namespace {
  void mouse(QWidget* w, QEvent::Type type, const QPoint& local, Qt::MouseButtons held) {
    const Qt::MouseButton b = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(local), QPointF(w->mapToGlobal(local)), b, held, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
  }
  // A whole drag from `from`'s centre to `to`'s, every event at the source, as the window grab delivers them.
  void dragOnto(QWidget* src, const QPoint& fromLocal, QWidget* to, const QPoint& toLocal) {
    const QPoint end = src->mapFromGlobal(to->mapToGlobal(toLocal));
    mouse(src, QEvent::MouseButtonPress, fromLocal, Qt::LeftButton);
    mouse(src, QEvent::MouseMove, fromLocal + QPoint(0, uiTimings().pressSlopPx + 3), Qt::LeftButton);
    mouse(src, QEvent::MouseMove, end, Qt::LeftButton);
    mouse(src, QEvent::MouseButtonRelease, end, Qt::NoButton);
    QApplication::processEvents();
  }
  int glows(QWidget& window) { return window.findChildren<QWidget*>(QLatin1String(DROP_GLOW_NAME)).size(); }
  QString hexa(const QColor& c) { return c.isValid() ? c.name(QColor::HexArgb) : QStringLiteral("none"); }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  const ColorSwatch rgbRed{[] { return QColor("#ff0000"); }, {}};
  const ColorSwatch wellHalfRed{[] { return QColor(255, 0, 0, 128); }, {}, true};
  const ColorSwatch wellGreen{[] { return QColor(0, 255, 0, 64); }, {}, true};
  const ColorSwatch rgbGreen{[] { return QColor("#00ff00"); }, {}};
  check(hexa(colorToApply(wellHalfRed, wellGreen)) == "#80ff0000", "both carrying alpha, the RGBA crosses");
  check(hexa(colorToApply(rgbRed, wellGreen)) == "#40ff0000", "an RGB source keeps the target's alpha");
  check(hexa(colorToApply(wellHalfRed, rgbGreen)) == "#ffff0000", "an RGB target takes the RGB alone");
  check(!colorToApply(rgbRed, rgbRed).isValid(), "a target already showing it takes nothing");
  check(!colorToApply(ColorSwatch{[] { return QColor(0, 0, 0, 0); }, {}, true}, wellGreen).isValid(),
        "alpha 0 shows no colour to hand over");
  check(colorToApply(rgbRed, ColorSwatch{[] { return QColor(255, 0, 0, 0); }, {}, true}).isValid(),
        "a target at alpha 0 shows nothing, so even its own RGB lands");

  QWidget window;
  window.resize(420, 320);
  QStringList log;
  QColor a("#112233"), b("#445566"), off("#778899");
  bool offEnabled = false;
  auto chip = [&](const QString& name, QColor& value, const QRect& at, std::function<bool()> enabled = {}) {
    auto* btn = new QPushButton(name, &window);
    btn->setGeometry(at);
    QObject::connect(btn, &QPushButton::clicked, [&log, name] { log << name + ":click"; });
    installColorDrag(btn, {[&value] { return value; },
                           [&log, &value, name](const QColor& c) { value = c; log << name + ":" + c.name(); },
                           false, std::move(enabled)});
    return btn;
  };
  QPushButton* chipA = chip("a", a, QRect(10, 10, 46, 26));
  QPushButton* chipB = chip("b", b, QRect(10, 60, 46, 26));
  QPushButton* chipOff = chip("off", off, QRect(10, 110, 46, 26), [&offEnabled] { return offEnabled; });
  auto* plain = new QWidget(&window);
  plain->setGeometry(200, 10, 60, 60);
  auto* table = new QTableWidget(3, 2, &window);
  table->setGeometry(100, 120, 300, 180);
  QColor cells[3] = {QColor("#aa0000"), QColor("#00aa00"), QColor("#0000aa")};
  for (int r = 0; r < 3; ++r) table->setItem(r, 0, new QTableWidgetItem(QString::number(r)));
  for (int r = 0; r < 2; ++r) {   // painted chips: the press lands on the viewport
    auto* cell = new QWidget(table);
    cell->setAttribute(Qt::WA_TransparentForMouseEvents);
    table->setCellWidget(r, 1, cell);
  }
  QObject::connect(table, &QTableWidget::cellClicked, [&log](int r, int c) { log << QString("cell%1,%2:click").arg(r).arg(c); });
  installColorDragCells(table, [&](int row, int column) {
    if (column != 1) return ColorSwatch{};
    return ColorSwatch{[&cells, row] { return cells[row]; },
                       [&cells, &log, row](const QColor& c) { cells[row] = c; log << QString("cell%1:%2").arg(row).arg(c.name()); }};
  });
  // A chip button in a cell, set after the table was wired, as a rebuilt row's is.
  auto* holder = new QWidget(table);
  auto* cellChip = new QPushButton(holder);
  cellChip->setGeometry(4, 2, 20, 14);
  QObject::connect(cellChip, &QPushButton::clicked, [&log] { log << QStringLiteral("cellchip:click"); });
  table->setCellWidget(2, 1, holder);
  window.show();
  QApplication::processEvents();

  const QPoint mid(23, 13);
  mouse(chipA, QEvent::MouseButtonPress, mid, Qt::LeftButton);
  mouse(chipA, QEvent::MouseMove, mid + QPoint(0, uiTimings().pressSlopPx + 3), Qt::LeftButton);
  check(glows(window) == 4, "a live drag lights every other enabled swatch: the chip and the three cells");
  mouse(chipA, QEvent::MouseButtonRelease, mid, Qt::NoButton);
  QApplication::processEvents();
  check(glows(window) == 0 && log.isEmpty(), "released back on the source: nothing lands, nothing clicks");
  chipB->setProperty(PAINTED_OUT_PROPERTY, true);
  mouse(chipA, QEvent::MouseButtonPress, mid, Qt::LeftButton);
  mouse(chipA, QEvent::MouseMove, mid + QPoint(0, uiTimings().pressSlopPx + 3), Qt::LeftButton);
  check(glows(window) == 3, "a swatch painted out (the project colour off its hover) is no target");
  mouse(chipA, QEvent::MouseButtonRelease, mid, Qt::NoButton);
  QApplication::processEvents();
  chipB->setProperty(PAINTED_OUT_PROPERTY, false);

  dragOnto(chipA, mid, chipB, mid);
  check(log == QStringList{"b:#112233"} && glows(window) == 0, "released on another chip, it takes the colour once, never a click");

  log.clear();
  dragOnto(chipA, mid, plain, QPoint(30, 30));
  dragOnto(chipA, mid, chipOff, mid);
  dragOnto(chipA, mid, chipB, mid);
  check(log.isEmpty(), "a non-swatch, a disabled swatch and one already that colour take nothing");

  const auto cellCentre = [table](int row) { return table->visualRect(table->model()->index(row, 1)).center(); };
  dragOnto(chipB, mid, table->viewport(), cellCentre(0));
  check(log == QStringList{"cell0:#112233"}, "a chip drops onto a table's swatch cell");

  log.clear();
  dragOnto(table->viewport(), cellCentre(1), table->viewport(), cellCentre(0));
  check(log == QStringList{"cell0:#00aa00"}, "one cell drags onto another in the same table");
  dragOnto(table->viewport(), cellCentre(1), chipA, mid);
  check(log.last() == "a:#00aa00" && !log.join(',').contains("click"), "…and onto a chip, the table clicking no cell");

  log.clear();
  const QPoint chipMid = cellChip->rect().center();
  dragOnto(cellChip, chipMid, chipA, mid);
  check(log == QStringList{"a:#0000aa"}, "a chip button inside a cell drags its cell's colour, never clicking");
  dragOnto(chipB, mid, cellChip, chipMid);
  check(log.last() == "cell2:#112233", "…and a drop on it lands on its cell");

  log.clear();
  const QPoint plainCell = table->visualRect(table->model()->index(0, 0)).center();
  dragOnto(table->viewport(), plainCell, chipB, mid);
  check(!log.join(',').contains("b:"), "a press on a cell holding no swatch drags nothing");

  return failures ? 1 : 0;
}
