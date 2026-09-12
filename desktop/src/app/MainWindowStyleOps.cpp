#include "MainWindow.hpp"
#include <QActionGroup>
#include <QButtonGroup>
#include <QComboBox>
#include "MainWindow.hpp"
#include "StayOpenMenu.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "ChatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSyncController.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "ShortcutsDialog.hpp"
#include "theme.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QToolButton>

// Line style, filter/tint and colour-swatch appliers — the shared apply paths behind the
// toolbar controls and their context-menu twins.

namespace stencil::gui {

  // Mirrors the browser change handlers (drawingApp.js:155-178). Defaults only — never the
  // selection.
  QColor MainWindow::effectiveDefaultPointColor() const {
    const QColor c(settings_.defaultPointColor);
    return (!settings_.defaultPointColor.isEmpty() && c.isValid()) ? c : lineColorValue_;
  }

  void MainWindow::onLineStyleControlChanged() {
    canvas_->setDefaults(settings_.defaultColor, settings_.defaultThickness,
                         settings_.defaultPointSize, settings_.defaultStyle,
                         settings_.defaultPointColor);
    persistSettings();
  }

  // One apply path for toolbar + context-menu twins. Setting an exclusive QAction's checked state
  // emits toggled(), not triggered(), so no re-entry.
  // Transient view state, not persisted.
  void MainWindow::setCompareModeUi(const QString& mode) {
    canvas_->setCompareMode(mode);
    if (compareCombo_) {
      const int idx = compareCombo_->findData(mode);
      if (idx >= 0 && idx != compareCombo_->currentIndex()) {
        QSignalBlocker b(compareCombo_);
        compareCombo_->setCurrentIndex(idx);
      }
    }
    if (compareGroup_) {
      for (QAction* a : compareGroup_->actions())
        if (a->data().toString() == mode) { a->setChecked(true); break; }
    }
    refreshActions();   // read-only view gates the editing actions + their shortcuts
  }

  void MainWindow::applyImageFilter(const QString& mode) {
    settings_.imageFilter = mode;
    if (imageFilter_) {  // sync toolbar combo by canonical data value
      const int idx = imageFilter_->findData(mode);
      if (idx >= 0) {
        QSignalBlocker b(imageFilter_);
        imageFilter_->setCurrentIndex(idx);
      }
    }
    if (filterButtons_) {  // sync context-menu radio group (blocked so it doesn't re-apply)
      for (QAbstractButton* b : filterButtons_->buttons())
        if (b->property("filterValue").toString() == mode) {
          QSignalBlocker bl(b);
          b->setChecked(true);
          break;
        }
    }
    if (filterColorBtn_) filterColorBtn_->setVisible(mode == "custom");
    // Re-gate the export rows live so "Filter Only" shows/hides at once.
    syncExportActions();
    canvas_->setImageFilter(mode, filterColorValue_);
    persistSettings();
    if (!remoteReloading_) filterDirty_ = true;   // user changed the filter
    remoteSync_->scheduleRemotePush();   // live co-edit: a filter change isn't a canvas changed()
  }

  void MainWindow::applyTintColor(const QColor& color) {
    filterColorValue_ = color;
    settings_.filterColor = color.name(QColor::HexRgb);
    if (filterColorBtn_) updateColorSwatch(filterColorBtn_, color);
    canvas_->setImageFilter(settings_.imageFilter, filterColorValue_);
    persistSettings();
    if (!remoteReloading_) filterDirty_ = true;   // user changed the tint
    remoteSync_->scheduleRemotePush();   // live co-edit: push tint changes to peers
  }

  void MainWindow::applyLineStyle(const QString& style) {
    settings_.defaultStyle = style;
    if (lineStyle_) {  // sync toolbar combo by canonical data value
      const int idx = lineStyle_->findData(style);
      if (idx >= 0) {
        QSignalBlocker b(lineStyle_);
        lineStyle_->setCurrentIndex(idx);
      }
    }
    if (lineStyleGroup_) {  // sync context-menu radio group
      for (QAction* a : lineStyleGroup_->actions())
        if (a->data().toString() == style) { a->setChecked(true); break; }
    }
    onLineStyleControlChanged();
  }

  // The browser uses <input type=color>.
  void MainWindow::updateColorSwatch(QToolButton* btn, const QColor& color) {
    // The same input palette as the spinboxes beside it, swatch drawn inside; re-run from
    // applyTheme.
    const Palette pal =
        themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    const bool labelled = !btn->text().isEmpty();
    btn->setFixedHeight(26);
    if (labelled) btn->setMinimumWidth(46);
    else btn->setFixedWidth(46);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        QStringLiteral(
            "QToolButton{background:%1;border:1px solid %2;border-radius:7px;"
            "color:%4;padding:0 %5px;}"
            "QToolButton:hover{border-color:%3;}")
            .arg(pal.inputBg.name(), pal.borderMain.name(), pal.accent.name(),
                 pal.textMain.name(), labelled ? QStringLiteral("6") : QStringLiteral("0")));
    // A luminance-tuned outline keeps a colour near the input background visible in either theme.
    QPixmap pm(32, 16);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      const bool lightFill = color.lightnessF() > 0.7;
      p.setPen(QPen(lightFill ? QColor(0, 0, 0, 102) : QColor(255, 255, 255, 102), 1));
      p.setBrush(color);
      p.drawRoundedRect(QRectF(0.5, 0.5, 31.0, 15.0), 4, 4);
    }
    btn->setIcon(QIcon(pm));
    btn->setIconSize(pm.size());
  }

}  // namespace stencil::gui
