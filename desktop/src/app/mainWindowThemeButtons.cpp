#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "../support/dockGrip.hpp"
#include "dropZonesOverlay.hpp"
#include "chatDock.hpp"
#include "chatMenuPanel.hpp"
#include "iconSet.hpp"
#include "incognitoOverlay.hpp"
#include "logoHoverFx.hpp"
#include "notifications.hpp"
#include "projectDragZones.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "theme.hpp"
#include "pillScrollBars.hpp"
#include "tipContent.hpp"
#include "../support/faceSwap.hpp"
#include "../support/motionPrefs.hpp"   // support::dustAllowed()
#include "../support/themeSwapOverlay.hpp"

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

// MainWindow's theming: applyTheme() and the icon/section restyling passes.
// Split from mainWindow.cpp; same class, definitions only.

namespace stencil::gui {

  // Filled-danger treatment for destructive toolbar buttons — the ONLY place the
  // danger red appears (menus keep the neutral glyph). Must run again once the
  // toolbars exist: styleActionIcons can fire before any button exists.
  void MainWindow::styleDangerToolButtons() {
    for (QToolButton* b : findChildren<QToolButton*>()) {
      QAction* a = b->defaultAction();
      if (!a) continue;
      // Only the toolbar-section buttons take a fill; makeToolSection tags them with the
      // section they belong to. Settings stays a bordered ghost, as in the browser.
      const QVariant sect = b->property("toolSection");
      if (!sect.isValid()) continue;
      // The Start/Stop toggle opts OUT of the section fill: its accent says which state it
      // is in (QToolButton[drawToggle]), so a permanent accent chip would say nothing.
      // Both actions have to restore its face, since either one's enable/disable re-copies
      // that action's menu glyph onto the button.
      if (b == startDrawBtn_) {
        b->setProperty("toolFill", QString());
        if (!b->property("fillSync").toBool()) {
          b->setProperty("fillSync", true);
          // A REPAINT, not a transition: the state change itself comes through
          // refreshActions and gets the swap, and this must not pre-empt it (the
          // enable/disable that starts a session fires first).
          for (QAction* state : {actStartDraw_, actStopDraw_})
            connect(state, &QAction::changed, b, [b] { repaintFace(b); });
        }
        syncDrawToggleFace(canvas_ && canvas_->isDrawing(), false);
        b->style()->unpolish(b);
        b->style()->polish(b);
        b->update();
        continue;
      }
      // No fill for a CHECKABLE toggle: there the accent means "on" (browser #chat-btn /
      // .active), so it comes from QToolButton:checked. Everything else ACTS the moment it
      // is pressed — Fit to window, the SETTINGS cluster's theme switch, shortcuts, visual
      // styles and help included — and takes the fill every other such button has (user
      // decision; browser twins: #zoom-fit, #theme-toggle, #settings-btn, #visuals-btn and
      // #info-btn in css/components.css).
      // …except Fullscreen, filled in BOTH states (browser #fullscreen-toggle): its glyph,
      // not its fill, says which way the click goes.
      const bool alwaysFilled = (a == actFullscreen_);
      const QString fill = (a->isCheckable() && !alwaysFilled)
                               ? QString()
                               : (dangerIcons_.contains(a) ? QStringLiteral("danger")
                                                           : QStringLiteral("accent"));
      b->setProperty("toolFill", fill);
      // …and what is left unfilled in that cluster wears its bordered ghost box.
      b->setProperty("toolGhostBox",
                     fill.isEmpty() && sect.toString() == QLatin1String("Settings"));
      const auto paint = [this, a, b] {
        const auto name = actionIconNames_.constFind(a);
        if (name != actionIconNames_.constEnd()) {
          const QColor ink = toolButtonIconColor(a, iconColor_);
          b->setIcon(themedIcon(name.value(), ink, kToolIcon));
        }
        // The compound [toolFill="danger"]:disabled selector needs a re-polish on every
        // enabled/disabled flip, same as the property itself does below — otherwise a
        // destructive action that goes disabled (Clear All Lines with nothing to clear)
        // kept its solid red fill instead of falling back to the muted disabled chip.
        b->style()->unpolish(b);
        b->style()->polish(b);
      };
      paint();
      // A QToolButton re-copies its default action's icon on every QEvent::ActionChanged —
      // so the first setEnabled/setVisible from refreshActions put the MENU's glyph back on
      // the fill, where it is invisible. Qt sends that event before it emits changed(), so
      // repainting from this signal lands last. Connected once per button.
      if (!b->property("fillSync").toBool()) {
        b->setProperty("fillSync", true);
        connect(a, &QAction::changed, b, paint);
      }
      // Qt matches property selectors at POLISH time, so a property set after the
      // stylesheet was applied changes nothing until the widget is re-polished.
      b->style()->unpolish(b);
      b->style()->polish(b);
      b->update();
    }
  }

  // Recolour the context-menu hosted checkboxes/radios so their indicators use the theme TEXT
  // colour, matching the surrounding menu text rather than the app-wide accent (which the global
  // QSS applies to every other QCheckBox/QRadioButton). The check/dot glyphs are rasterised in
  // the text colour and cached on disk keyed by hex, so a theme switch regenerates them without
  // Qt serving a stale QSS-image cache. Applied per-widget so only these menu controls change.
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
    QList<QWidget*> toggles = {tooltipEnableCheck_, ttPageCheck_, ttScreenCheck_,
                               ttCoordsCheck_, ctxAllowFormulas_};
    if (filterButtons_)
      for (QAbstractButton* b : filterButtons_->buttons()) toggles.append(b);
    for (QWidget* w : toggles)
      if (w) w->setStyleSheet(css);
  }
}  // namespace stencil::gui

