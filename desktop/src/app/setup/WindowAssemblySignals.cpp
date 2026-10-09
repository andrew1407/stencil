// Every signal the window owns, wired once at construction: the canvas, the toolbars, the chat dock,
// the project bar and the collaboration client all meet here.
#include "MainWindow.hpp"
#include "../../support/control/dblReset.hpp"
#include "ChatSessionController.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSyncController.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "mainWindowShared.hpp"
#include "zoomPan.hpp"
#include "../../support/control/reveal/controlReveal.hpp"   // a group of fields comes and goes as sand

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QVariant>
#include <QLineEdit>
#include <QLabel>
#include <QDockWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <memory>

namespace stencil::gui {

  void WindowAssembly::wireSignals() {
    // Dock visibility → toggle sync + provider probe.
    QObject::connect(w.chatDock, &QDockWidget::visibilityChanged, &w, [this](bool visible) {
      if (visible) { w.parts.dockChrome.chatClosing = false; w.chatDock->setClosing(false); }
      if (w.acts.chat && w.acts.chat->isChecked() != visible) {
        QSignalBlocker b(w.acts.chat);
        w.acts.chat->setChecked(visible);
      }
      if (visible) w.chatSession->refreshLlmStatus();
    });
    QObject::connect(w.canvas, &CanvasWidget::hovered, &w, [this](double x, double y) { w.parts.view.onHovered(x, y); });
    QObject::connect(w.canvas, &CanvasWidget::changed, &w, &MainWindow::onCanvasChanged);
    // An undone filter lands as a pick does — controls, render, persistence — minus the step.
    QObject::connect(w.canvas, &CanvasWidget::filterRestored, &w,
                     [this](const QString& mode, const QColor& tint) {
                       w.applyTintColor(tint, /*asUndoStep=*/false);
                       w.applyImageFilter(mode, false);
                     });
    QObject::connect(w.canvas, &CanvasWidget::selectionChanged, &w,
                     &MainWindow::onSelectionChanged);
    QObject::connect(w.canvas, &CanvasWidget::contextRequested, &w,
                     [this](const QPoint& globalPos) { w.parts.canvasMenu.showContextMenu(globalPos); });
    // Idle-canvas click grows the blank creator out of the CARD, not the toolbar icon.
    QObject::connect(w.canvas, &CanvasWidget::blankImageRequested, &w, [this] {
      const QRect card = w.canvas ? w.canvas->idleCardGlobalRect() : QRect();
      if (card.isValid()) {
        w.pop.dialogAnchor.clear();       // the rect below is the origin, not any icon
        w.pop.dialogAnchorRect = card;
      }
      w.parts.sourceOpener.openImageDialog(/*startBlank=*/true);
    });
    QObject::connect(w.canvas, &CanvasWidget::drawingModeChanged, &w, [this](bool drawing) {
      w.refreshActions();
      if (drawing) w.parts.view.showLinesForDrawing();
    });
    QObject::connect(w.canvas, &CanvasWidget::hoverDetail, &w,
                     [this](double x, double y, const QPoint& globalPos, Qt::KeyboardModifiers mods,
                            bool immediate) { w.parts.hoverTip.onHoverDetail(x, y, globalPos, mods, immediate); });
    QObject::connect(w.canvas, &CanvasWidget::hoverLeft, &w,
                     [this] { w.parts.hoverTip.hideHoverTooltip(); });
    QObject::connect(w.canvas, &CanvasWidget::canvasLeft, &w, [this] {
      w.parts.view.lastHoverX = w.parts.view.lastHoverY = std::numeric_limits<double>::quiet_NaN();
      w.updateStatusIdle();
    });
    QObject::connect(w.canvas, &CanvasWidget::panBy, &w,
                     [this](int dx, int dy, bool fast) {
                       const double speed = fast ? 2.5 : 1.0;  // drawingApp.js pan speed
                       w.parts.view.scrollTo(
                           w.scroll->horizontalScrollBar()->value() - qRound(dx * speed),
                           w.scroll->verticalScrollBar()->value() - qRound(dy * speed));
                     });
    QObject::connect(w.canvas, &CanvasWidget::fitRequested, &w, &MainWindow::fitToWindow);
    QObject::connect(w.canvas, &CanvasWidget::zoomAtCursor, &w,
                     [this](int dir, const QPoint& posInWidget, bool fast) {
                       // Step 0.1 (0.3 with Shift), additive — drawingApp.js wheel.
                       const double step = fast ? 0.3 : 0.1;
                       const double target = w.canvas->getScale() + dir * step;
                       // posInWidget is canvas-space; the focal math wants viewport coords.
                       const QPoint inVp =
                           w.canvas->mapTo(w.scroll->viewport(), posInWidget);
                       w.setZoomAnchored(target, inVp);
                     });
    QObject::connect(w.canvas, &CanvasWidget::zoomByFactorAt, &w,
                     [this](double factor, const QPoint& posInWidget) {
                       const QPoint inVp = w.canvas->mapTo(w.scroll->viewport(), posInWidget);
                       w.setZoomAnchored(w.canvas->getScale() * factor, inVp);
                     });
    QObject::connect(w.canvas, &CanvasWidget::zoomToRect, &w,
                     [this](const QRectF& r) {
                       const QSize vp = w.scroll->viewport()->size();
                       const auto z = core::rectZoom(r.x(), r.y(), r.width(), r.height(),
                                                     vp.width(), vp.height());
                       w.setZoom(z.scale);
                       w.parts.view.scrollTo(qRound(z.scrollLeft), qRound(z.scrollTop));
                     });
    QObject::connect(w.zoom, &QComboBox::currentTextChanged, &w, [this](const QString& t) {
      // syncCombo=false: don't re-write the field being read.
      QString s = t;
      s.remove('%');
      bool ok = false;
      const double pct = s.trimmed().toDouble(&ok);
      if (ok) w.setZoom(pct / 100.0, false);
    });
    // Index-based: the editable search field mutates the text per keystroke.
    QObject::connect(w.units.pageSize, QOverload<int>::of(&QComboBox::currentIndexChanged), &w,
                     [this](int) { w.parts.view.onPageSizeChanged(); });
    QObject::connect(w.units.customW, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &w,
                     [this](double v) {
                       // Edited in the active unit; the model stores cm.
                       w.settings.customPageWidth = v / w.unitFormat().factor;
                       w.persistSettings();
                       w.parts.view.onHovered(w.parts.view.lastHoverX, w.parts.view.lastHoverY);
                       w.onSelectionChanged();  // refresh panel cm
                       w.remoteSync->scheduleRemotePush();  // page format rides the layout — push it to peers
                     });
    QObject::connect(w.units.customH, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &w,
                     [this](double v) {
                       w.settings.customPageHeight = v / w.unitFormat().factor;
                       w.persistSettings();
                       w.parts.view.onHovered(w.parts.view.lastHoverX, w.parts.view.lastHoverY);
                       w.onSelectionChanged();  // refresh panel cm
                       w.remoteSync->scheduleRemotePush();
                     });
    // The toolbar checkbox is the source of truth; the View action just drives it.
    QObject::connect(w.tools.allowFormulas, &QCheckBox::toggled, &w, [this](bool on) {
      w.settings.allowFormulas = on;
      revealControls(w.tools.formulaGroup, on);
      if (w.acts.allowFormulas && w.acts.allowFormulas->isChecked() != on) {
        QSignalBlocker ba(w.acts.allowFormulas);
        w.acts.allowFormulas->setChecked(on);
      }
      // Expressions are KEPT while disabled so re-enabling restores them.
      if (!on) w.tools.formulaError->setVisible(false);
      w.persistSettings();
      w.parts.view.onHovered(w.parts.view.lastHoverX, w.parts.view.lastHoverY);
      w.onSelectionChanged();  // refresh panel cm when formulas toggle (GAP-2)
      w.remoteSync->scheduleRemotePush();  // formulas ride the layout — push to peers
    });
    QObject::connect(w.acts.allowFormulas, &QAction::toggled, &w,
                     [this](bool on) { w.tools.allowFormulas->setChecked(on); });
    // The f(x,y) pair commits on an idle pause (see the timer); Enter / focus-out apply at once.
    w.tools.formulaCommitTimer = new QTimer(&w);
    w.tools.formulaCommitTimer->setSingleShot(true);
    w.tools.formulaCommitTimer->setInterval(FORMULA_COMMIT_MS);
    QObject::connect(w.tools.formulaCommitTimer, &QTimer::timeout, &w, [this] { w.parts.view.validateAndApplyFormulas(); });
    const auto onFormulaEdited = [this](const QString&) {
      // A wrong expression is only flagged once typing stops.
      const core::FormulaContext ctx = w.parts.view.formulaContext();
      const bool okX = core::FormulaParser::validate(w.tools.formulaX->text().trimmed().toStdString(), ctx);
      const bool okY = core::FormulaParser::validate(w.tools.formulaY->text().trimmed().toStdString(), ctx);
      if (okX && okY) w.tools.formulaError->setVisible(false);
      w.tools.formulaCommitTimer->start();
    };
    QObject::connect(w.tools.formulaX, &QLineEdit::textChanged, &w, onFormulaEdited);
    QObject::connect(w.tools.formulaY, &QLineEdit::textChanged, &w, onFormulaEdited);
    QObject::connect(w.tools.formulaX, &QLineEdit::editingFinished, &w, [this] { w.parts.view.validateAndApplyFormulas(); });
    QObject::connect(w.tools.formulaY, &QLineEdit::editingFinished, &w, [this] { w.parts.view.validateAndApplyFormulas(); });
    QObject::connect(w.selPanel, &SelectionPanel::pointActivated, &w,
                     [this](int i) { w.canvas->selectPoint(i); });
    QObject::connect(w.selPanel, &SelectionPanel::pointDeleteRequested, &w,
                     [this](int i) { w.canvas->deletePoint(i); });
    QObject::connect(w.selPanel, &SelectionPanel::pointCoordChanged, &w,
                     [this](int i, int axis, double v) { w.canvas->setPointCoord(i, axis, v); });

    // Hover cross-highlight, both directions (browser parity); never scrolls a list.
    QObject::connect(w.selPanel, &SelectionPanel::pointRowHovered, &w,
                     [this](int i) { w.canvas->setListHoverPoint(i); });
    QObject::connect(w.selPanel, &SelectionPanel::lineRowHovered, &w,
                     [this](int i) { w.canvas->setListHoverLine(i); });
    QObject::connect(w.canvas, &CanvasWidget::canvasHoverChanged, &w,
                     [this](int lineIdx, int ptIdx, int overLineIdx) {
                       const bool onPanelLine = ptIdx >= 0 && lineIdx == w.canvas->panelLineIdx();
                       w.selPanel->setCanvasHover(onPanelLine ? ptIdx : -1, overLineIdx);
                     });

    // "Selected Line:" bar → canvas mutators — drawingApp.js:181-195; no Delete here (browser parity).
    QObject::connect(w.selectedLineBar, &SelectedLineBar::lineColorChanged, &w,
                     [this](const QString& v, bool preview) { w.canvas->setSelectedLineColor(v, preview); });
    QObject::connect(w.selectedLineBar, &SelectedLineBar::linePointColorChanged, &w,
                     [this](const QString& v, bool preview) { w.canvas->setSelectedLinePointColor(v, preview); });
    QObject::connect(w.selectedLineBar, &SelectedLineBar::lineThicknessChanged, &w,
                     [this](int t) { w.canvas->setSelectedLineThickness(t); });
    QObject::connect(w.selectedLineBar, &SelectedLineBar::linePointSizeChanged, &w,
                     [this](int m) { w.canvas->setSelectedLinePointSize(m); });
    QObject::connect(w.selectedLineBar, &SelectedLineBar::lineStyleChanged, &w,
                     [this](const QString& s) { w.canvas->setSelectedLineStyle(s); });
    QObject::connect(w.selectedLineBar, &SelectedLineBar::lineFillChanged, &w,
                     [this](const QString& v, bool preview) { w.canvas->setSelectedLineFill(v, preview); });
    QObject::connect(w.selectedLineBar, &SelectedLineBar::unchainRequested, &w,
                     [this] { w.canvas->unchainSelectedLine(); });
    QObject::connect(w.selectedLineBar, &SelectedLineBar::deselectRequested, &w,
                     [this] { w.canvas->deselect(); });
    QObject::connect(w.canvas, &CanvasWidget::statusMessage, &w,
                     [this](const QString& text) { w.notify->success(text); });
    // Routes through acts.panel so the View menu / Alt+X stay in sync.
    QObject::connect(w.selPanel, &SelectionPanel::collapseRequested, &w,
                     [this] { if (w.acts.panel) w.acts.panel->setChecked(false); });
    w.selPanel->setToggleHint(w.keys.value("togglePointsList", "Alt+X"));   // shortcut in the chevron tooltip

    // Lines tab → index-keyed selection (Ctrl/⌘+Shift toggles multi-select) and removal.
    QObject::connect(w.selPanel, &SelectionPanel::lineListActivated, &w,
                     [this](int idx, bool multi) {
                       if (multi) w.canvas->toggleLineSelectionByIndex(idx);
                       else w.canvas->selectLineByIndex(idx);
                     });
    QObject::connect(w.selPanel, &SelectionPanel::lineListRemoveRequested, &w,
                     [this](int idx) { w.canvas->removeLineByIndex(idx); });
    wireLineRows();
    // What a double-click or a logo drop puts each toolbar control back to (support/control/dblReset.hpp).
    const Settings d;
    for (const auto& [control, v] : std::initializer_list<std::pair<QWidget*, QVariant>>{
             {w.tools.imageFilter, d.imageFilter}, {w.tools.lineStyle, d.defaultStyle}, {w.tools.compareCombo, QStringLiteral("none")},
             {w.units.unitCombo, localeDefaultUnit()}, {w.units.pageSize, d.pageSize}, {w.tools.showPointsCheck, d.showPoints},
             {w.tools.showLinesCheck, d.showLines}, {w.tools.allowFormulas, d.allowFormulas},
             {w.tools.lineThickness, int(d.defaultThickness)}, {w.tools.pointSize, int(d.defaultPointSize)},
             {w.tools.formulaX, d.formulaX}, {w.tools.formulaY, d.formulaY}})
      support::setResetDefault(control, v);
  }
}  // namespace stencil::gui
