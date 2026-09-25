#include "MainWindow.hpp"
#include "ToastStack.hpp"
#include "mainWindowShared.hpp"
#include "MainWindow.hpp"
#include "StayOpenMenu.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "ChatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "DropZonesOverlay.hpp"
#include "IncognitoOverlay.hpp"
#include "guiHelpers.hpp"
#include "MenuHotkeys.hpp"
#include "menuReveal.hpp"
#include "MenuShimmer.hpp"
#include "modalReveal.hpp"
#include "SearchCombo.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "theme.hpp"
#include "../../support/tip/AppTooltip.hpp"
#include "../../support/control/swap/controlSwap.hpp"
#include "../../support/dockGrip.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/icon/iconMotion.hpp"
#include "../../support/motion/HoverSlide.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include <QStatusBar>
#include <QToolButton>

// Window chrome geometry: the overlay arrows, the dock grip, the chat edge and the toast inset.

namespace stencil::gui {

  // The panel re-opens from a floating chevron flush to the canvas' right edge — shown ONLY while the panel is hidden.
  void MainWindow::buildOverlayArrows() {
    panelReopenBtn = new QToolButton(this);
    panelReopenBtn->setCursor(Qt::PointingHandCursor);
    panelReopenBtn->setFocusPolicy(Qt::NoFocus);   // ditto: no focus halo over the canvas
    panelReopenBtn->setFixedSize(PANEL_TOGGLE_BOX, PANEL_TOGGLE_BOX);   // same rounded square as the panel-header chevron
    panelReopenBtn->setIconSize(QSize(PANEL_TOGGLE_GLYPH, PANEL_TOGGLE_GLYPH));
    panelReopenBtn->setToolTip(QString("Show Last Line Points (%1)").arg(hotkey("togglePointsList", "Alt+X")));   // browser mainContent.js
    panelReopenBtn->setStyleSheet(panelToggleQss());
    // Its angle is STATE — no icon-motion on hover.
    panelReopenBtn->setProperty(NO_ICON_MOTION_PROPERTY, true);
    connect(panelReopenBtn, &QToolButton::clicked, this,
            [this] { if (actPanel) actPanel->setChecked(true); });
    panelReopenBtn->hide();
    // The separator is QMainWindow chrome with no widget, so a mouse-transparent overlay paints the grip (browser .panel-resizer).
    panelGrip = new DockGripOverlay(this);
    panelGrip->setObjectName(QStringLiteral("panelGrip"));
    {
      const Palette pal = themePalette(resolveDark(settings.themeMode), settings.accentColor);
      panelGrip->setColors(pal.borderMain, pal.accent);
    }
    panelGrip->hide();
    // The chat dock's resize edge (browser .chat-resizer), same trick.
    chatEdge = new DockEdgeOverlay(this);
    chatEdge->setObjectName(QStringLiteral("chatResizeEdge"));
    {
      const Palette pal = themePalette(resolveDark(settings.themeMode), settings.accentColor);
      chatEdge->setColors(pal.borderMain, pal.accent);
    }
    chatEdge->hide();
    // The drag (browser chat/dock.js beginResize): the grabbed extent plus the pointer's travel,
    // toward the window's centre, through the layout's own resize. A resize adopts the layout.
    chatEdge->setDragHandlers(
        [this] {
          if (!chatDock) return;
          setChatCompactPopover(false);
          const Qt::DockWidgetArea a = dockWidgetArea(chatDock);
          const bool horiz = a == Qt::LeftDockWidgetArea || a == Qt::RightDockWidgetArea;
          chatEdgeStart = horiz ? chatDock->width() : chatDock->height();
        },
        [this](const QPoint& d) {
          if (!chatDock) return;
          const Qt::DockWidgetArea a = dockWidgetArea(chatDock);
          const bool horiz = a == Qt::LeftDockWidgetArea || a == Qt::RightDockWidgetArea;
          const int travel = a == Qt::LeftDockWidgetArea ? d.x() : a == Qt::RightDockWidgetArea ? -d.x()
                           : a == Qt::TopDockWidgetArea  ? d.y() : -d.y();
          const int extent = qMax(1, chatEdgeStart + travel);
          resizeDocks({chatDock}, {extent}, horiz ? Qt::Horizontal : Qt::Vertical);
          chatRestoreExtent = extent;   // the size it reopens at; the layout applies it a beat later
        },
        {});
    spinControlsPill(false);   // seed the pill's angle from the current toolbar state
    updatePanelReopenButton();
  }

  // Over the 9px separator strip (theme.cpp QMainWindow::separator); hidden with the panel, while it floats, and
  // through a slide, so it lands with the panel rather than ahead of it (browser: #fs-panel-resizer forms with the list).
  void MainWindow::positionPanelGrip() {
    if (!panelGrip || !selPanel) return;
    const Qt::DockWidgetArea area = editor->dockWidgetArea(selPanel);
    const bool on = !selPanel->isHidden() && !selPanel->isFloating() && !panelAnim && !showCovered
                    && (area == Qt::RightDockWidgetArea || area == Qt::LeftDockWidgetArea);
    panelGrip->setVisible(on);
    if (!on) {
      panelGrip->setHot(false);
      panelGripDrag = false;
      return;
    }
    const QRect pr(selPanel->mapTo(this, QPoint(0, 0)), selPanel->size());   // the overlay is the window's
    constexpr int SEP_W = DOCK_SEPARATOR_PX;   // the QSS QMainWindow::separator width
    panelGrip->setGeometry(area == Qt::LeftDockWidgetArea
                                ? QRect(pr.right() + 1, pr.top(), SEP_W, pr.height())
                                : QRect(pr.left() - SEP_W, pr.top(), SEP_W, pr.height()));
    panelGrip->raise();
  }

  // The strip lies INSIDE the dock along the edge it is docked by (browser .chat-resizer): the
  // dock's inner paddings are wider, so it covers no content; the page gap is the separator's.
  void MainWindow::positionChatEdge() {
    if (!chatEdge || !chatDock) return;
    const bool on = chatDock->isVisible() && !chatDock->isFloating() && !fs.active && !showCovered;
    if (!on) {
      chatEdge->hide();
      chatEdge->setHot(false);
      return;
    }
    const QRect r = chatDock->geometry();
    constexpr int T = DockEdgeOverlay::THICKNESS;
    QRect band;
    switch (dockWidgetArea(chatDock)) {
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
  void MainWindow::syncToastInset() {
    // Late dock signals during ~MainWindow land after the layout died.
    if (tearingDown || !notify) return;
    // findChild, not statusBar() — the accessor lazily CREATES the empty strip this window deliberately doesn't keep.
    const QWidget* bar = findChild<QStatusBar*>();
    int bottom = bar && bar->isVisible() ? bar->height() : 0;
    int left = 0;
    // A docked chat panel owns its corner: the stack moves beside or above it.
    if (chatDock && chatDock->isVisible() && !chatDock->isFloating()) {
      const Qt::DockWidgetArea area = dockWidgetArea(chatDock);
      if (area == Qt::LeftDockWidgetArea) left = chatDock->width();   // the gap is the stack's own
      else if (area == Qt::BottomDockWidgetArea) bottom += chatDock->height() + 8;
    }
    notify->toasts()->setLeftInset(left);
    notify->toasts()->setBottomInset(bottom);
  }

}  // namespace stencil::gui
