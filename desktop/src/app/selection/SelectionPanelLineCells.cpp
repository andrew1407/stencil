// The Lines tab's per-row name and eye, a hidden line's dimmed row, and the row restyle hover and
// selection share (browser ui/panel/lines/list.js nameCell / eyeCell and .lines-row-hidden).
#include "SelectionPanel.hpp"
#include "linesTableParts.hpp"
#include "../../support/skinPrefs.hpp"
#include <QGraphicsOpacityEffect>
#include <algorithm>

namespace stencil::gui {

  namespace {
    // Browser .lines-row-hidden td: every cell but the eye, which stays the way back.
    constexpr double HIDDEN_ROW_OPACITY = 0.45;
    const QString NAME_TIP = QStringLiteral("Line name\nDouble-click to rename");
  }  // namespace

  void SelectionPanel::addLineNameCell(int i, const core::Line& ln) {
    auto* it = new QTableWidgetItem(ln.name.empty() ? QStringLiteral("Line %1").arg(i + 1)
                                                    : QString::fromStdString(ln.name));
    it->setData(NAME_ROLE, QString::fromStdString(ln.name));
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
    it->setToolTip(NAME_TIP);
    QFont f = lines->font();
    f.setItalic(ln.name.empty());   // browser .lines-name-unset
    it->setFont(f);
    lines->setItem(i, LCOL_NAME, it);
  }

  void SelectionPanel::addLineEye(int i, bool hidden) {
    auto* eye = new LineEyeButton(hidden, i == eyeFlipRow, lines);
    connect(eye, &QPushButton::clicked, this, [this, i, hidden] {
      eyeFlipRow = i;
      emit lineHiddenToggled(i, !hidden);
    });
    lines->setCellWidget(i, LCOL_EYE, centeredCell(eye, lines));
  }

  void SelectionPanel::dimHiddenRow(int i) {
    if (QTableWidgetItem* it = lines->item(i, LCOL_INDEX)) it->setData(HIDDEN_ROLE, true);
    for (int c = 0; c < LCOL_COUNT; ++c) {
      QWidget* cell = c == LCOL_EYE ? nullptr : lines->cellWidget(i, c);
      if (!cell) continue;
      auto* fade = new QGraphicsOpacityEffect(cell);
      fade->setOpacity(HIDDEN_ROW_OPACITY);
      cell->setGraphicsEffect(fade);
    }
  }

  // Kept in one place so setCanvasHover can restyle two rows without rebuilding or scrolling.
  void SelectionPanel::styleLineRow(int i) {
    if (!lines || i < 0 || i >= lines->rowCount()) return;
    if (isEmptyRow(lines, i)) {
      if (QTableWidgetItem* it = lines->item(0, 0)) it->setBackground(i == canvasHoverLineRow ? emptyWash() : QBrush());
      return;
    }
    const bool sel = std::find(linesSelected.begin(), linesSelected.end(), i) !=
                     linesSelected.end();
    // A restyle is no edit: itemChanged would read a size cell's new wash as a typed value.
    QSignalBlocker quiet(lines);
    // Browser .lines-row-selected (the delegate strokes the outline) and .lines-row-hover.
    const bool hot = i == canvasHoverLineRow;
    const QBrush wash = (sel || hot) ? rowWash(sel) : QBrush();
    const QTableWidgetItem* index = lines->item(i, LCOL_INDEX);
    const bool hidden = index && index->data(HIDDEN_ROLE).toBool();
    const QColor muted = palette().color(QPalette::PlaceholderText);
    for (int c = 0; c < LCOL_COUNT; ++c)
      if (QTableWidgetItem* it = lines->item(i, c)) {
        it->setBackground(wash);
        const bool unnamed = c == LCOL_NAME && it->data(NAME_ROLE).toString().isEmpty();
        QBrush ink = sel && support::isWebcore() ? rowInk() : unnamed ? QBrush(muted) : QBrush();
        if (hidden) {
          QColor faded = ink.style() == Qt::NoBrush ? palette().color(QPalette::Text) : ink.color();
          faded.setAlphaF(faded.alphaF() * HIDDEN_ROW_OPACITY);
          ink = QBrush(faded);
        }
        it->setForeground(ink);
        it->setData(SELECTED_ROLE, sel);
      }
  }

}  // namespace stencil::gui
