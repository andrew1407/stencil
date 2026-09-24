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
#include "../../support/skinPrefs.hpp"

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
    const QColor c(settings.defaultPointColor);
    return (!settings.defaultPointColor.isEmpty() && c.isValid()) ? c : lineColorValue;
  }

  void MainWindow::onLineStyleControlChanged() {
    canvas->setDefaults(settings.defaultColor, settings.defaultThickness,
                         settings.defaultPointSize, settings.defaultStyle,
                         settings.defaultPointColor);
    persistSettings();
  }

  // One apply path for toolbar + context-menu twins. Setting an exclusive QAction's checked state
  // emits toggled(), not triggered(), so no re-entry. Transient view state, not persisted.
  void MainWindow::setCompareModeUi(const QString& mode) {
    canvas->setCompareMode(mode);
    if (compareCombo) {
      const int idx = compareCombo->findData(mode);
      if (idx >= 0 && idx != compareCombo->currentIndex()) {
        QSignalBlocker b(compareCombo);
        compareCombo->setCurrentIndex(idx);
      }
    }
    if (compareGroup) {
      for (QAction* a : compareGroup->actions())
        if (a->data().toString() == mode) { a->setChecked(true); break; }
    }
    refreshActions();   // read-only view gates the editing actions + their shortcuts
  }

  void MainWindow::applyImageFilter(const QString& mode) {
    settings.imageFilter = mode;
    if (imageFilter) {  // sync toolbar combo by canonical data value
      const int idx = imageFilter->findData(mode);
      if (idx >= 0) {
        QSignalBlocker b(imageFilter);
        imageFilter->setCurrentIndex(idx);
      }
    }
    if (filterButtons) {  // sync context-menu radio group (blocked so it doesn't re-apply)
      for (QAbstractButton* b : filterButtons->buttons())
        if (b->property("filterValue").toString() == mode) {
          QSignalBlocker bl(b);
          b->setChecked(true);
          break;
        }
    }
    if (filterColorBtn) filterColorBtn->setVisible(mode == "custom");
    // Re-gate the export rows live so "Filter Only" shows/hides at once.
    syncExportActions();
    canvas->setImageFilter(mode, filterColorValue);
    persistSettings();
    if (!remoteReloading) filterDirty = true;   // user changed the filter
    remoteSync->scheduleRemotePush();   // live co-edit: a filter change isn't a canvas changed()
  }

  void MainWindow::applyTintColor(const QColor& color) {
    filterColorValue = color;
    settings.filterColor = color.name(QColor::HexRgb);
    if (filterColorBtn) updateColorSwatch(filterColorBtn, color);
    canvas->setImageFilter(settings.imageFilter, filterColorValue);
    persistSettings();
    if (!remoteReloading) filterDirty = true;   // user changed the tint
    remoteSync->scheduleRemotePush();   // live co-edit: push tint changes to peers
  }

  void MainWindow::applyLineStyle(const QString& style) {
    settings.defaultStyle = style;
    if (lineStyle) {  // sync toolbar combo by canonical data value
      const int idx = lineStyle->findData(style);
      if (idx >= 0) {
        QSignalBlocker b(lineStyle);
        lineStyle->setCurrentIndex(idx);
      }
    }
    if (lineStyleGroup) {  // sync context-menu radio group
      for (QAction* a : lineStyleGroup->actions())
        if (a->data().toString() == style) { a->setChecked(true); break; }
    }
    onLineStyleControlChanged();
  }

  // The browser uses <input type=color>.
  void MainWindow::updateColorSwatch(QToolButton* btn, const QColor& color) {
    // The same input palette as the spinboxes beside it, swatch drawn inside; re-run from
    // applyTheme.
    const Palette pal =
        themePalette(resolveDark(settings.themeMode), settings.accentColor);
    const bool labelled = !btn->text().isEmpty();
    btn->setFixedHeight(26);
    if (labelled) btn->setMinimumWidth(46);
    else btn->setFixedWidth(46);
    btn->setCursor(Qt::PointingHandCursor);
    const bool skin = support::isWebcore();   // a skin squares every corner, the chip included
    btn->setStyleSheet(
        QStringLiteral(
            "QToolButton{background:%1;border:1px solid %2;border-radius:%6px;"
            "color:%4;padding:0 %5px;}"
            "QToolButton:hover{border-color:%3;}")
            .arg(pal.inputBg.name(), pal.borderMain.name(), pal.accent.name(),
                 pal.textMain.name(), labelled ? QStringLiteral("6") : QStringLiteral("0"),
                 skin ? QStringLiteral("0") : QStringLiteral("7")));
    // A luminance-tuned outline keeps a colour near the input background visible in either theme.
    QPixmap pm(32, 16);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, !skin);
      const bool lightFill = color.lightnessF() > 0.7;
      p.setPen(QPen(skin ? QColor(Qt::black)
                         : lightFill ? QColor(0, 0, 0, 102) : QColor(255, 255, 255, 102), 1));
      p.setBrush(color);
      const QRectF chip(0.5, 0.5, 31.0, 15.0);
      if (skin) p.drawRect(chip); else p.drawRoundedRect(chip, 4, 4);
    }
    btn->setIcon(QIcon(pm));
    btn->setIconSize(pm.size());
  }

}  // namespace stencil::gui
