// The rest of the canvas context menu's persistent actions. Phase chain in MainWindowContextActions.cpp.
#include "MainWindow.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "exportPreview.hpp"
#include "menuRowPolish.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QWidgetAction>

namespace stencil::gui {

  void MainWindow::buildDrawNowActions() {
    // contextMenu.js ctx-draw-line/ctx-draw-rect: two fixed actions, each with its own outline glyph (browser parity).
    actDrawLineNow = new QAction("Draw Line", this);
    connect(actDrawLineNow, &QAction::triggered, this, [this] {
      if (!canvas->hasImage()) {
        notify->error("Load an image first");
        return;
      }
      canvas->setDrawMode(CanvasWidget::DrawMode::LINE);
      canvas->startDrawingMode();  // continues the selected line if one is set
      notify->info("Drag to draw a line");
    });

    actDrawRectNow = new QAction("Draw Rectangle", this);
    connect(actDrawRectNow, &QAction::triggered, this, [this] {
      if (!canvas->hasImage()) {
        notify->error("Load an image first");
        return;
      }
      canvas->setDrawMode(CanvasWidget::DrawMode::RECT);
      canvas->startDrawingMode();  // continues the selected line if one is set
      notify->info("Drag to draw a rectangle");
    });
  }

  void MainWindow::buildContextTooltipActions() {
    // contextMenu.js:96-107, 546-557. Real QCheckBoxes in QWidgetActions so a click flips them WITHOUT dismissing the menu.
    // The enable toggle mirrors the View-menu actTooltip.
    addContextCheckRow("Show Tooltips", settings.tooltipEnabled, tooltipEnableCheck, actTooltipEnable);
    connect(tooltipEnableCheck, &QCheckBox::toggled, this, [this](bool on) {
      settings.tooltipEnabled = on;
      {
        QSignalBlocker b(actTooltip);
        actTooltip->setChecked(on);  // keep the View-menu item in lock-step
      }
      persistSettings();
      if (!on)
        tooltip->hide();
      else if (!QApplication::activePopupWidget())
        onHovered(lastHoverX, lastHoverY);  // re-show at the current hover (not while the menu's up)
    });
    // Binding `backing` to the settings field keeps the toggles in sync with the source of truth.
    auto mkRowToggle = [this](const QString& text, bool& backing,
                                           QCheckBox*& box, QWidgetAction*& act) {
      addContextCheckRow(text, backing, box, act);
      connect(box, &QCheckBox::toggled, this, [this, &backing](bool on) {
        backing = on;
        persistSettings();
        // Showing the tooltip window while the menu is up would steal the popup's grab and dismiss it.
        if (!QApplication::activePopupWidget()) onHovered(lastHoverX, lastHoverY);
      });
    };
    mkRowToggle("Page (cm)", settings.tooltipShowPage, ttPageCheck, actTtPage);
    mkRowToggle("Screen (px)", settings.tooltipShowScreen, ttScreenCheck, actTtScreen);
    mkRowToggle("To Edge (cm)", settings.tooltipShowCoords, ttCoordsCheck, actTtCoords);

    // contextMenu.js:84-100. Twins of the toolbar formula widgets — edits here drive those, so the validate/apply/persist pipeline runs unchanged.
    addContextCheckRow("Allow Formulas", settings.allowFormulas, ctxAllowFormulas, ctxAllowFormulasAct);
    connect(ctxAllowFormulas, &QCheckBox::toggled, this, [this](bool on) {
      allowFormulas->setChecked(on);   // the canonical toolbar handler does settings/persist/apply
      if (ctxFormulaXAct) ctxFormulaXAct->setVisible(on);
      if (ctxFormulaYAct) ctxFormulaYAct->setVisible(on);
    });
    auto mkFormulaRow = [this](const QString& label, const QString& placeholder,
                                             QLineEdit*& edit, QWidgetAction*& act) {
      auto* lay = makeContextMenuRow(act, 2, 4);
      auto* w = lay->parentWidget();
      lay->addWidget(new QLabel(label, w));
      edit = new QLineEdit(w);
      edit->setPlaceholderText(placeholder);
      edit->setFixedWidth(150);
      lay->addStretch(1);
      lay->addWidget(edit);
    };
    mkFormulaRow("x(x)=", "e.g. x + 9", ctxFormulaX, ctxFormulaXAct);
    mkFormulaRow("y(y)=", "e.g. (y-7)*4", ctxFormulaY, ctxFormulaYAct);
    // Mirror into the canonical toolbar inputs (guarded against a feedback loop).
    connect(ctxFormulaX, &QLineEdit::textChanged, this, [this](const QString& t) {
      if (formulaX->text() != t) formulaX->setText(t);
    });
    connect(ctxFormulaY, &QLineEdit::textChanged, this, [this](const QString& t) {
      if (formulaY->text() != t) formulaY->setText(t);
    });
  }

  void MainWindow::buildUnitActions() {
    // Units: cm | inches; the custom page spinboxes stay backed by cm internally.
    auto* unitGroup = new QActionGroup(this);
    unitGroup->setExclusive(true);
    auto mkUnit = [this, unitGroup](const QString& text, const QString& code) {
      auto* a = new QAction(text, this);
      a->setCheckable(true);
      a->setChecked(settings.units == code);
      unitGroup->addAction(a);
      connect(a, &QAction::toggled, this, [this, code](bool on) {
        if (on) applyUnits(code);
      });
      return a;
    };
    units.unitCm = mkUnit("Centimeters (cm)", "cm");
    units.unitIn = mkUnit("Inches (in)", "in");
  }

  // The rendered preview for one export-variant QAction; null for any action that isn't one of ours.
  QImage MainWindow::exportVariantPreviewImage(QAction* act) const {
    struct Spec { QAction* action; const char* variant; };
    const Spec specs[] = {
        {actCopyImage, "current"},
        {actSaveImage, "current"},
        {actCopyImageCurrentRow, "current"},
        {actSaveImageCurrentRow, "current"},
        {actCopyImageSplit, "split"},
        {actSaveImageSplit, "split"},
        {actCopyImageOriginal, "original"},
        {actCopyImageTint, "tint"},
        {actSaveImageOriginal, "original"},
        {actSaveImageTint, "tint"},
    };
    for (const auto& s : specs)
      if (s.action == act) return canvas->renderToImage(QString::fromLatin1(s.variant));
    return QImage();
  }

  // One export-variant menu, identical on every surface: "With Compare" (only while comparing), "Current"'s OWN row,
  // then the two fixed variants — Copy lists Filter Only before Original, Download the reverse (browser export/optionsMenu.js).
  void MainWindow::populateExportVariantMenu(QMenu* menu, bool copy) {
    if (copy) {
      menu->addAction(actCopyImageSplit);
      menu->addAction(actCopyImageCurrentRow);
      menu->addAction(actCopyImageTint);
      menu->addAction(actCopyImageOriginal);
    } else {
      menu->addAction(actSaveImageSplit);
      menu->addAction(actSaveImageCurrentRow);
      menu->addAction(actSaveImageOriginal);
      menu->addAction(actSaveImageTint);
    }
    wireExportPreviewHover(menu);
  }

}  // namespace stencil::gui
