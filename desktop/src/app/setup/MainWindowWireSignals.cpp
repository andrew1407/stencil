// Every signal the window owns, wired once at construction: the canvas, the toolbars, the chat dock,
// the project bar and the collaboration client all meet here.
#include "mainWindowShellParts.hpp"

namespace stencil::gui {

  void MainWindow::wireSignals() {
    // Dock visibility → toggle sync + provider probe.
    connect(chatDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
      if (visible) { chatClosing = false; chatDock->setClosing(false); }
      if (actChat && actChat->isChecked() != visible) {
        QSignalBlocker b(actChat);
        actChat->setChecked(visible);
      }
      if (visible) refreshLlmStatus();
    });
    connect(canvas, &CanvasWidget::hovered, this, &MainWindow::onHovered);
    connect(canvas, &CanvasWidget::changed, this, &MainWindow::onCanvasChanged);
    connect(canvas, &CanvasWidget::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(canvas, &CanvasWidget::contextRequested, this,
            &MainWindow::showContextMenu);
    // Idle-canvas click grows the blank creator out of the CARD, not the toolbar icon.
    connect(canvas, &CanvasWidget::blankImageRequested, this, [this] {
      const QRect card = canvas ? canvas->idleCardGlobalRect() : QRect();
      if (card.isValid()) {
        pop.dialogAnchor.clear();       // the rect below is the origin, not any icon
        pop.dialogAnchorRect = card;
      }
      openImageDialog(/*startBlank=*/true);
    });
    connect(canvas, &CanvasWidget::drawingModeChanged, this,
            &MainWindow::refreshActions);
    connect(canvas, &CanvasWidget::hoverDetail, this,
            &MainWindow::onHoverDetail);
    connect(canvas, &CanvasWidget::hoverLeft, this,
            [this] { hideHoverTooltip(); });
    connect(canvas, &CanvasWidget::canvasLeft, this, [this] {
      lastHoverX = std::numeric_limits<double>::quiet_NaN();
      lastHoverY = std::numeric_limits<double>::quiet_NaN();
      updateStatusIdle();
    });
    connect(canvas, &CanvasWidget::panBy, this,
            [this](int dx, int dy, bool fast) {
              const double speed = fast ? 2.5 : 1.0;  // drawingApp.js pan speed
              scrollTo(
                  scroll->horizontalScrollBar()->value() - qRound(dx * speed),
                  scroll->verticalScrollBar()->value() - qRound(dy * speed));
            });
    connect(canvas, &CanvasWidget::fitRequested, this, &MainWindow::fitToWindow);
    connect(canvas, &CanvasWidget::zoomAtCursor, this,
            [this](int dir, const QPoint& posInWidget, bool fast) {
              // Step 0.1 (0.3 with Shift), additive — drawingApp.js wheel.
              const double step = fast ? 0.3 : 0.1;
              const double target = canvas->getScale() + dir * step;
              // posInWidget is canvas-space; the focal math wants viewport coords.
              const QPoint inVp =
                  canvas->mapTo(scroll->viewport(), posInWidget);
              setZoomAnchored(target, inVp);
            });
    connect(canvas, &CanvasWidget::zoomByFactorAt, this,
            [this](double factor, const QPoint& posInWidget) {
              const QPoint inVp = canvas->mapTo(scroll->viewport(), posInWidget);
              setZoomAnchored(canvas->getScale() * factor, inVp);
            });
    connect(canvas, &CanvasWidget::zoomToRect, this,
            [this](const QRectF& r) {
              const QSize vp = scroll->viewport()->size();
              const auto z = core::rectZoom(r.x(), r.y(), r.width(), r.height(),
                                            vp.width(), vp.height());
              setZoom(z.scale);
              scrollTo(qRound(z.scrollLeft), qRound(z.scrollTop));
            });
    connect(zoom, &QComboBox::currentTextChanged, this, [this](const QString& t) {
      // syncCombo=false: don't re-write the field being read.
      QString s = t;
      s.remove('%');
      bool ok = false;
      const double pct = s.trimmed().toDouble(&ok);
      if (ok) setZoom(pct / 100.0, false);
    });
    // Index-based: the editable search field mutates the text per keystroke.
    connect(units.pageSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onPageSizeChanged(); });
    connect(units.customW, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              // Edited in the active unit; the model stores cm.
              settings.customPageWidth = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX, lastHoverY);
              onSelectionChanged();  // refresh panel cm
              remoteSync->scheduleRemotePush();  // page format rides the layout — push it to peers
            });
    connect(units.customH, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              settings.customPageHeight = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX, lastHoverY);
              onSelectionChanged();  // refresh panel cm
              remoteSync->scheduleRemotePush();
            });
    // The toolbar checkbox is the source of truth; the View action just drives it.
    connect(allowFormulas, &QCheckBox::toggled, this, [this](bool on) {
      settings.allowFormulas = on;
      revealControls(formulaGroup, on);
      if (actAllowFormulas && actAllowFormulas->isChecked() != on) {
        QSignalBlocker ba(actAllowFormulas);
        actAllowFormulas->setChecked(on);
      }
      // Expressions are KEPT while disabled so re-enabling restores them.
      if (!on) formulaError->setVisible(false);
      persistSettings();
      onHovered(lastHoverX, lastHoverY);
      onSelectionChanged();  // refresh panel cm when formulas toggle (GAP-2)
      remoteSync->scheduleRemotePush();  // formulas ride the layout — push to peers
    });
    connect(actAllowFormulas, &QAction::toggled, this,
            [this](bool on) { allowFormulas->setChecked(on); });
    // The f(x,y) pair commits on an idle pause (see the timer); Enter / focus-out apply at once.
    formulaCommitTimer = new QTimer(this);
    formulaCommitTimer->setSingleShot(true);
    formulaCommitTimer->setInterval(FORMULA_COMMIT_MS);
    connect(formulaCommitTimer, &QTimer::timeout, this, [this] { validateAndApplyFormulas(); });
    const auto onFormulaEdited = [this](const QString&) {
      // A wrong expression is only flagged once typing stops.
      const bool okX = core::FormulaParser::validate(formulaX->text().trimmed().toStdString(), 'x');
      const bool okY = core::FormulaParser::validate(formulaY->text().trimmed().toStdString(), 'y');
      if (okX && okY) formulaError->setVisible(false);
      formulaCommitTimer->start();
    };
    connect(formulaX, &QLineEdit::textChanged, this, onFormulaEdited);
    connect(formulaY, &QLineEdit::textChanged, this, onFormulaEdited);
    connect(formulaX, &QLineEdit::editingFinished, this, [this] { validateAndApplyFormulas(); });
    connect(formulaY, &QLineEdit::editingFinished, this, [this] { validateAndApplyFormulas(); });
    connect(selPanel, &SelectionPanel::pointActivated, this,
            [this](int i) { canvas->selectPoint(i); });
    connect(selPanel, &SelectionPanel::pointDeleteRequested, this,
            [this](int i) { canvas->deletePoint(i); });
    connect(selPanel, &SelectionPanel::pointCoordChanged, this,
            [this](int i, int axis, double v) { canvas->setPointCoord(i, axis, v); });

    // Hover cross-highlight, both directions (browser parity); never scrolls a list.
    connect(selPanel, &SelectionPanel::pointRowHovered, this,
            [this](int i) { canvas->setListHoverPoint(i); });
    connect(selPanel, &SelectionPanel::lineRowHovered, this,
            [this](int i) { canvas->setListHoverLine(i); });
    connect(canvas, &CanvasWidget::canvasHoverChanged, this,
            [this](int lineIdx, int ptIdx, int overLineIdx) {
              const bool onPanelLine = ptIdx >= 0 && lineIdx == canvas->panelLineIdx();
              selPanel->setCanvasHover(onPanelLine ? ptIdx : -1, overLineIdx);
            });

    // "Selected Line:" bar → canvas mutators — drawingApp.js:181-195; no Delete here (browser parity).
    connect(selectedLineBar, &SelectedLineBar::lineColorChanged, this,
            [this](const QString& v, bool preview) { canvas->setSelectedLineColor(v, preview); });
    connect(selectedLineBar, &SelectedLineBar::linePointColorChanged, this,
            [this](const QString& v, bool preview) { canvas->setSelectedLinePointColor(v, preview); });
    connect(selectedLineBar, &SelectedLineBar::lineThicknessChanged, this,
            [this](int t) { canvas->setSelectedLineThickness(t); });
    connect(selectedLineBar, &SelectedLineBar::linePointSizeChanged, this,
            [this](int m) { canvas->setSelectedLinePointSize(m); });
    connect(selectedLineBar, &SelectedLineBar::lineStyleChanged, this,
            [this](const QString& s) { canvas->setSelectedLineStyle(s); });
    connect(selectedLineBar, &SelectedLineBar::lineFillChanged, this,
            [this](const QString& v, bool preview) { canvas->setSelectedLineFill(v, preview); });
    connect(selectedLineBar, &SelectedLineBar::unchainRequested, this,
            [this] { canvas->unchainSelectedLine(); });
    connect(selectedLineBar, &SelectedLineBar::deselectRequested, this,
            [this] { canvas->deselect(); });
    connect(canvas, &CanvasWidget::statusMessage, this,
            [this](const QString& text) { notify->success(text); });
    // Routes through actPanel so the View menu / Alt+X stay in sync.
    connect(selPanel, &SelectionPanel::collapseRequested, this,
            [this] { if (actPanel) actPanel->setChecked(false); });
    selPanel->setToggleHint(hotkey("togglePointsList", "Alt+X"));   // shortcut in the chevron tooltip

    // Lines tab → index-keyed selection (Ctrl/⌘+Shift toggles multi-select) and removal.
    connect(selPanel, &SelectionPanel::lineListActivated, this,
            [this](int idx, bool multi) {
              if (multi) canvas->toggleLineSelectionByIndex(idx);
              else canvas->selectLineByIndex(idx);
            });
    connect(selPanel, &SelectionPanel::lineListRemoveRequested, this,
            [this](int idx) { canvas->removeLineByIndex(idx); });
  }
}  // namespace stencil::gui
