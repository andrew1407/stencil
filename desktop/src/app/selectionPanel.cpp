#include "../support/searchCombo.hpp"
#include "selectionPanel.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "numericInput.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/iconMotion.hpp"
#include "../support/modalReveal.hpp"
#include <QCheckBox>
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
#include <QShowEvent>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace stencil::gui {

  namespace {
    // Points-table columns: index · X(px, editable) · Y(px, editable) · page(cm, read-only) · 🗑.
    enum PointCol { ColIndex = 0, ColX, ColY, ColPage, ColDel, ColCount };

    // The header chevron's box + glyph. Also the floating re-open chevron's, which has to
    // read as the same button (mainWindow kPanelToggleBox).
    constexpr int kToggleBox = 24;
    constexpr int kToggleGlyph = 15;


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
    // Named + WA_StyledBackground so theme.cpp can give it the panel surface: a bare QWidget
    // paints nothing and let the window backdrop through, which is why the header read as a
    // black band across the top of the panel.
    titleBar->setObjectName("selPanelTitle");
    titleBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(10, 2, 5, 2);
    auto* titleLbl = new QLabel("Points", titleBar);
    titleLbl->setStyleSheet("font-weight:600;");
    collapseBtn_ = new QToolButton(titleBar);
    collapseBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    collapseBtn_->setCursor(Qt::PointingHandCursor);
    collapseBtn_->setToolTip("Hide panel");
    // Sits ON the panel surface, so it takes the browser's #toggle-coord-panel treatment
    // (transparent, themed hairline border) rather than the floating chevron's dark slab —
    // that one overlays the canvas, this one would be a dark hole in a light panel.
    collapseBtn_->setObjectName("panelCollapseBtn");   // styled in theme.cpp
    // A fold chevron's angle is STATE (open/closed), not hover feedback — the browser's
    // `[id^="toggle-"]` icon-motion opt-out (iconMotion.json trigger.excluded).
    collapseBtn_->setProperty(kNoIconMotionProperty, true);
    collapseBtn_->setFocusPolicy(Qt::NoFocus);   // no macOS focus halo around the chevron
    collapseBtn_->setFixedSize(kToggleBox, kToggleBox);
    collapseBtn_->setIconSize(QSize(kToggleGlyph, kToggleGlyph));
    connect(collapseBtn_, &QToolButton::clicked, this, [this] { emit collapseRequested(); });
    titleRow->addWidget(titleLbl);
    titleRow->addStretch(1);
    titleRow->addWidget(collapseBtn_);
    setTitleBarWidget(titleBar);

    auto* body = new QWidget(this);
    body->setObjectName("selPanelBody");
    body->setAttribute(Qt::WA_StyledBackground, true);
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
    form->addRow("Line Color:", colorSwatch_);

    // selPointColor — the point colour, set independently of the stroke.
    pointColorSwatch_ = new QPushButton(editor_);
    pointColorSwatch_->setToolTip("Point color for this line");
    setSwatchColor(pointColorSwatch_, currentPointColor_);
    form->addRow("Point Color:", pointColorSwatch_);

    // selThickness — drawingApp.js:1546 / :182 (min 1, max 20)
    thickness_ = new ExprSpinBox(editor_);
    thickness_->setRange(1, 20);
    thickness_->setToolTip("Thickness of the selected line (px)");
    form->addRow("Thickness:", thickness_);

    // selPointSize — drawingApp.js:1547 / :183 (min 1, max 30)
    pointSize_ = new ExprSpinBox(editor_);
    pointSize_->setRange(1, 30);
    pointSize_->setToolTip("Point size of the selected line (px)");
    form->addRow("Point Size:", pointSize_);

    // selStyle — drawingApp.js:1548 / :184
    style_ = new SearchComboBox(editor_, /*searchable=*/false);
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
    // Hover cross-highlight, row → canvas: entering a row rings that point on the
    // canvas (browser coordTable.js row mouseenter). Leave is caught in eventFilter.
    points_->setMouseTracking(true);
    points_->viewport()->setMouseTracking(true);
    connect(points_, &QTableWidget::cellEntered, this,
            [this](int row, int) { emit pointRowHovered(row); });
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
    // ClickFocus (not NoFocus) so a bare Delete/Backspace can be scoped to this list the way
    // it already is for the points table. Selection stays canvas-driven — only the CURRENT
    // row moves on click, which is what the key acts on.
    lines_->setFocusPolicy(Qt::ClickFocus);
    lines_->installEventFilter(this);
    // Hover cross-highlight, row → canvas: entering a row glows that line on the
    // canvas (browser renderLinesList row mouseenter). Leave is caught in eventFilter.
    lines_->setMouseTracking(true);
    lines_->viewport()->setMouseTracking(true);
    connect(lines_, &QListWidget::itemEntered, this,
            [this](QListWidgetItem* it) { emit lineRowHovered(lines_->row(it)); });
    linesLay->addWidget(lines_, 1);
    tabs_->addTab(linesTab, "Lines");

    // Row click → select that line (multi = Ctrl/⌘+Shift held, mirroring the canvas modifier).
    connect(lines_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
      const int idx = lines_->row(it);
      if (idx < 0) return;
      lines_->setCurrentRow(idx);   // the row Delete/Backspace will act on
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
      const QColor c =
          support::pickColorAnimated(currentColor_, this, "Line color", colorSwatch_);
      if (!c.isValid()) return;
      currentColor_ = c;
      setSwatchColor(colorSwatch_, c);
      emit lineColorChanged(c.name());
    });
    // selPointColor: same flow as the line colour, emitting the point signal instead.
    connect(pointColorSwatch_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      const QColor c = support::pickColorAnimated(currentPointColor_, this, "Point color",
                                                  pointColorSwatch_);
      if (!c.isValid()) return;
      currentPointColor_ = c;
      setSwatchColor(pointColorSwatch_, c);
      emit linePointColorChanged(c.name());
    });
    // selThickness (drawingApp.js:182).
    connect(thickness_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              if (updating_) return;
              emit lineThicknessChanged(v);
            });
    // selPointSize (drawingApp.js:183).
    connect(pointSize_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              if (updating_) return;
              emit linePointSizeChanged(v);
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
      const QColor c =
          support::pickColorAnimated(currentFill_, this, "Area fill color", fillSwatch_);
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
    // clear() drops the current row, so a keyboard delete (which repopulates the list)
    // would lose its target and the next Delete would do nothing. Carry it across, clamped
    // to the new count — the browser re-focuses the equivalent row for the same reason.
    const int prevCurrent = lines_->currentRow();
    lines_->clear();
    linesSelected_ = selected;   // styleLineRow's selection snapshot
    canvasHoverPointRow_ = -1;   // rebuilt rows carry no stale hover tint
    canvasHoverLineRow_ = -1;
    if (lines.empty()) {
      auto* item = new QListWidgetItem("No lines yet.", lines_);
      item->setFlags(Qt::NoItemFlags);
      item->setTextAlignment(Qt::AlignCenter);
      return;
    }
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
              [this, i] {
                // The row scatters before setLines() rebuilds the list without it.
                if (QListWidgetItem* it = lines_->item(i))
                  DisintegrateOverlay::overRect(lines_->viewport(), lines_->visualItemRect(it), window());
                emit lineListRemoveRequested(i);
              });

      rl->addWidget(swatch);
      rl->addWidget(label, 1);
      rl->addWidget(rm);

      item->setSizeHint(row->sizeHint());
      lines_->setItemWidget(item, row);
      // Selected rows carry an accent outline (canvas-driven, since selection mode is Off);
      // styleLineRow also handles the canvas-hover tint.
      styleLineRow(i);
    }
    if (prevCurrent >= 0)
      lines_->setCurrentRow(std::min(prevCurrent, lines_->count() - 1));
  }

  // Selected outline > canvas-hover tint > plain. Kept in one place so setCanvasHover can
  // restyle two rows without rebuilding the list (and without scrolling it).
  void SelectionPanel::styleLineRow(int i) {
    if (!lines_ || i < 0 || i >= lines_->count()) return;
    QListWidgetItem* it = lines_->item(i);
    QWidget* w = it ? lines_->itemWidget(it) : nullptr;
    if (!w) return;
    const bool sel = std::find(linesSelected_.begin(), linesSelected_.end(), i) !=
                     linesSelected_.end();
    QString ss;
    if (sel) {
      ss = "background: palette(alternate-base);"
           "border:1px solid palette(highlight);border-radius:5px;";
    } else if (i == canvasHoverLineRow_) {
      // The line under the canvas cursor (browser .lines-row-hover).
      ss = "background: palette(alternate-base);border-radius:5px;";
    }
    // Scope to the row container so the colour chip's own sheet stays untouched.
    w->setStyleSheet(ss);
  }

  void SelectionPanel::setCanvasHover(int pointRow, int lineRow) {
    // Points table: tint the row of the point under the canvas cursor (browser
    // .row-highlighted). setBackground fires itemChanged, so updating_ guards it
    // from reading as a user coordinate edit.
    if (points_ && pointRow != canvasHoverPointRow_) {
      const bool wasUpdating = updating_;
      updating_ = true;
      QColor tint = palette().color(QPalette::Highlight);
      tint.setAlpha(45);
      const auto paintRow = [this, &tint](int r, bool on) {
        if (r < 0 || r >= points_->rowCount()) return;
        for (int c = 0; c < ColCount; ++c)
          if (auto* cell = points_->item(r, c))
            cell->setBackground(on ? QBrush(tint) : QBrush());
      };
      paintRow(canvasHoverPointRow_, false);
      paintRow(pointRow, true);
      canvasHoverPointRow_ = pointRow;
      updating_ = wasUpdating;
    }
    // Lines tab: tint the row of the hovered line. Never scrolls the list.
    if (lines_ && lineRow != canvasHoverLineRow_) {
      const int prev = canvasHoverLineRow_;
      canvasHoverLineRow_ = lineRow;
      styleLineRow(prev);
      styleLineRow(lineRow);
    }
  }

  void SelectionPanel::restyleIcons(const QColor& iconColor) {
    // Delete is a red danger button, so its glyph stays white for contrast; the
    // others follow the theme text color (re-applied on each light/dark switch).
    if (deleteLine_) deleteLine_->setIcon(themedIcon("trash", QColor("#ffffff"), 15));
    if (deselectBtn_) deselectBtn_->setIcon(themedIcon("x", iconColor, 15));
    if (fillClear_) fillClear_->setIcon(themedIcon("x", iconColor, 14));
    // Chevron points toward the edge to hide (›) the panel — back at 0°, since any spin
    // from the last click ended with the panel (and this button) hidden.
    if (collapseBtn_) collapseBtn_->setIcon(themedIcon("chevron-right", iconColor, kToggleGlyph));
    // Re-theme the per-row 🗑 buttons too (new ones in showLine use the stored colour).
    iconColor_ = iconColor;
    if (points_) {
      for (int r = 0; r < points_->rowCount(); ++r)
        if (auto* b = qobject_cast<QPushButton*>(points_->cellWidget(r, ColDel)))
          b->setIcon(themedIcon("trash", iconColor_, 14));
    }
  }

  void SelectionPanel::spinCollapseChevron(qreal fromDeg, qreal toDeg, int ms) {
    if (collapseBtn_) spinIcon(collapseBtn_, "chevron-right", iconColor_, kToggleGlyph, fromDeg, toDeg, ms);
  }

  void SelectionPanel::showEvent(QShowEvent* event) {
    QDockWidget::showEvent(event);
    spinCollapseChevron(0, 0, 0);
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
    // change handlers meanwhile (drawingApp.js:1544-1564). Gating on editorLine
    // (null when nothing is explicitly selected) keeps the editor hidden for the
    // fallback panelLine(), whose mutators all early-return.
    updating_ = true;
    editor_->setVisible(editorLine != nullptr);
    if (editorLine) {
      currentColor_ = QColor(QString::fromStdString(editorLine->color));
      setSwatchColor(colorSwatch_, currentColor_);
      // A line with no point colour of its own shows the colour it actually draws in (its
      // stroke), via core::pointColorOr — not a blank or stale swatch.
      currentPointColor_ =
          QColor(QString::fromStdString(core::pointColorOr(*editorLine)));
      setSwatchColor(pointColorSwatch_, currentPointColor_);
      thickness_->setValue(
          static_cast<int>(std::lround(editorLine->thickness)));
      pointSize_->setValue(
          static_cast<int>(std::lround(editorLine->pointSize)));
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

    // Build the editable points table; `updating_` suppresses itemChanged while
    // cells are set (only a USER edit should fire pointCoordChanged). X/Y editable
    // px, page (cm) read-only, each row ends with 🗑. Mirrors browser coordTable.js.
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
      connect(del, &QPushButton::clicked, this, [this, r] {
        // A QTableWidget row has no widget of its own — scatter its RECT instead.
        const QRect rowRect(0, points_->rowViewportPosition(r),
                            points_->viewport()->width(), points_->rowHeight(r));
        DisintegrateOverlay::overRect(points_->viewport(), rowRect, window());
        emit pointDeleteRequested(r);
      });
      points_->setCellWidget(r, ColDel, del);
    }
    if (selectedPoint >= 0 && selectedPoint < points_->rowCount())
      points_->selectRow(selectedPoint);
    points_->resizeRowsToContents();
    updating_ = false;
  }

  bool SelectionPanel::eventFilter(QObject* obj, QEvent* event) {
    // The cursor left a list entirely → clear its row → canvas hover highlight.
    if (event->type() == QEvent::Leave) {
      if (obj == points_) emit pointRowHovered(-1);
      else if (obj == lines_) emit lineRowHovered(-1);
    }
    if (event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      const bool isDelete =
          ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace;
      if (isDelete && obj == points_ && points_->currentRow() >= 0) {
        emit pointDeleteRequested(points_->currentRow());
        return true;
      }
      // Same key on the Lines tab removes the current line — the row's 🗑 path, and the
      // browser's focused lines-row Delete (drawingApp.js renderLinesList).
      if (isDelete && obj == lines_ && lines_->currentRow() >= 0 &&
          lines_->currentRow() < lines_->count()) {
        emit lineListRemoveRequested(lines_->currentRow());
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }

}
