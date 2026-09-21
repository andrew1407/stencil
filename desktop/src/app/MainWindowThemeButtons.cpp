#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "../support/dockGrip.hpp"
#include "DropZonesOverlay.hpp"
#include "ChatDock.hpp"
#include "ChatMenuPanel.hpp"
#include "iconSet.hpp"
#include "IncognitoOverlay.hpp"
#include "LogoHoverFx.hpp"
#include "Notifications.hpp"
#include "ProjectDragZones.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "theme.hpp"
#include "PillScrollBars.hpp"
#include "tipContent.hpp"
#include "../support/faceSwap.hpp"
#include "../support/motionPrefs.hpp"   // support::isDustAllowed()
#include "../support/ThemeSwapOverlay.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QLayout>
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>

// MainWindow's theming: the toolbar button fills and the menu-hosted controls.

namespace stencil::gui {

  // The only place the danger red appears. Must run again once the toolbars exist.
  void MainWindow::styleDangerToolButtons() {
    for (QToolButton* b : findChildren<QToolButton*>()) {
      QAction* a = b->defaultAction();
      if (!a) continue;
      // Only tagged section buttons take a fill; Settings stays a bordered ghost, as in the
      // browser.
      const QVariant sect = b->property("toolSection");
      if (!sect.isValid()) continue;
      // The Start/Stop toggle opts out: its accent says which state it is in. Both actions restore
      // its face, since either enable re-copies a glyph.
      if (b == startDrawBtn) {
        b->setProperty("toolFill", QString());
        if (!b->property("fillSync").toBool()) {
          b->setProperty("fillSync", true);
          // A repaint, not a transition: the state change comes through refreshActions and must
          // not be pre-empted.
          for (QAction* state : {actStartDraw, actStopDraw})
            connect(state, &QAction::changed, b, [b] { repaintFace(b); });
        }
        syncDrawToggleFace(canvas && canvas->getIsDrawing(), false);
        b->style()->unpolish(b);
        b->style()->polish(b);
        b->update();
        continue;
      }
      // No fill for a checkable toggle, where the accent means "on" (browser #chat-btn .active);
      // everything that acts on press takes the fill. Fullscreen is filled in both states.
      const bool alwaysFilled = (a == actFullscreen);
      const QString fill = (a->isCheckable() && !alwaysFilled)
                               ? QString()
                               : (dangerIcons.contains(a) ? QStringLiteral("danger")
                                                           : QStringLiteral("accent"));
      b->setProperty("toolFill", fill);
      b->setProperty("toolGhostBox",
                     fill.isEmpty() && sect.toString() == QLatin1String("Settings"));
      const auto paint = [this, a, b] {
        const auto name = actionIconNames.constFind(a);
        if (name != actionIconNames.constEnd()) {
          const QColor ink = toolButtonIconColor(a, iconColor);
          b->setIcon(themedIcon(name.value(), ink, TOOL_ICON));
        }
        // The compound [toolFill="danger"]:disabled selector needs a re-polish on every enabled
        // flip, or a disabled Clear keeps its red.
        b->style()->unpolish(b);
        b->style()->polish(b);
      };
      paint();
      // A QToolButton re-copies its action's icon on every ActionChanged, before changed() is
      // emitted, so repainting from the signal lands last.
      if (!b->property("fillSync").toBool()) {
        b->setProperty("fillSync", true);
        connect(a, &QAction::changed, b, paint);
      }
      // Qt matches property selectors at polish time.
      b->style()->unpolish(b);
      b->style()->polish(b);
      b->update();
    }
  }

  // Menu-hosted checkboxes/radios take the theme text colour, not the accent; glyphs are cached on
  // disk keyed by hex so a switch never serves a stale QSS image.
  void MainWindow::restyleContextToggles(const QColor& textColor) {
    const QString hex = textColor.name().mid(1);  // "rrggbb"
    const QString checkPath = QDir::tempPath() + "/stencil-ctx-check-" + hex + ".png";
    const QString dotPath = QDir::tempPath() + "/stencil-ctx-dot-" + hex + ".png";
    if (!QFileInfo::exists(checkPath))
      themedIcon("check", textColor, 12).pixmap(12, 12).save(checkPath, "PNG");
    if (!QFileInfo::exists(dotPath)) {
      QPixmap dot(12, 12);
      dot.fill(Qt::transparent);
      {
        QPainter p(&dot);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(textColor);
        p.drawEllipse(3, 3, 6, 6);
      }  // painter destroyed before save
      dot.save(dotPath, "PNG");
    }
    const QString css =
        QStringLiteral(
            "QCheckBox::indicator,QRadioButton::indicator{width:15px;height:15px;"
            "border:1px solid %1;background:transparent;}"
            "QCheckBox::indicator{border-radius:4px;}"
            "QRadioButton::indicator{border-radius:8px;}"
            "QCheckBox::indicator:checked{image:url(\"%2\");}"
            "QRadioButton::indicator:checked{image:url(\"%3\");}")
            .arg(textColor.name(), checkPath, dotPath);
    QList<QWidget*> toggles = {tooltipEnableCheck, ttPageCheck, ttScreenCheck,
                               ttCoordsCheck, ctxAllowFormulas};
    if (filterButtons)
      for (QAbstractButton* b : filterButtons->buttons()) toggles.append(b);
    for (QWidget* w : toggles)
      if (w) w->setStyleSheet(css);
  }
}  // namespace stencil::gui

