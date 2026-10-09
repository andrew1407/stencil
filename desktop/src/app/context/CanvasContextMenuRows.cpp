// The rest of the canvas context menu's persistent actions, and syncing them to the editor before
// the menu opens. The build chain starts in CanvasContextMenu.cpp.
#include "MainWindow.hpp"
#include <QScrollArea>
#include "CanvasContextMenu.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "MenuRowReveal.hpp"
#include <QActionGroup>
#include <QCheckBox>
#include <QButtonGroup>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWidgetAction>

namespace stencil::gui {

  void CanvasContextMenu::buildDrawNowActions() {
    // contextMenu.js ctx-draw-line/ctx-draw-rect: two fixed actions, each with its own outline glyph (browser parity).
    w.ctxMenu.drawLineNow = new QAction("Draw Line", &w);
    QObject::connect(w.ctxMenu.drawLineNow, &QAction::triggered, &w, [this] {
      if (!w.canvas->hasImage()) {
        w.notify->error("Load an image first");
        return;
      }
      w.canvas->setDrawMode(CanvasWidget::DrawMode::LINE);
      w.canvas->startDrawingMode();  // continues the selected line if one is set
      w.notify->info("Drag to draw a line");
    });

    w.ctxMenu.drawRectNow = new QAction("Draw Rectangle", &w);
    QObject::connect(w.ctxMenu.drawRectNow, &QAction::triggered, &w, [this] {
      if (!w.canvas->hasImage()) {
        w.notify->error("Load an image first");
        return;
      }
      w.canvas->setDrawMode(CanvasWidget::DrawMode::RECT);
      w.canvas->startDrawingMode();  // continues the selected line if one is set
      w.notify->info("Drag to draw a rectangle");
    });
  }

  void CanvasContextMenu::buildContextTooltipActions() {
    // contextMenu.js:96-107, 546-557. Real QCheckBoxes in QWidgetActions so a click flips them WITHOUT dismissing the menu.
    // The enable toggle mirrors the View-menu acts.tooltip.
    addContextCheckRow("Show Tooltips", w.settings.tooltipEnabled, w.ctxMenu.tooltipEnableCheck, w.ctxMenu.tooltipEnable);
    QObject::connect(w.ctxMenu.tooltipEnableCheck, &QCheckBox::toggled, &w, [this](bool on) {
      w.settings.tooltipEnabled = on;
      {
        QSignalBlocker b(w.acts.tooltip);
        w.acts.tooltip->setChecked(on);  // keep the View-menu item in lock-step
      }
      w.persistSettings();
      if (!on)
        w.overlays.tooltip->hide();
      else if (!QApplication::activePopupWidget())
        w.parts.view.onHovered(w.parts.view.lastHoverX, w.parts.view.lastHoverY);  // re-show at the current hover (not while the menu's up)
    });
    // Binding `backing` to the settings field keeps the toggles in sync with the source of truth.
    auto mkRowToggle = [this](const QString& text, bool& backing,
                                           QCheckBox*& box, QWidgetAction*& act) {
      addContextCheckRow(text, backing, box, act);
      QObject::connect(box, &QCheckBox::toggled, &w, [this, &backing](bool on) {
        backing = on;
        w.persistSettings();
        // Showing the tooltip window while the menu is up would steal the popup's grab and dismiss it.
        if (!QApplication::activePopupWidget()) w.parts.view.onHovered(w.parts.view.lastHoverX, w.parts.view.lastHoverY);
      });
    };
    mkRowToggle("Page (cm)", w.settings.tooltipShowPage, w.ctxMenu.ttPageCheck, w.ctxMenu.ttPage);
    mkRowToggle("Screen (px)", w.settings.tooltipShowScreen, w.ctxMenu.ttScreenCheck, w.ctxMenu.ttScreen);
    mkRowToggle("To Edge (cm)", w.settings.tooltipShowCoords, w.ctxMenu.ttCoordsCheck, w.ctxMenu.ttCoords);

    // contextMenu.js:84-100. Twins of the toolbar formula widgets — edits here drive those, so the validate/apply/persist pipeline runs unchanged.
    addContextCheckRow("Allow Formulas", w.settings.allowFormulas, w.ctxMenu.allowFormulas, w.ctxMenu.allowFormulasAct);
    QObject::connect(w.ctxMenu.allowFormulas, &QCheckBox::toggled, &w, [this](bool on) {
      w.tools.allowFormulas->setChecked(on);   // the canonical toolbar handler does settings/persist/apply
      revealMenuRows({w.ctxMenu.formulaXAct, w.ctxMenu.formulaYAct}, on);
    });
    auto mkFormulaRow = [this](const QString& label, const QString& placeholder,
                                             QLineEdit*& edit, QWidgetAction*& act) {
      auto* lay = makeContextMenuRow(act, 2, 4);
      auto* host = lay->parentWidget();
      lay->addWidget(new QLabel(label, host));
      edit = new QLineEdit(host);
      edit->setPlaceholderText(placeholder);
      edit->setFixedWidth(150);
      lay->addStretch(1);
      lay->addWidget(edit);
    };
    mkFormulaRow("x(x)=", "e.g. x + 9", w.ctxMenu.formulaX, w.ctxMenu.formulaXAct);
    mkFormulaRow("y(y)=", "e.g. (y-7)*4", w.ctxMenu.formulaY, w.ctxMenu.formulaYAct);
    // Mirror into the canonical toolbar inputs (guarded against a feedback loop).
    QObject::connect(w.ctxMenu.formulaX, &QLineEdit::textChanged, &w, [this](const QString& t) {
      if (w.tools.formulaX->text() != t) w.tools.formulaX->setText(t);
    });
    QObject::connect(w.ctxMenu.formulaY, &QLineEdit::textChanged, &w, [this](const QString& t) {
      if (w.tools.formulaY->text() != t) w.tools.formulaY->setText(t);
    });
  }

