#include "MainWindow.hpp"
#include <QButtonGroup>
#include <QComboBox>
#include "CanvasWidget.hpp"
#include "RemoteSyncController.hpp"
#include "theme.hpp"
#include "../../support/skinPrefs.hpp"

#include <QAbstractButton>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QToolButton>

// Line style, filter/tint and colour-swatch appliers — the shared apply paths behind the
// toolbar controls and their context-menu twins. A filter pick is one undo step; `asUndoStep`
// false adopts one (a load, a peer, an undo) under the step already there.

namespace stencil::gui {




  void MainWindow::applyImageFilter(const QString& mode, bool asUndoStep) {
    settings.imageFilter = mode;
    if (tools.imageFilter) {  // sync toolbar combo by canonical data value
      const int idx = tools.imageFilter->findData(mode);
      if (idx >= 0) {
        QSignalBlocker b(tools.imageFilter);
        tools.imageFilter->setCurrentIndex(idx);
      }
    }
    if (ctxMenu.filterButtons) {  // sync context-menu radio group (blocked so it doesn't re-apply)
      for (QAbstractButton* b : ctxMenu.filterButtons->buttons())
        if (b->property("filterValue").toString() == mode) {
          QSignalBlocker bl(b);
          b->setChecked(true);
          break;
        }
    }
    if (tools.filterColorBtn) tools.filterColorBtn->setVisible(mode == "custom");
    // Re-gate the export rows live so "Filter Only" shows/hides at once.
    parts.exportMenus.syncExportActions();
    if (asUndoStep) canvas->commitFilter(mode, tools.filterColorValue);
    else canvas->setImageFilter(mode, tools.filterColorValue);
    persistSettings();
    if (!remote.reloading) filterDirty = true;   // user changed the filter
    remoteSync->scheduleRemotePush();   // live co-edit: an adopted filter emits no changed()
  }

  void MainWindow::applyTintColor(const QColor& color, bool asUndoStep) {
    tools.filterColorValue = color;
    settings.filterColor = color.name(QColor::HexRgb);
    if (tools.filterColorBtn) updateColorSwatch(tools.filterColorBtn, color);
    if (asUndoStep) canvas->commitFilter(settings.imageFilter, tools.filterColorValue);
    else canvas->setImageFilter(settings.imageFilter, tools.filterColorValue);
    persistSettings();
    if (!remote.reloading) filterDirty = true;   // user changed the tint
    remoteSync->scheduleRemotePush();   // live co-edit: push tint changes to peers
  }


  // The browser uses <input type=color>.
  void MainWindow::updateColorSwatch(QToolButton* btn, const QColor& color) {
    // The same input palette as the spinboxes beside it, swatch drawn inside; re-run from
    // applyTheme.
    const Palette pal = themePalette(parts.theme.paintingDark(), settings.accentColor);
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
