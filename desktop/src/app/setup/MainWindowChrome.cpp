#include "MainWindow.hpp"
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
    panelReopenBtn->setToolTip(QString("Show panel (%1)").arg(hotkey("togglePointsList", "Alt+X")));
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
      chatEdge->setAccent(pal.accent);
    }
    chatEdge->hide();
    spinControlsPill(false);   // seed the pill's angle from the current toolbar state
    updatePanelReopenButton();
  }

  // Over the 9px separator strip (theme.cpp QMainWindow::separator); hidden with the panel, while it floats, and
  // through a slide, so it lands with the panel rather than ahead of it (browser: #fs-panel-resizer forms with the list).
  void MainWindow::positionPanelGrip() {
    if (!panelGrip || !selPanel) return;
    const Qt::DockWidgetArea area = dockWidgetArea(selPanel);
    const bool on = !selPanel->isHidden() && !selPanel->isFloating() && !panelAnim && !showCovered
                    && (area == Qt::RightDockWidgetArea || area == Qt::LeftDockWidgetArea);
    panelGrip->setVisible(on);
    if (!on) {
      panelGrip->setHot(false);
      panelGripDrag = false;
      return;
    }
    const QRect pr = selPanel->geometry();
    constexpr int SEP_W = DOCK_SEPARATOR_PX;   // the QSS QMainWindow::separator width
    panelGrip->setGeometry(area == Qt::LeftDockWidgetArea
                                ? QRect(pr.right() + 1, pr.top(), SEP_W, pr.height())
                                : QRect(pr.left() - SEP_W, pr.top(), SEP_W, pr.height()));
    panelGrip->raise();
  }

  // Horizontal separators are a hairline, so the band is drawn to MIN_THICKNESS while the hit rect stays Qt's strip.
  void MainWindow::positionChatEdge() {
    if (!chatEdge || !chatDock) return;
    const bool on = chatDock->isVisible() && !chatDock->isFloating() && !fs.active && !showCovered;
    chatEdge->setVisible(on);
    if (!on) {
      chatEdge->setHot(false);
      chatEdgeDrag = false;
      chatEdgeHit = QRect();
      return;
    }
    const QRect r = chatDock->geometry();
    const Qt::DockWidgetArea area = dockWidgetArea(chatDock);
    constexpr int SEP_W = DOCK_SEPARATOR_PX;   // QSS QMainWindow::separator width
    constexpr int SEP_H = 1;                  // …and its height (theme.cpp)
    switch (area) {
      case Qt::LeftDockWidgetArea:   chatEdgeHit = QRect(r.right() + 1, r.top(), SEP_W, r.height()); break;
      case Qt::RightDockWidgetArea:  chatEdgeHit = QRect(r.left() - SEP_W, r.top(), SEP_W, r.height()); break;
      case Qt::TopDockWidgetArea:    chatEdgeHit = QRect(r.left(), r.bottom() + 1, r.width(), SEP_H); break;
      case Qt::BottomDockWidgetArea: chatEdgeHit = QRect(r.left(), r.top() - SEP_H, r.width(), SEP_H); break;
      default: chatEdgeHit = QRect(); chatEdge->hide(); return;
    }
    QRect band = chatEdgeHit;
    const int grow = DockEdgeOverlay::MIN_THICKNESS;
    if (band.height() < grow && band.width() > band.height())
      band.adjust(0, -(grow - band.height()) / 2, 0, (grow - band.height() + 1) / 2);
    else if (band.width() < grow && band.height() > band.width())
      band.adjust(-(grow - band.width()) / 2, 0, (grow - band.width() + 1) / 2, 0);
    chatEdge->setGeometry(band);
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
    notify->setLeftInset(left);
    notify->setBottomInset(bottom);
  }

}  // namespace stencil::gui
