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
#include "../support/AppTooltip.hpp"
#include "../support/controlSwap.hpp"
#include "../support/dockGrip.hpp"
#include "../support/modalChrome.hpp"
#include "../support/iconMotion.hpp"
#include "../support/HoverSlide.hpp"
#include "../support/ShimmerOverlay.hpp"

#include <QStatusBar>
#include <QToolButton>

// Window chrome geometry: the overlay arrows, the dock grip, the chat edge and the toast inset.

namespace stencil::gui {

  // The panel re-opens from a floating chevron flush to the canvas' right edge — shown ONLY while the panel is hidden.
  void MainWindow::buildOverlayArrows() {
    panelReopenBtn_ = new QToolButton(this);
    panelReopenBtn_->setCursor(Qt::PointingHandCursor);
    panelReopenBtn_->setFocusPolicy(Qt::NoFocus);   // ditto: no focus halo over the canvas
    panelReopenBtn_->setFixedSize(PANEL_TOGGLE_BOX, PANEL_TOGGLE_BOX);   // same rounded square as the panel-header chevron
    panelReopenBtn_->setIconSize(QSize(PANEL_TOGGLE_GLYPH, PANEL_TOGGLE_GLYPH));
    panelReopenBtn_->setToolTip(QString("Show panel (%1)").arg(hotkey("togglePointsList", "Alt+X")));
    panelReopenBtn_->setStyleSheet(panelToggleQss());
    // Its angle is STATE — no icon-motion on hover.
    panelReopenBtn_->setProperty(NO_ICON_MOTION_PROPERTY, true);
    connect(panelReopenBtn_, &QToolButton::clicked, this,
            [this] { if (actPanel_) actPanel_->setChecked(true); });
    panelReopenBtn_->hide();
    // The separator is QMainWindow chrome with no widget, so a mouse-transparent overlay paints the grip (browser .panel-resizer).
    panelGrip_ = new DockGripOverlay(this);
    {
      const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
      panelGrip_->setColors(pal.borderMain, pal.accent);
    }
    panelGrip_->hide();
    // The chat dock's resize edge (browser .chat-resizer), same trick.
    chatEdge_ = new DockEdgeOverlay(this);
    chatEdge_->setObjectName(QStringLiteral("chatResizeEdge"));
    {
      const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
      chatEdge_->setAccent(pal.accent);
    }
    chatEdge_->hide();
    spinControlsPill(false);   // seed the pill's angle from the current toolbar state
    updatePanelReopenButton();
  }

  // Over the 9px separator strip (theme.cpp QMainWindow::separator); hidden with the panel, while it floats, and
  // through a slide, so it lands with the panel rather than ahead of it (browser: #fs-panel-resizer forms with the list).
  void MainWindow::positionPanelGrip() {
    if (!panelGrip_ || !selPanel_) return;
    const Qt::DockWidgetArea area = dockWidgetArea(selPanel_);
    const bool on = !selPanel_->isHidden() && !selPanel_->isFloating() && !panelAnim_
                    && (area == Qt::RightDockWidgetArea || area == Qt::LeftDockWidgetArea);
    panelGrip_->setVisible(on);
    if (!on) {
      panelGrip_->setHot(false);
      panelGripDrag_ = false;
      return;
    }
    const QRect pr = selPanel_->geometry();
    constexpr int SEP_W = DOCK_SEPARATOR_PX;   // the QSS QMainWindow::separator width
    panelGrip_->setGeometry(area == Qt::LeftDockWidgetArea
                                ? QRect(pr.right() + 1, pr.top(), SEP_W, pr.height())
                                : QRect(pr.left() - SEP_W, pr.top(), SEP_W, pr.height()));
    panelGrip_->raise();
  }

  // Horizontal separators are a hairline, so the band is drawn to MIN_THICKNESS while the hit rect stays Qt's strip.
  void MainWindow::positionChatEdge() {
    if (!chatEdge_ || !chatDock_) return;
    const bool on = chatDock_->isVisible() && !chatDock_->isFloating() && !fs_.active;
    chatEdge_->setVisible(on);
    if (!on) {
      chatEdge_->setHot(false);
      chatEdgeDrag_ = false;
      chatEdgeHit_ = QRect();
      return;
    }
    const QRect r = chatDock_->geometry();
    const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
    constexpr int SEP_W = DOCK_SEPARATOR_PX;   // QSS QMainWindow::separator width
    constexpr int SEP_H = 1;                  // …and its height (theme.cpp)
    switch (area) {
      case Qt::LeftDockWidgetArea:   chatEdgeHit_ = QRect(r.right() + 1, r.top(), SEP_W, r.height()); break;
      case Qt::RightDockWidgetArea:  chatEdgeHit_ = QRect(r.left() - SEP_W, r.top(), SEP_W, r.height()); break;
      case Qt::TopDockWidgetArea:    chatEdgeHit_ = QRect(r.left(), r.bottom() + 1, r.width(), SEP_H); break;
      case Qt::BottomDockWidgetArea: chatEdgeHit_ = QRect(r.left(), r.top() - SEP_H, r.width(), SEP_H); break;
      default: chatEdgeHit_ = QRect(); chatEdge_->hide(); return;
    }
    QRect band = chatEdgeHit_;
    const int grow = DockEdgeOverlay::MIN_THICKNESS;
    if (band.height() < grow && band.width() > band.height())
      band.adjust(0, -(grow - band.height()) / 2, 0, (grow - band.height() + 1) / 2);
    else if (band.width() < grow && band.height() > band.width())
      band.adjust(-(grow - band.width()) / 2, 0, (grow - band.width() + 1) / 2, 0);
    chatEdge_->setGeometry(band);
    chatEdge_->raise();
  }

  // The stack hangs off the WINDOW's bottom-left, so it is told to clear the status bar's coord readout.
  void MainWindow::syncToastInset() {
    // Late dock signals during ~MainWindow land after the layout died.
    if (tearingDown_ || !notify_) return;
    // findChild, not statusBar() — the accessor lazily CREATES the empty strip this window deliberately doesn't keep.
    const QWidget* bar = findChild<QStatusBar*>();
    int bottom = bar && bar->isVisible() ? bar->height() : 0;
    int left = 0;
    // A docked chat panel owns its corner: the stack moves beside or above it.
    if (chatDock_ && chatDock_->isVisible() && !chatDock_->isFloating()) {
      const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
      if (area == Qt::LeftDockWidgetArea) left = chatDock_->width();   // the gap is the stack's own
      else if (area == Qt::BottomDockWidgetArea) bottom += chatDock_->height() + 8;
    }
    notify_->setLeftInset(left);
    notify_->setBottomInset(bottom);
  }

}  // namespace stencil::gui
