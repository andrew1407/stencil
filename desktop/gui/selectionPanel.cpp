#include "selectionPanel.hpp"
#include "guiHelpers.hpp"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <cmath>

namespace stencil::gui {

  SelectionPanel::SelectionPanel(QWidget* parent)
      : QDockWidget("Selection", parent) {
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

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
    form->addRow("Thickness:", thickness_);

    // selMarkerSize — drawingApp.js:1547 / :183 (min 1, max 30)
    markerSize_ = new QSpinBox(editor_);
    markerSize_->setRange(1, 30);
    form->addRow("Marker Size:", markerSize_);

    // selStyle — drawingApp.js:1548 / :184
    style_ = new QComboBox(editor_);
    style_->addItem("Solid", "solid");
    style_->addItem("Dashed", "dashed");
    style_->addItem("Dotted", "dotted");
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
    fillClear_ = new QPushButton("✕", fillGroup_);  // selFillClear (✕)
    fillClear_->setToolTip("Clear fill (make transparent)");
    fillRow->addWidget(fillEnabled_);
    fillRow->addWidget(fillSwatch_);
    fillRow->addWidget(fillClear_);
    fillRow->addStretch(1);
    form->addRow("Fill:", fillGroup_);

    // Delete line + selDeselect (drawingApp.js:195 deselectLine).
    auto* btnRow = new QHBoxLayout();
    deleteLine_ = new QPushButton("\U0001F5D1 Delete Line", editor_);
    deselectBtn_ = new QPushButton("✕ Deselect", editor_);  // selDeselect
    btnRow->addWidget(deleteLine_);
    btnRow->addWidget(deselectBtn_);
    form->addRow(btnRow);

    layout->addWidget(editor_);

    layout->addWidget(new QLabel("<b>Points</b>", body));
    points_ = new QListWidget(body);
    points_->setAlternatingRowColors(true);
    points_->installEventFilter(this);
    layout->addWidget(points_, 1);

    layout->addWidget(new QLabel("<b>Measurements</b>", body));
    measurements_ = new QLabel("No selection", body);
    measurements_->setWordWrap(true);
    layout->addWidget(measurements_);

    setWidget(body);

    connect(points_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
      emit pointActivated(points_->row(it));
    });

    // ── inline-editor wiring — each lambda early-returns while showLine is
    // repopulating the controls (updating_), matching the browser which guards
    // via selectedLineIdx and re-sets .value without firing change handlers. ──

    // selColor: open a color dialog, repaint swatch, emit (drawingApp.js:181).
    connect(colorSwatch_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      const QColor c = QColorDialog::getColor(currentColor_, this, "Line color");
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
      const QColor c = QColorDialog::getColor(currentFill_, this, "Area fill color");
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

  void SelectionPanel::showLine(const core::Line* line,
                                const core::Line* editorLine, int selectedPoint,
                                const std::vector<QString>& cmRows) {
    points_->clear();

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

    if (!line || line->points.empty()) {
      measurements_->setText("No selection");
      return;
    }

    for (std::size_t i = 0; i < line->points.size(); ++i) {
      const auto& p = line->points[i];
      // px first (rounded like coordTable.js), then page cm when available so
      // formulas / custom page show here exactly as on the status bar + tooltip.
      QString row = QString("%1.  %2, %3 px")
                        .arg(i + 1)
                        .arg(p.x, 0, 'f', 1)
                        .arg(p.y, 0, 'f', 1);
      if (i < cmRows.size()) row += "   " + cmRows[i] + " cm";
      points_->addItem(row);
    }
    if (selectedPoint >= 0 && selectedPoint < points_->count())
      points_->setCurrentRow(selectedPoint);

    // Total polyline length in pixels (consecutive euclidean distances).
    double total = 0.0;
    for (std::size_t i = 1; i < line->points.size(); ++i) {
      const auto& a = line->points[i - 1];
      const auto& b = line->points[i];
      total += std::hypot(b.x - a.x, b.y - a.y);
    }
    const int segments = line->points.size() > 1
                             ? static_cast<int>(line->points.size()) - 1
                             : 0;
    measurements_->setText(QString("Points: %1\nSegments: %2\nTotal length: %3 px")
                               .arg(line->points.size())
                               .arg(segments)
                               .arg(total, 0, 'f', 2));
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
