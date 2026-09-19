// Every signal the window owns, wired once at construction: the canvas, the toolbars, the chat dock,
// the project bar and the collaboration client all meet here.
#include "mainWindowShellParts.hpp"

namespace stencil::gui {

  void MainWindow::wireSignals() {
    // Dock visibility → toggle sync + provider probe.
    connect(chatDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
      if (visible) { chatClosing_ = false; chatDock_->setClosing(false); }
      if (actChat_ && actChat_->isChecked() != visible) {
        QSignalBlocker b(actChat_);
        actChat_->setChecked(visible);
      }
      if (visible) refreshLlmStatus();
    });
    connect(canvas_, &CanvasWidget::hovered, this, &MainWindow::onHovered);
    connect(canvas_, &CanvasWidget::changed, this, &MainWindow::onCanvasChanged);
    connect(canvas_, &CanvasWidget::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(canvas_, &CanvasWidget::contextRequested, this,
            &MainWindow::showContextMenu);
    // Idle-canvas click grows the blank creator out of the CARD, not the toolbar icon.
    connect(canvas_, &CanvasWidget::blankImageRequested, this, [this] {
      const QRect card = canvas_ ? canvas_->idleCardGlobalRect() : QRect();
      if (card.isValid()) {
        pop_.dialogAnchor.clear();       // the rect below is the origin, not any icon
        pop_.dialogAnchorRect = card;
      }
      openImageDialog(/*startBlank=*/true);
    });
    connect(canvas_, &CanvasWidget::drawingModeChanged, this,
            &MainWindow::refreshActions);
    connect(canvas_, &CanvasWidget::hoverDetail, this,
            &MainWindow::onHoverDetail);
    connect(canvas_, &CanvasWidget::hoverLeft, this,
            [this] { hideHoverTooltip(); });
    connect(canvas_, &CanvasWidget::canvasLeft, this, [this] {
      lastHoverX_ = std::numeric_limits<double>::quiet_NaN();
      lastHoverY_ = std::numeric_limits<double>::quiet_NaN();
      updateStatusIdle();
    });
    connect(canvas_, &CanvasWidget::panBy, this,
            [this](int dx, int dy, bool fast) {
              const double speed = fast ? 2.5 : 1.0;  // drawingApp.js pan speed
              scrollTo(
                  scroll_->horizontalScrollBar()->value() - qRound(dx * speed),
                  scroll_->verticalScrollBar()->value() - qRound(dy * speed));
            });
    connect(canvas_, &CanvasWidget::fitRequested, this, &MainWindow::fitToWindow);
    connect(canvas_, &CanvasWidget::zoomAtCursor, this,
            [this](int dir, const QPoint& posInWidget, bool fast) {
              // Step 0.1 (0.3 with Shift), additive — drawingApp.js wheel.
              const double step = fast ? 0.3 : 0.1;
              const double target = canvas_->scale() + dir * step;
              // posInWidget is canvas-space; the focal math wants viewport coords.
              const QPoint inVp =
                  canvas_->mapTo(scroll_->viewport(), posInWidget);
              setZoomAnchored(target, inVp);
            });
    connect(canvas_, &CanvasWidget::zoomByFactorAt, this,
            [this](double factor, const QPoint& posInWidget) {
              const QPoint inVp = canvas_->mapTo(scroll_->viewport(), posInWidget);
              setZoomAnchored(canvas_->scale() * factor, inVp);
            });
    connect(canvas_, &CanvasWidget::zoomToRect, this,
            [this](const QRectF& r) {
              const QSize vp = scroll_->viewport()->size();
              const auto z = core::rectZoom(r.x(), r.y(), r.width(), r.height(),
                                            vp.width(), vp.height());
              setZoom(z.scale);
              scrollTo(qRound(z.scrollLeft), qRound(z.scrollTop));
            });
    connect(zoom_, &QComboBox::currentTextChanged, this, [this](const QString& t) {
      // syncCombo=false: don't re-write the field being read.
      QString s = t;
      s.remove('%');
      bool ok = false;
      const double pct = s.trimmed().toDouble(&ok);
      if (ok) setZoom(pct / 100.0, false);
    });
    // Index-based: the editable search field mutates the text per keystroke.
    connect(units_.pageSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onPageSizeChanged(); });
    connect(units_.customW, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              // Edited in the active unit; the model stores cm.
              settings_.customPageWidth = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm
              remoteSync_->scheduleRemotePush();  // page format rides the layout — push it to peers
            });
    connect(units_.customH, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              settings_.customPageHeight = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm
              remoteSync_->scheduleRemotePush();
            });
    // The toolbar checkbox is the source of truth; the View action just drives it.
    connect(allowFormulas_, &QCheckBox::toggled, this, [this](bool on) {
      settings_.allowFormulas = on;
      revealControls(formulaGroup_, on);
      if (actAllowFormulas_ && actAllowFormulas_->isChecked() != on) {
        QSignalBlocker ba(actAllowFormulas_);
        actAllowFormulas_->setChecked(on);
      }
      // Expressions are KEPT while disabled so re-enabling restores them.
      if (!on) formulaError_->setVisible(false);
      persistSettings();
      onHovered(lastHoverX_, lastHoverY_);
      onSelectionChanged();  // refresh panel cm when formulas toggle (GAP-2)
      remoteSync_->scheduleRemotePush();  // formulas ride the layout — push to peers
    });
    connect(actAllowFormulas_, &QAction::toggled, this,
            [this](bool on) { allowFormulas_->setChecked(on); });
    // The f(x,y) pair commits on an idle pause (see the timer); Enter / focus-out apply at once.
    formulaCommitTimer_ = new QTimer(this);
    formulaCommitTimer_->setSingleShot(true);
    formulaCommitTimer_->setInterval(FORMULA_COMMIT_MS);
    connect(formulaCommitTimer_, &QTimer::timeout, this, [this] { validateAndApplyFormulas(); });
    const auto onFormulaEdited = [this](const QString&) {
      // A wrong expression is only flagged once typing stops.
      const bool okX = core::FormulaParser::validate(formulaX_->text().trimmed().toStdString(), 'x');
      const bool okY = core::FormulaParser::validate(formulaY_->text().trimmed().toStdString(), 'y');
      if (okX && okY) formulaError_->setVisible(false);
      formulaCommitTimer_->start();
    };
    connect(formulaX_, &QLineEdit::textChanged, this, onFormulaEdited);
    connect(formulaY_, &QLineEdit::textChanged, this, onFormulaEdited);
    connect(formulaX_, &QLineEdit::editingFinished, this, [this] { validateAndApplyFormulas(); });
    connect(formulaY_, &QLineEdit::editingFinished, this, [this] { validateAndApplyFormulas(); });
    connect(selPanel_, &SelectionPanel::pointActivated, this,
            [this](int i) { canvas_->selectPoint(i); });
    connect(selPanel_, &SelectionPanel::pointDeleteRequested, this,
            [this](int i) { canvas_->deletePoint(i); });
    connect(selPanel_, &SelectionPanel::pointCoordChanged, this,
            [this](int i, int axis, double v) { canvas_->setPointCoord(i, axis, v); });

    // Hover cross-highlight, both directions (browser parity); never scrolls a list.
    connect(selPanel_, &SelectionPanel::pointRowHovered, this,
            [this](int i) { canvas_->setListHoverPoint(i); });
    connect(selPanel_, &SelectionPanel::lineRowHovered, this,
            [this](int i) { canvas_->setListHoverLine(i); });
    connect(canvas_, &CanvasWidget::canvasHoverChanged, this,
            [this](int lineIdx, int ptIdx, int overLineIdx) {
              const bool onPanelLine = ptIdx >= 0 && lineIdx == canvas_->panelLineIdx();
              selPanel_->setCanvasHover(onPanelLine ? ptIdx : -1, overLineIdx);
            });

    // "Selected Line:" bar → canvas mutators — drawingApp.js:181-195; no Delete here (browser parity).
    connect(selectedLineBar_, &SelectedLineBar::lineColorChanged, this,
            [this](const QString& v, bool preview) { canvas_->setSelectedLineColor(v, preview); });
    connect(selectedLineBar_, &SelectedLineBar::linePointColorChanged, this,
            [this](const QString& v, bool preview) { canvas_->setSelectedLinePointColor(v, preview); });
    connect(selectedLineBar_, &SelectedLineBar::lineThicknessChanged, this,
            [this](int t) { canvas_->setSelectedLineThickness(t); });
    connect(selectedLineBar_, &SelectedLineBar::linePointSizeChanged, this,
            [this](int m) { canvas_->setSelectedLinePointSize(m); });
    connect(selectedLineBar_, &SelectedLineBar::lineStyleChanged, this,
            [this](const QString& s) { canvas_->setSelectedLineStyle(s); });
    connect(selectedLineBar_, &SelectedLineBar::lineFillChanged, this,
            [this](const QString& v, bool preview) { canvas_->setSelectedLineFill(v, preview); });
    connect(selectedLineBar_, &SelectedLineBar::unchainRequested, this,
            [this] { canvas_->unchainSelectedLine(); });
    connect(selectedLineBar_, &SelectedLineBar::deselectRequested, this,
            [this] { canvas_->deselect(); });
    connect(canvas_, &CanvasWidget::statusMessage, this,
            [this](const QString& text) { notify_->success(text); });
    // Routes through actPanel_ so the View menu / Alt+X stay in sync.
    connect(selPanel_, &SelectionPanel::collapseRequested, this,
            [this] { if (actPanel_) actPanel_->setChecked(false); });
    selPanel_->setToggleHint(hotkey("togglePointsList", "Alt+X"));   // shortcut in the chevron tooltip

    // Lines tab → index-keyed selection (Ctrl/⌘+Shift toggles multi-select) and removal.
    connect(selPanel_, &SelectionPanel::lineListActivated, this,
            [this](int idx, bool multi) {
              if (multi) canvas_->toggleLineSelectionByIndex(idx);
              else canvas_->selectLineByIndex(idx);
            });
    connect(selPanel_, &SelectionPanel::lineListRemoveRequested, this,
            [this](int idx) { canvas_->removeLineByIndex(idx); });
  }
}  // namespace stencil::gui