  void CanvasContextMenu::buildUnitActions() {
    // Units: cm | inches; the custom page spinboxes stay backed by cm internally.
    auto* unitGroup = new QActionGroup(&w);
    unitGroup->setExclusive(true);
    auto mkUnit = [this, unitGroup](const QString& text, const QString& code) {
      auto* a = new QAction(text, &w);
      a->setCheckable(true);
      a->setChecked(w.settings.units == code);
      unitGroup->addAction(a);
      QObject::connect(a, &QAction::toggled, &w, [this, code](bool on) {
        if (on) w.parts.view.applyUnits(code);
      });
      return a;
    };
    w.units.unitCm = mkUnit("Centimeters (cm)", "cm");
    w.units.unitIn = mkUnit("Inches (in)", "in");
  }

  // Mirrors browser/js/ui/contextMenu/contextMenu.js grouping, reusing the shared QActions.
  void CanvasContextMenu::showContextMenuFromKeyboard() {
    if (!w.scroll) return;
    const QWidget* vp = w.scroll->viewport();
    const QRect vpGlobal(vp->mapToGlobal(QPoint(0, 0)), vp->size());
    const QPoint cursor = QCursor::pos();
    showContextMenu(vpGlobal.contains(cursor) ? cursor : vpGlobal.center());
  }

  // Live-sync the submenu state before exec — contextMenu.js:239-297 syncState().
  void CanvasContextMenu::syncContextActions() {
    const bool hasImg = w.canvas->hasImage();
    const bool hasLines = !w.canvas->allLines().empty();
    w.acts.fullscreen->setText(w.isFullScreen() ? QStringLiteral("Exit Fullscreen")
                                           : QStringLiteral("Enter Fullscreen"));
    w.setActionTip(w.acts.fullscreen, w.isFullScreen() ? "Exit fullscreen" : "Fullscreen mode");

    // contextMenu.js:254-264
    w.parts.exportMenus.syncExportActions();
    w.acts.pasteImage->setEnabled(true);  // dispatch notifies "Load an image first"
    w.acts.copyLayout->setEnabled(hasLines);
    w.acts.downloadJson->setEnabled(hasLines);
    w.acts.pasteLayout->setEnabled(hasImg);
    w.acts.uploadJson->setEnabled(hasImg);
    w.acts.saveProjectFile->setEnabled(hasImg);

    // contextMenu.js:274-276; blocked so seeding doesn't re-fire handlers.
    {
      QSignalBlocker bm(w.ctxMenu.pointSpin), bt(w.ctxMenu.thickSpin);
      w.ctxMenu.pointSpin->setValue(w.settings.defaultPointSize);
      w.ctxMenu.thickSpin->setValue(w.settings.defaultThickness);
    }
    for (QAction* a : w.ctxMenu.lineStyleGroup->actions())
      a->setChecked(a->data().toString() == w.settings.defaultStyle);

    // contextMenu.js:278-282
    for (QAbstractButton* b : w.ctxMenu.filterButtons->buttons()) {
      QSignalBlocker bl(b);   // seeding the check state must not re-fire applyImageFilter
      b->setChecked(b->property("filterValue").toString() == w.settings.imageFilter);
    }
    w.ctxMenu.tintColorAction->setVisible(w.settings.imageFilter == "custom");

    // contextMenu.js:289-293; blocked so seeding doesn't re-fire the toggle handlers.
    {
      QSignalBlocker be(w.ctxMenu.tooltipEnableCheck), bp(w.ctxMenu.ttPageCheck), bs(w.ctxMenu.ttScreenCheck), bc(w.ctxMenu.ttCoordsCheck);
      w.ctxMenu.tooltipEnableCheck->setChecked(w.settings.tooltipEnabled);
      w.ctxMenu.ttPageCheck->setChecked(w.settings.tooltipShowPage);
      w.ctxMenu.ttScreenCheck->setChecked(w.settings.tooltipShowScreen);
      w.ctxMenu.ttCoordsCheck->setChecked(w.settings.tooltipShowCoords);
    }

    // contextMenu.js:294-297; blocked so seeding doesn't re-apply.
    {
      QSignalBlocker ba(w.ctxMenu.allowFormulas), bx(w.ctxMenu.formulaX), by(w.ctxMenu.formulaY);
      w.ctxMenu.allowFormulas->setChecked(w.settings.allowFormulas);
      w.ctxMenu.formulaX->setText(w.settings.formulaX);
      w.ctxMenu.formulaY->setText(w.settings.formulaY);
    }
    w.ctxMenu.formulaXAct->setVisible(w.settings.allowFormulas);
    w.ctxMenu.formulaYAct->setVisible(w.settings.allowFormulas);
  }

}  // namespace stencil::gui
