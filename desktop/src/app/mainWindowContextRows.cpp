// The rest of the canvas context menu's persistent actions. Phase chain in mainWindowContextActions.cpp.
#include "mainWindow.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "notifications.hpp"
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
    actDrawLineNow_ = new QAction("Draw Line", this);
    connect(actDrawLineNow_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) {
        notify_->error("Load an image first");
        return;
      }
      canvas_->setDrawMode(CanvasWidget::DrawMode::Line);
      canvas_->startDrawingMode();  // continues the selected line if one is set
      notify_->info("Drag to draw a line");
    });

    actDrawRectNow_ = new QAction("Draw Rectangle", this);
    connect(actDrawRectNow_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) {
        notify_->error("Load an image first");
        return;
      }
      canvas_->setDrawMode(CanvasWidget::DrawMode::Rect);
      canvas_->startDrawingMode();  // continues the selected line if one is set
      notify_->info("Drag to draw a rectangle");
    });
  }

  void MainWindow::buildContextTooltipActions() {
    // contextMenu.js:96-107, 546-557. Real QCheckBoxes in QWidgetActions so a click flips them WITHOUT dismissing the menu.
    // The enable toggle mirrors the View-menu actTooltip_.
    addContextCheckRow("Show Tooltips", settings_.tooltipEnabled, tooltipEnableCheck_, actTooltipEnable_);
    connect(tooltipEnableCheck_, &QCheckBox::toggled, this, [this](bool on) {
      settings_.tooltipEnabled = on;
      {
        QSignalBlocker b(actTooltip_);
        actTooltip_->setChecked(on);  // keep the View-menu item in lock-step
      }
      persistSettings();
      if (!on)
        tooltip_->hide();
      else if (!QApplication::activePopupWidget())
        onHovered(lastHoverX_, lastHoverY_);  // re-show at the current hover (not while the menu's up)
    });
    // Binding `backing` to the settings_ field keeps the toggles in sync with the source of truth.
    auto mkRowToggle = [this](const QString& text, bool& backing,
                                           QCheckBox*& box, QWidgetAction*& act) {
      addContextCheckRow(text, backing, box, act);
      connect(box, &QCheckBox::toggled, this, [this, &backing](bool on) {
        backing = on;
        persistSettings();
        // Showing the tooltip window while the menu is up would steal the popup's grab and dismiss it.
        if (!QApplication::activePopupWidget()) onHovered(lastHoverX_, lastHoverY_);
      });
    };
    mkRowToggle("Page (cm)", settings_.tooltipShowPage, ttPageCheck_, actTtPage_);
    mkRowToggle("Screen (px)", settings_.tooltipShowScreen, ttScreenCheck_, actTtScreen_);
    mkRowToggle("To Edge (cm)", settings_.tooltipShowCoords, ttCoordsCheck_, actTtCoords_);

    // contextMenu.js:84-100. Twins of the toolbar formula widgets — edits here drive those, so the validate/apply/persist pipeline runs unchanged.
    addContextCheckRow("Allow Formulas", settings_.allowFormulas, ctxAllowFormulas_, ctxAllowFormulasAct_);
    connect(ctxAllowFormulas_, &QCheckBox::toggled, this, [this](bool on) {
      allowFormulas_->setChecked(on);   // the canonical toolbar handler does settings/persist/apply
      if (ctxFormulaXAct_) ctxFormulaXAct_->setVisible(on);
      if (ctxFormulaYAct_) ctxFormulaYAct_->setVisible(on);
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
    mkFormulaRow("x(x)=", "e.g. x + 9", ctxFormulaX_, ctxFormulaXAct_);
    mkFormulaRow("y(y)=", "e.g. (y-7)*4", ctxFormulaY_, ctxFormulaYAct_);
    // Mirror into the canonical toolbar inputs (guarded against a feedback loop).
    connect(ctxFormulaX_, &QLineEdit::textChanged, this, [this](const QString& t) {
      if (formulaX_->text() != t) formulaX_->setText(t);
    });
    connect(ctxFormulaY_, &QLineEdit::textChanged, this, [this](const QString& t) {
      if (formulaY_->text() != t) formulaY_->setText(t);
    });
  }

  void MainWindow::buildUnitActions() {
    // Units: cm | inches; the custom page spinboxes stay backed by cm internally.
    auto* unitGroup = new QActionGroup(this);
    unitGroup->setExclusive(true);
    auto mkUnit = [this, unitGroup](const QString& text, const QString& code) {
      auto* a = new QAction(text, this);
      a->setCheckable(true);
      a->setChecked(settings_.units == code);
      unitGroup->addAction(a);
      connect(a, &QAction::toggled, this, [this, code](bool on) {
        if (on) applyUnits(code);
      });
      return a;
    };
    units_.unitCm = mkUnit("Centimeters (cm)", "cm");
    units_.unitIn = mkUnit("Inches (in)", "in");
  }

  // The rendered preview for one export-variant QAction; null for any action that isn't one of ours.
  QImage MainWindow::exportVariantPreviewImage(QAction* act) const {
    struct Spec { QAction* action; const char* variant; };
    const Spec specs[] = {
        {actCopyImage_, "current"},
        {actSaveImage_, "current"},
        {actCopyImageCurrentRow_, "current"},
        {actSaveImageCurrentRow_, "current"},
        {actCopyImageSplit_, "split"},
        {actSaveImageSplit_, "split"},
        {actCopyImageOriginal_, "original"},
        {actCopyImageTint_, "tint"},
        {actSaveImageOriginal_, "original"},
        {actSaveImageTint_, "tint"},
    };
    for (const auto& s : specs)
      if (s.action == act) return canvas_->renderToImage(QString::fromLatin1(s.variant));
    return QImage();
  }

  // One export-variant menu, identical on every surface: "With Compare" (only while comparing), "Current"'s OWN row,
  // then the two fixed variants — Copy lists Filter Only before Original, Download the reverse (browser exportOptionsMenu.js).
  void MainWindow::populateExportVariantMenu(QMenu* menu, bool copy) {
    if (copy) {
      menu->addAction(actCopyImageSplit_);
      menu->addAction(actCopyImageCurrentRow_);
      menu->addAction(actCopyImageTint_);
      menu->addAction(actCopyImageOriginal_);
    } else {
      menu->addAction(actSaveImageSplit_);
      menu->addAction(actSaveImageCurrentRow_);
      menu->addAction(actSaveImageOriginal_);
      menu->addAction(actSaveImageTint_);
    }
    wireExportPreviewHover(menu);
  }

}  // namespace stencil::gui
