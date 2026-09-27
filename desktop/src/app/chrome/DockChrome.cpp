#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "ToastStack.hpp"
#include "mainWindowShared.hpp"
#include "guiHelpers.hpp"
#include "Notifications.hpp"
#include "SelectionPanel.hpp"
#include "../../support/dockGrip.hpp"
#include "iconMotionTypes.hpp"
#include "ChatDock.hpp"

#include <QStatusBar>
#include <QToolButton>

// Window chrome geometry: the overlay arrows, the dock grip, the chat edge and the toast inset.

namespace stencil::gui {

  // The panel re-opens from a floating chevron flush to the canvas' right edge — shown ONLY while the panel is hidden.
  void DockChrome::buildOverlayArrows() {
    w.tools.panelReopenBtn = new QToolButton(&w);
    w.tools.panelReopenBtn->setCursor(Qt::PointingHandCursor);
    w.tools.panelReopenBtn->setFocusPolicy(Qt::NoFocus);   // ditto: no focus halo over the canvas
    w.tools.panelReopenBtn->setFixedSize(PANEL_TOGGLE_BOX, PANEL_TOGGLE_BOX);   // same rounded square as the panel-header chevron
    w.tools.panelReopenBtn->setIconSize(QSize(PANEL_TOGGLE_GLYPH, PANEL_TOGGLE_GLYPH));
    w.tools.panelReopenBtn->setToolTip(QString("Show Last Line Points (%1)").arg(w.keys.value("togglePointsList", "Alt+X")));   // browser mainContent.js
    w.tools.panelReopenBtn->setStyleSheet(panelToggleQss());
    // Its angle is STATE — no icon-motion on hover.
    w.tools.panelReopenBtn->setProperty(NO_ICON_MOTION_PROPERTY, true);
    QObject::connect(w.tools.panelReopenBtn, &QToolButton::clicked, &w,
                     [this] { if (w.acts.panel) w.acts.panel->setChecked(true); });
    w.tools.panelReopenBtn->hide();
    // Over the separator strip, taking its drag as the chat's edge does (browser .panel-resizer).
    panelGrip = new DockEdgeOverlay(&w);
    panelGrip->setObjectName(QStringLiteral("panelGrip"));
    {
      const Palette pal = themePalette(resolveDark(w.settings.themeMode), w.settings.accentColor);
      panelGrip->setColors(pal.borderMain, pal.accent);
    }
    panelGrip->setAxis(Qt::Horizontal);
    panelGrip->setDragHandlers(
        [this] { if (w.selPanel) panelGripStart = w.selPanel->width(); },
        [this](const QPoint& d) {
          if (!w.selPanel) return;
          const bool left = w.editor->dockWidgetArea(w.selPanel) == Qt::LeftDockWidgetArea;
          const int width = qMax(w.panelSlide.MIN_WIDTH, panelGripStart + (left ? d.x() : -d.x()));
          w.editor->resizeDocks({w.selPanel}, {width}, Qt::Horizontal);
        },
        {});
    panelGrip->hide();
    // The chat dock's resize edge (browser .chat-resizer), same trick.
    chatEdge = new DockEdgeOverlay(&w);
    chatEdge->setObjectName(QStringLiteral("chatResizeEdge"));
    {
      const Palette pal = themePalette(resolveDark(w.settings.themeMode), w.settings.accentColor);
      chatEdge->setColors(pal.borderMain, pal.accent);
    }
    chatEdge->hide();
    // The drag (browser chat/dock.js beginResize): the grabbed extent plus the pointer's travel,
    // toward the window's centre, through the layout's own resize. A resize adopts the layout.
    chatEdge->setDragHandlers(
        [this] {
          if (!w.chatDock) return;
          setChatCompactPopover(false);
          const Qt::DockWidgetArea a = w.dockWidgetArea(w.chatDock);
          const bool horiz = a == Qt::LeftDockWidgetArea || a == Qt::RightDockWidgetArea;
          chatEdgeStart = horiz ? w.chatDock->width() : w.chatDock->height();
        },
        [this](const QPoint& d) {
          if (!w.chatDock) return;
          const Qt::DockWidgetArea a = w.dockWidgetArea(w.chatDock);
          const bool horiz = a == Qt::LeftDockWidgetArea || a == Qt::RightDockWidgetArea;
          const int travel = a == Qt::LeftDockWidgetArea ? d.x() : a == Qt::RightDockWidgetArea ? -d.x()
                           : a == Qt::TopDockWidgetArea  ? d.y() : -d.y();
          const int extent = qMax(1, chatEdgeStart + travel);
          w.resizeDocks({w.chatDock}, {extent}, horiz ? Qt::Horizontal : Qt::Vertical);
          chatRestoreExtent = extent;   // the size it reopens at; the layout applies it a beat later
        },
        {});
    w.spinControlsPill(false);   // seed the pill's angle from the current toolbar state
    updatePanelReopenButton();
  }

