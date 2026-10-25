// One row of the Lines tab, built from its line: its number, name, colour and thickness, its points'
// colour and size, its point count, its bin and its eye (browser ui/panel/lines/list.js rowOf); past
// LAZY_ROWS a row's widgets are made once it scrolls into view.
#include "SelectionPanel.hpp"
#include "linesTableParts.hpp"
#include "cssColor.hpp"
#include "iconSet.hpp"
#include "../../support/control/dblReset.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <algorithm>
#include <cmath>

namespace stencil::gui {

  namespace {
    // Every cell names itself (browser lines/list.js TIPS); a pair's second half shares its heading.
    const QString LINE_TIP = QStringLiteral("Line color\nDouble-click: the toolbar's line color");
    const QString AREA_TIP = QStringLiteral("Line color — an area shows its fill inside\n"
                                            "Double-click: the toolbar's line color");
    const QString POINT_TIP = QStringLiteral("Point color\nDouble-click: the toolbar's point color");

    QString rangeTip(const char* what, int lo, int hi) {
      return QStringLiteral("%1\nDouble-click to edit (%2–%3 px)").arg(QLatin1String(what)).arg(lo).arg(hi);
    }
  }  // namespace

  bool SelectionPanel::lazyLineRows() const { return lines && lines->rowCount() > LAZY_ROWS; }

  void SelectionPanel::buildLineRow(int i, const core::Line& ln, const QColor& stroke) {
    const support::lineLimits::Table& limits = support::lineLimits::table();
    const QString thickTip = rangeTip("Line thickness", limits.thickMin, limits.thickMax);
    const QString sizeTip = rangeTip("Point size", limits.pointMin, limits.pointMax);
    // A size is an int, so its editor is a spin box; the rest is text, never a locale's 1,234.
    const auto cell = [this, i](int col, const QVariant& value, const QString& tip, bool editable = false) {
      auto* it = new QTableWidgetItem();
      it->setData(Qt::EditRole, value);
      it->setFlags(editable ? Qt::ItemFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable) : Qt::ItemFlags(Qt::ItemIsEnabled));
      it->setToolTip(tip);
      this->lines->setItem(i, col, it);
    };
    cell(LCOL_INDEX, QString::number(i + 1), QStringLiteral("Line %1").arg(i + 1));
    addLineNameCell(i, ln);
    cell(LCOL_COLOR, QString(), ln.locked ? AREA_TIP : LINE_TIP);
    cell(LCOL_THICK, int(std::lround(ln.thickness)), thickTip, true);
    cell(LCOL_POINT, QString(), POINT_TIP);
    cell(LCOL_SIZE, int(std::lround(ln.pointSize)), sizeTip, true);
    cell(LCOL_PTS, QString::number(int(ln.points.size())), QStringLiteral("Points"));
    if (!lazyLineRows()) buildLineRowWidgets(i, ln, stroke);
    else if (ln.hidden) dimHiddenRow(i);   // its ink dims now; its widgets fade as they are made
  }

  void SelectionPanel::buildLineRowWidgets(int i, const core::Line& ln, const QColor& stroke) {
    // A chip's click opens its picker once the double-click window passes; a second one resets.
    const auto chip = [this, i](int col, const QColor& face, const QColor& rim, const QString& tip) {
      const bool point = col == LCOL_POINT;
      auto* c = new LineChip(point, this->lines);
      c->setObjectName(point ? QStringLiteral("linesPointSwatch") : QStringLiteral("linesSwatch"));
      c->setColors(face, rim);
      c->setToolTip(tip);
      support::wireColorChip(c, [this, i, point] { if (point) emit pointSwatchPick(i); else emit lineSwatchPick(i); },
                             [this, i, point] { if (point) emit pointSwatchReset(i); else emit lineSwatchReset(i); });
      this->lines->setCellWidget(i, col, centeredCell(c, this->lines));
    };
    // What the line LOOKS like: its fill when it has one, its stroke otherwise — the stroke
    // always as the rim (browser lines/list.js over core/layout.js fillState).
    const QColor rim = ln.color.empty() ? stroke : cssColor(ln.color);
    const bool filled = !ln.fillColor.empty() && ln.fillColor != "transparent";
    const QColor face = filled ? cssColor(ln.fillColor) : ln.locked ? QColor(Qt::transparent) : rim;
    chip(LCOL_COLOR, face, rim, ln.locked ? AREA_TIP : LINE_TIP);
    const std::string& dots = core::pointColorOr(ln);
    chip(LCOL_POINT, dots.empty() ? stroke : cssColor(dots), QColor(), POINT_TIP);

    auto* rm = new QPushButton(this->lines);
    rm->setObjectName("pointDelBtn");
    rm->setFlat(true);
    rm->setCursor(Qt::PointingHandCursor);
    rm->setToolTip("Remove line");
    rm->setIcon(themedIcon("trash", binColor, 14));
    connect(rm, &QPushButton::clicked, this, [this, i] {
      const QRect rowRect(0, this->lines->rowViewportPosition(i),
                          this->lines->viewport()->width(), this->lines->rowHeight(i));
      DisintegrateOverlay::overRect(this->lines->viewport(), rowRect, window());
      emit lineListRemoveRequested(i);
    });
    this->lines->setCellWidget(i, LCOL_DEL, centeredCell(rm, this->lines));
    addLineEye(i, ln.hidden);
    if (ln.hidden) dimHiddenRow(i);
  }

  void SelectionPanel::ensureLineRowWidgets(int i) {
    if (i < 0 || i >= lines->rowCount() || i >= static_cast<int>(builtRows.size()) || lines->cellWidget(i, LCOL_DEL)) return;
    buildLineRowWidgets(i, builtRows[static_cast<std::size_t>(i)].first, builtStroke);
  }

  // A scroll, or a viewport that grew (its range moves), brings a long list's next rows on screen.
  void SelectionPanel::watchRowsShown() {
    connect(lines->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { materializeLineRows(); });
    connect(lines->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { materializeLineRows(); });
    connect(points->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { materializePointRows(); });
    connect(points->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { materializePointRows(); });
  }

  void SelectionPanel::materializeLineRows() {
    if (!lines || builtRows.empty()) return;
    const auto [first, last] = lazyLineRows() ? shownRows(lines) : std::pair{0, lines->rowCount() - 1};
    for (int i = first; i <= last; ++i) ensureLineRowWidgets(i);
  }

}  // namespace stencil::gui
