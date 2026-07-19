#include "selectionPanel.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QTabWidget>
#include <QPainter>
#include <QPalette>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace stencil::gui {

  namespace {
    // Points-table columns: index · X(px, editable) · Y(px, editable) · page(cm, read-only) · 🗑.
    enum PointCol { ColIndex = 0, ColX, ColY, ColPage, ColDel, ColCount };

    // Paints a selected row as a flat accent OUTLINE (not a filled background); hover tint + cell
    // text come from QSS / the base. Mirrors the browser row treatment but with an outline.
    class PointRowDelegate : public QStyledItemDelegate {
     public:
      using QStyledItemDelegate::QStyledItemDelegate;
      void paint(QPainter* p, const QStyleOptionViewItem& opt,
                 const QModelIndex& idx) const override {
        // The selection FILL is made transparent via QSS (selection-background-color); here we
        // just stroke an accent outline around the selected row on top of the normal item paint.
        QStyledItemDelegate::paint(p, opt, idx);
        if (!(opt.state & QStyle::State_Selected)) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        p->setPen(QPen(opt.palette.color(QPalette::Highlight), 2));
        const QRect r = opt.rect.adjusted(0, 1, 0, -1);
        p->drawLine(r.topLeft(), r.topRight());
        p->drawLine(r.bottomLeft(), r.bottomRight());
        if (idx.column() == ColIndex) p->drawLine(r.topLeft(), r.bottomLeft());
        if (idx.column() == ColCount - 1) p->drawLine(r.topRight(), r.bottomRight());
        p->restore();
      }
    };
  }

  SelectionPanel::SelectionPanel(QWidget* parent)
      : QDockWidget("Selection", parent) {
    // Pinned in place: NOT floatable/movable (the browser panel can't be torn off / unpinned) — it
    // can only be hidden/shown via the toggle. Removes the unwanted drag-to-float behaviour.
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    // Custom title bar with a right-aligned chevron that hides the panel — mirrors the browser
    // panel header's #toggle-coord-panel chevron (placed IN the panel, not floating over the canvas).
    auto* titleBar = new QWidget(this);
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(10, 5, 6, 5);
    auto* titleLbl = new QLabel("Points", titleBar);
    titleLbl->setStyleSheet("font-weight:600;");
    collapseBtn_ = new QToolButton(titleBar);
    collapseBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    collapseBtn_->setCursor(Qt::PointingHandCursor);
    collapseBtn_->setToolTip("Hide panel");
    // Same rounded-square look as the floating re-open chevron (guiHelpers::panelToggleQss) so the
    // shown/hidden toggles read as one consistent button, just mirrored.
    collapseBtn_->setFixedSize(28, 28);
    collapseBtn_->setIconSize(QSize(18, 18));
    collapseBtn_->setStyleSheet(panelToggleQss());
    connect(collapseBtn_, &QToolButton::clicked, this, [this] { emit collapseRequested(); });
    titleRow->addWidget(titleLbl);
    titleRow->addStretch(1);
    titleRow->addWidget(collapseBtn_);
    setTitleBarWidget(titleBar);

    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 8);

    // ── inline line editor (browser selectionPanel.js: selection-panel-inner) ──
    // Sits above the points list; mirrors the browser top selection bar.
    editor_ = new QWidget(body);
    auto* form = new QFormLayout(editor_);
    form->setContentsMargins(0, 0, 0, 8);

    // selColor — drawingApp.js:1545 / :181
    colorSwatch_ = new QPushButton(editor_);
    colorSwatch_->setToolTip("Line color");
    setSwatchColor(colorSwatch_, currentColor_);
    form->addRow("Color:", colorSwatch_);

    // selThickness — drawingApp.js:1546 / :182 (min 1, max 20)
    thickness_ = new QSpinBox(editor_);
    thickness_->setRange(1, 20);
    thickness_->setToolTip("Thickness of the selected line (px)");
    form->addRow("Thickness:", thickness_);

    // selMarkerSize — drawingApp.js:1547 / :183 (min 1, max 30)
    markerSize_ = new QSpinBox(editor_);
    markerSize_->setRange(1, 30);
    markerSize_->setToolTip("Point marker size of the selected line (px)");
    form->addRow("Marker Size:", markerSize_);

    // selStyle — drawingApp.js:1548 / :184
    style_ = new QComboBox(editor_);
    style_->addItem("Solid", "solid");
    style_->addItem("Dashed", "dashed");
    style_->addItem("Dotted", "dotted");
    style_->setToolTip("Stroke style of the selected line (solid, dashed, dotted)");
    form->addRow("Style:", style_);

    // selFillGroup — locked-area fill, hidden unless line.locked
    // (selectionPanel.js:29-33; drawingApp.js:1550-1560).
    fillGroup_ = new QWidget(editor_);
    auto* fillRow = new QHBoxLayout(fillGroup_);
    fillRow->setContentsMargins(0, 0, 0, 0);
    fillEnabled_ = new QCheckBox(fillGroup_);  // selFillEnabled
    fillEnabled_->setToolTip("Locked area fill");
    fillSwatch_ = new QPushButton(fillGroup_);  // selFill
    fillSwatch_->setToolTip("Area fill color");
    setSwatchColor(fillSwatch_, currentFill_);
    fillClear_ = new QPushButton(fillGroup_);  // selFillClear (x icon)
    fillClear_->setToolTip("Clear fill (make transparent)");
    fillRow->addWidget(fillEnabled_);
    fillRow->addWidget(fillSwatch_);
    fillRow->addWidget(fillClear_);
    fillRow->addStretch(1);
    form->addRow("Fill:", fillGroup_);

    // Delete line + selDeselect (drawingApp.js:195 deselectLine). Delete is the
    // danger action — given the red treatment via objectName (styled in theme.cpp,
    // matching the browser's --danger delete button); icons set in restyleIcons().
    auto* btnRow = new QHBoxLayout();
    deleteLine_ = new QPushButton("Delete Line", editor_);
    deleteLine_->setObjectName("dangerButton");
    deleteLine_->setToolTip("Delete the selected line");
    deselectBtn_ = new QPushButton("Deselect", editor_);  // selDeselect
    deselectBtn_->setToolTip("Clear the current selection");
    btnRow->addWidget(deleteLine_);
    btnRow->addWidget(deselectBtn_);
    form->addRow(btnRow);

    layout->addWidget(editor_);

    // Shown instead of the inline editor while 2+ lines are multi-selected (Ctrl+Shift+click):
    // the editor is ambiguous, so it's hidden and this explains the mode.
    multiLabel_ = new QLabel(body);
    multiLabel_->setWordWrap(true);
    multiLabel_->setStyleSheet("color: palette(highlight); font-weight: 600;");
    multiLabel_->setVisible(false);
    layout->addWidget(multiLabel_);

    // Points | Lines tabs (browser mainContent.js coord-tabs). The Points tab holds the
    // per-line coordinate table + measurements (the existing panel body); the Lines tab lists
    // every committed line for select/inspect/remove (browser renderLinesList / #lines-list).
    tabs_ = new QTabWidget(body);
    tabs_->setObjectName("selectionTabs");
    layout->addWidget(tabs_, 1);

    auto* ptsTab = new QWidget(tabs_);
    auto* ptsLay = new QVBoxLayout(ptsTab);
    ptsLay->setContentsMargins(0, 6, 0, 0);
    points_ = new QTableWidget(0, ColCount, ptsTab);
    points_->setObjectName("pointsTable");
    points_->setItemDelegate(new PointRowDelegate(points_));  // outline-style selection
    points_->setHorizontalHeaderLabels({"#", "X", "Y", "Page", QString()});
    points_->verticalHeader()->setVisible(false);
    points_->setSelectionBehavior(QAbstractItemView::SelectRows);
    points_->setSelectionMode(QAbstractItemView::SingleSelection);
    points_->setShowGrid(false);
    points_->setAlternatingRowColors(true);
    points_->setWordWrap(false);
    // Only the X/Y px cells are editable (via double-click); # / page / 🗑 stay read-only.
    points_->setEditTriggers(QAbstractItemView::DoubleClicked |
                             QAbstractItemView::EditKeyPressed);
    points_->installEventFilter(this);
    auto* hh = points_->horizontalHeader();
    hh->setSectionResizeMode(ColIndex, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColX, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColY, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColPage, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColDel, QHeaderView::Fixed);
    points_->setColumnWidth(ColDel, 34);
    hh->setHighlightSections(false);
    ptsLay->addWidget(points_, 1);

    tabs_->addTab(ptsTab, "Points");

    // Lines tab — a flat list of every committed line (browser #lines-list). Each row: color
    // chip · "Line N · M pts" · 🗑. Rows single-select on click (Ctrl/⌘+Shift toggles the
    // multi-select set); the 🗑 removes the line. Populated by setLines().
    auto* linesTab = new QWidget(tabs_);
    auto* linesLay = new QVBoxLayout(linesTab);
    linesLay->setContentsMargins(0, 6, 0, 0);
    lines_ = new QListWidget(linesTab);
    lines_->setObjectName("linesList");
    lines_->setSelectionMode(QAbstractItemView::NoSelection);  // selection is driven by the canvas
    lines_->setFocusPolicy(Qt::NoFocus);
    linesLay->addWidget(lines_, 1);
    tabs_->addTab(linesTab, "Lines");

    // Row click → select that line (multi = Ctrl/⌘+Shift held, mirroring the canvas modifier).
    connect(lines_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
      const int idx = lines_->row(it);
      if (idx < 0) return;
      const auto mods = QGuiApplication::keyboardModifiers();
      const bool multi = (mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
                         (mods & Qt::ShiftModifier);
      emit lineListActivated(idx, multi);
    });

    setWidget(body);
    restyleIcons(palette().color(QPalette::WindowText));

    // Click a row (not the 🗑 column) → focus that point on the canvas.
    connect(points_, &QTableWidget::cellClicked, this, [this](int row, int col) {
      if (col != ColDel) emit pointActivated(row);
    });
    // A committed X/Y edit → forward to the canvas (guarded against showLine's repopulation).
    connect(points_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
      if (updating_ || !it) return;
      const int col = it->column();
      if (col != ColX && col != ColY) return;
      bool ok = false;
      const double v = it->text().toDouble(&ok);
      if (ok) emit pointCoordChanged(it->row(), col == ColX ? 0 : 1, v);
    });

    // ── inline-editor wiring — each lambda early-returns while showLine is
    // repopulating the controls (updating_), matching the browser which guards
    // via selectedLineIdx and re-sets .value without firing change handlers. ──

    // selColor: open a color dialog, repaint swatch, emit (drawingApp.js:181).
    connect(colorSwatch_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      const QColor c = QColorDialog::getColor(currentColor_, this, "Line color",
                                              QColorDialog::DontUseNativeDialog);
      if (!c.isValid()) return;
      currentColor_ = c;
      setSwatchColor(colorSwatch_, c);
      emit lineColorChanged(c.name());
    });
    // selThickness (drawingApp.js:182).
    connect(thickness_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              if (updating_) return;
              emit lineThicknessChanged(v);
            });
    // selMarkerSize (drawingApp.js:183).
    connect(markerSize_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              if (updating_) return;
              emit lineMarkerSizeChanged(v);
            });
    // selStyle (drawingApp.js:184).
    connect(style_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
              if (updating_) return;
              emit lineStyleChanged(style_->currentData().toString());
            });

    // selFillEnabled: emit chosen color when on, "transparent" when off
    // (drawingApp.js:185 applyFill).
    connect(fillEnabled_, &QCheckBox::toggled, this, [this](bool on) {
      if (updating_) return;
      emit lineFillChanged(on ? currentFill_.name() : QStringLiteral("transparent"));
    });
    // selFill: choosing a color implies enabled=true (drawingApp.js:186-189).
    connect(fillSwatch_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      const QColor c = QColorDialog::getColor(currentFill_, this, "Area fill color",
                                              QColorDialog::DontUseNativeDialog);
      if (!c.isValid()) return;
      currentFill_ = c;
      setSwatchColor(fillSwatch_, c);
      {
        QSignalBlocker block(fillEnabled_);
        fillEnabled_->setChecked(true);
      }
      emit lineFillChanged(c.name());
    });
    // selFillClear: clear fill → transparent (drawingApp.js:190-193).
    connect(fillClear_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      {
        QSignalBlocker block(fillEnabled_);
        fillEnabled_->setChecked(false);
      }
      emit lineFillChanged(QStringLiteral("transparent"));
    });

    connect(deleteLine_, &QPushButton::clicked, this, [this] {
      if (!updating_) emit lineDeleteRequested();
    });
    // selDeselect (drawingApp.js:195 deselectLine).
    connect(deselectBtn_, &QPushButton::clicked, this, [this] {
      if (!updating_) emit deselectRequested();
    });

    showLine(nullptr, nullptr, -1);
  }

  void SelectionPanel::setSwatchColor(QPushButton* btn, const QColor& color) {
    setColorSwatch(btn, color);  // QPushButton derives from QAbstractButton
  }

  void SelectionPanel::setMultiSelectCount(int n) {
    if (!multiLabel_) return;
    if (n >= 2) {
      multiLabel_->setText(QString("%1 lines selected — Ctrl+Shift+click to add/remove · "
                                   "Alt+Shift+drag to move all · Ctrl+Shift+scroll to rotate all · "
                                   "Alt+Shift+arrows to flip / rotate 90°")
                               .arg(n));
      multiLabel_->setVisible(true);
    } else {
      multiLabel_->setVisible(false);
    }
  }

  void SelectionPanel::setLines(const core::Lines& lines,
                                const std::vector<int>& selected) {
    if (!lines_) return;
    QSignalBlocker block(lines_);
    lines_->clear();
    if (lines.empty()) {
      auto* item = new QListWidgetItem("No lines yet.", lines_);
      item->setFlags(Qt::NoItemFlags);
      item->setTextAlignment(Qt::AlignCenter);
      return;
    }
    const auto isSel = [&](int i) {
      return std::find(selected.begin(), selected.end(), i) != selected.end();
    };
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
      const core::Line& ln = lines[i];
      auto* item = new QListWidgetItem(lines_);

      auto* row = new QWidget(lines_);
      auto* rl = new QHBoxLayout(row);
      rl->setContentsMargins(6, 4, 6, 4);
      rl->setSpacing(8);

      // Color chip — transparent for an unfilled locked area (matches the browser swatch).
      auto* swatch = new QLabel(row);
      swatch->setFixedSize(14, 14);
      swatch->setAttribute(Qt::WA_TransparentForMouseEvents);
      const QString colName = QString::fromStdString(ln.color);
      const bool unfilledArea =
          ln.locked && (ln.fillColor.empty() || ln.fillColor == "transparent");
      swatch->setStyleSheet(
          QString("background:%1;border:1px solid %2;border-radius:3px;")
              .arg(unfilledArea ? QStringLiteral("transparent") : colName, colName));

      auto* label = new QLabel(row);
      label->setAttribute(Qt::WA_TransparentForMouseEvents);
      const int np = static_cast<int>(ln.points.size());
      QString text = QString("Line %1 · %2 pt%3")
                         .arg(i + 1).arg(np).arg(np == 1 ? "" : "s");
      if (ln.locked) text += " · area";
      label->setText(text);

      auto* rm = new QPushButton(row);
      rm->setObjectName("pointDelBtn");
      rm->setFlat(true);
      rm->setCursor(Qt::PointingHandCursor);
      rm->setToolTip("Remove line");
      rm->setIcon(themedIcon("trash", iconColor_, 14));
      connect(rm, &QPushButton::clicked, this,
              [this, i] { emit lineListRemoveRequested(i); });

      rl->addWidget(swatch);
      rl->addWidget(label, 1);
      rl->addWidget(rm);

      // Selected rows carry an accent outline (canvas-driven, since selection mode is Off).
      if (isSel(i))
        row->setStyleSheet(
            "background: palette(alternate-base);"
            "border:1px solid palette(highlight);border-radius:5px;");

      item->setSizeHint(row->sizeHint());
      lines_->setItemWidget(item, row);
    }
  }

  void SelectionPanel::restyleIcons(const QColor& iconColor) {
    // Delete is a red danger button, so its glyph stays white for contrast; the
    // others follow the theme text color (re-applied on each light/dark switch).
    if (deleteLine_) deleteLine_->setIcon(themedIcon("trash", QColor("#ffffff"), 15));
    if (deselectBtn_) deselectBtn_->setIcon(themedIcon("x", iconColor, 15));
    if (fillClear_) fillClear_->setIcon(themedIcon("x", iconColor, 14));
    // Chevron points toward the edge to hide (›) the panel.
    if (collapseBtn_) collapseBtn_->setIcon(themedIcon("chevron-right", iconColor, 18));
    // Re-theme the per-row 🗑 buttons too (new ones in showLine use the stored colour).
    iconColor_ = iconColor;
    if (points_) {
      for (int r = 0; r < points_->rowCount(); ++r)
        if (auto* b = qobject_cast<QPushButton*>(points_->cellWidget(r, ColDel)))
          b->setIcon(themedIcon("trash", iconColor_, 14));
    }
  }

  void SelectionPanel::setToggleHint(const QString& hint) {
    if (!collapseBtn_) return;
    collapseBtn_->setToolTip(hint.isEmpty() ? QStringLiteral("Hide panel")
                                            : QStringLiteral("Hide panel (%1)").arg(hint));
  }

  void SelectionPanel::showLine(const core::Line* line,
                                const core::Line* editorLine, int selectedPoint,
                                const std::vector<QString>& cmRows) {
    points_->setRowCount(0);  // clear rows (NOT clear() — that would drop the header labels)

    // Populate the inline editor from the *selected* line only, suppressing the
    // control change handlers while we do so (drawingApp.js:1544-1564). The
    // browser reveals #selectionPanel solely on an explicit selection; gating on
    // editorLine (canvas selectedLine(), null when selectedLineIdx_ < 0) keeps
    // the editor hidden for the fallback panelLine() — whose mutators all
    // early-return — so the user never sees controls that silently do nothing.
    updating_ = true;
    editor_->setVisible(editorLine != nullptr);
    if (editorLine) {
      currentColor_ = QColor(QString::fromStdString(editorLine->color));
      setSwatchColor(colorSwatch_, currentColor_);
      thickness_->setValue(
          static_cast<int>(std::lround(editorLine->thickness)));
      markerSize_->setValue(
          static_cast<int>(std::lround(editorLine->markerSize)));
      const int sidx =
          style_->findData(QString::fromStdString(editorLine->style));
      style_->setCurrentIndex(sidx >= 0 ? sidx : 0);

      // Fill controls only for locked areas (drawingApp.js:1551-1560).
      fillGroup_->setVisible(editorLine->locked);
      if (editorLine->locked) {
        const QString fc = QString::fromStdString(editorLine->fillColor);
        const bool hasFill = !fc.isEmpty() && fc != "transparent";
        fillEnabled_->setChecked(hasFill);
        if (hasFill) {
          currentFill_ = QColor(fc);
          setSwatchColor(fillSwatch_, currentFill_);
        }
      }
    }
    updating_ = false;

    if (!line || line->points.empty()) return;

    // Build the editable points table. `updating_` suppresses the itemChanged handler while we
    // set cell text (only a USER edit should fire pointCoordChanged). X/Y are editable px cells
    // (double-click); the page (cm) column is read-only and pre-formatted by the caller; each row
    // ends with a 🗑 button. Mirrors browser coordTable.js.
    updating_ = true;
    points_->setRowCount(static_cast<int>(line->points.size()));
    for (std::size_t i = 0; i < line->points.size(); ++i) {
      const auto& p = line->points[i];
      const int r = static_cast<int>(i);
      auto* idx = new QTableWidgetItem(QString::number(i + 1));
      idx->setFlags(Qt::ItemIsEnabled);
      idx->setTextAlignment(Qt::AlignCenter);
      points_->setItem(r, ColIndex, idx);
      auto* xi = new QTableWidgetItem(QString::number(p.x, 'f', 1));
      xi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
      xi->setToolTip("Double-click to edit X (px)");
      points_->setItem(r, ColX, xi);
      auto* yi = new QTableWidgetItem(QString::number(p.y, 'f', 1));
      yi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
      yi->setToolTip("Double-click to edit Y (px)");
      points_->setItem(r, ColY, yi);
      auto* pg = new QTableWidgetItem(i < cmRows.size() ? cmRows[i] : QString());
      pg->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      points_->setItem(r, ColPage, pg);
      auto* del = new QPushButton(points_);
      del->setObjectName("pointDelBtn");
      del->setFlat(true);
      del->setCursor(Qt::PointingHandCursor);
      del->setToolTip("Remove point");
      del->setIcon(themedIcon("trash", iconColor_, 14));
      connect(del, &QPushButton::clicked, this, [this, r] { emit pointDeleteRequested(r); });
      points_->setCellWidget(r, ColDel, del);
    }
    if (selectedPoint >= 0 && selectedPoint < points_->rowCount())
      points_->selectRow(selectedPoint);
    points_->resizeRowsToContents();
    updating_ = false;
  }

  bool SelectionPanel::eventFilter(QObject* obj, QEvent* event) {
    if (obj == points_ && event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      if ((ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) &&
          points_->currentRow() >= 0) {
        emit pointDeleteRequested(points_->currentRow());
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }

}