  // Over the 9px separator strip (theme.cpp QMainWindow::separator); hidden with the panel, while it floats, and
  // through a slide, so it lands with the panel rather than ahead of it (browser: #fs-panel-resizer forms with the list).
  void DockChrome::positionPanelGrip() {
    if (!panelGrip || !w.selPanel) return;
    const Qt::DockWidgetArea area = w.editor->dockWidgetArea(w.selPanel);
    const bool on = !w.selPanel->isHidden() && !w.selPanel->isFloating() && !w.panelSlide.anim && !w.showCovered
                    && (area == Qt::RightDockWidgetArea || area == Qt::LeftDockWidgetArea);
    panelGrip->setVisible(on);
    if (!on) {
      panelGrip->setHot(false);
      return;
    }
    const QRect pr(w.selPanel->mapTo(&w, QPoint(0, 0)), w.selPanel->size());   // the overlay is the window's
    constexpr int SEP_W = DOCK_SEPARATOR_PX;   // the QSS QMainWindow::separator width
    panelGrip->setGeometry(area == Qt::LeftDockWidgetArea
                                ? QRect(pr.right() + 1, pr.top(), SEP_W, pr.height())
                                : QRect(pr.left() - SEP_W, pr.top(), SEP_W, pr.height()));
    panelGrip->raise();
  }

  // The strip lies INSIDE the dock along the edge it is docked by (browser .chat-resizer): the
  // dock's inner paddings are wider, so it covers no content; the page gap is the separator's.
  void DockChrome::positionChatEdge() {
    if (!chatEdge || !w.chatDock) return;
    const bool on = w.chatDock->isVisible() && !w.chatDock->isFloating() && !w.fs.active && !w.showCovered;
    if (!on) {
      chatEdge->hide();
      chatEdge->setHot(false);
      return;
    }
    const QRect r = w.chatDock->geometry();
    constexpr int T = DockEdgeOverlay::THICKNESS;
    QRect band;
    switch (w.dockWidgetArea(w.chatDock)) {
      case Qt::LeftDockWidgetArea:   band = QRect(r.right() + 1 - T, r.top(), T, r.height()); break;
      case Qt::RightDockWidgetArea:  band = QRect(r.left(), r.top(), T, r.height()); break;
      case Qt::TopDockWidgetArea:    band = QRect(r.left(), r.bottom() + 1 - T, r.width(), T); break;
      case Qt::BottomDockWidgetArea: band = QRect(r.left(), r.top(), r.width(), T); break;
      default: chatEdge->hide(); return;
    }
    chatEdge->setAxis(band.height() > band.width() ? Qt::Horizontal : Qt::Vertical);
    // Placed BEFORE it shows: a widget shown at its old box under the pointer takes an Enter.
    chatEdge->setGeometry(band);
    chatEdge->show();
    chatEdge->raise();
  }

  // The stack hangs off the WINDOW's bottom-left, so it is told to clear the status bar's coord readout.
  void DockChrome::syncToastInset() {
    // Late dock signals during ~MainWindow land after the layout died.
    if (w.tearingDown || !w.notify) return;
    // findChild, not statusBar() — the accessor lazily CREATES the empty strip this window deliberately doesn't keep.
    const QWidget* bar = w.findChild<QStatusBar*>();
    int bottom = bar && bar->isVisible() ? bar->height() : 0;
    int left = 0;
    // A docked chat panel owns its corner: the stack moves beside or above it.
    if (w.chatDock && w.chatDock->isVisible() && !w.chatDock->isFloating()) {
      const Qt::DockWidgetArea area = w.dockWidgetArea(w.chatDock);
      if (area == Qt::LeftDockWidgetArea) left = w.chatDock->width();   // the gap is the stack's own
      else if (area == Qt::BottomDockWidgetArea) bottom += w.chatDock->height() + 8;
    }
    w.notify->toasts()->setLeftInset(left);
    w.notify->toasts()->setBottomInset(bottom);
  }

}  // namespace stencil::gui
