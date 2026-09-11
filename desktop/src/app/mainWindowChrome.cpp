#include "mainWindow.hpp"
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "stayOpenMenu.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "chatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "dropZonesOverlay.hpp"
#include "incognitoOverlay.hpp"
#include "guiHelpers.hpp"
#include "menuHotkeys.hpp"
#include "menuReveal.hpp"
#include "menuShimmer.hpp"
#include "modalReveal.hpp"
#include "searchCombo.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include "../support/appTooltip.hpp"
#include "../support/controlSwap.hpp"
#include "../support/dockGrip.hpp"
#include "../support/modalChrome.hpp"
#include "../support/iconMotion.hpp"
#include "../support/hoverSlide.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QStatusBar>
#include <QToolButton>

// Window chrome geometry: the overlay arrows, the dock grip, the chat edge and the toast inset.

namespace stencil::gui {

  // The top menu toggles from the "Controls" pill in the header row (kept in sync here). The panel
  // hides from its own header chevron and re-opens from a single floating chevron that sits flush to
  // the canvas' right edge — shown ONLY while the panel is hidden, so it's never a dead-end.
  void MainWindow::buildOverlayArrows() {
    panelReopenBtn_ = new QToolButton(this);
    panelReopenBtn_->setCursor(Qt::PointingHandCursor);
    panelReopenBtn_->setFocusPolicy(Qt::NoFocus);   // ditto: no focus halo over the canvas
    panelReopenBtn_->setFixedSize(kPanelToggleBox, kPanelToggleBox);   // same rounded square as the panel-header chevron
    panelReopenBtn_->setIconSize(QSize(kPanelToggleGlyph, kPanelToggleGlyph));
    panelReopenBtn_->setToolTip(QString("Show panel (%1)").arg(hotkey("togglePointsList", "Alt+X")));
    panelReopenBtn_->setStyleSheet(panelToggleQss());
    // Its angle is STATE, like the panel-header chevron's — no icon-motion on hover.
    panelReopenBtn_->setProperty(kNoIconMotionProperty, true);
    connect(panelReopenBtn_, &QToolButton::clicked, this,
            [this] { if (actPanel_) actPanel_->setChecked(true); });
    panelReopenBtn_->hide();
    // The canvas↔panel separator grip (browser .panel-resizer parity, animated) — the
    // separator is QMainWindow chrome with no widget of its own, so a decorative,
    // mouse-transparent overlay paints the bar and eventFilter drives its hot state.
    panelGrip_ = new DockGripOverlay(this);
    {
      const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
      panelGrip_->setColors(pal.borderMain, pal.accent);
    }
    panelGrip_->hide();
    // …and the chat dock's own resize edge (browser .chat-resizer), on the same trick.
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

  // Place the grip over the separator beside the docked panel — the 9px strip the
  // QSS sizes (theme.cpp QMainWindow::separator). Hidden with the panel (the reopen
  // chevron owns that state) and while it floats or fullscreen hides the chrome.
  void MainWindow::positionPanelGrip() {
    if (!panelGrip_ || !selPanel_) return;
    const Qt::DockWidgetArea area = dockWidgetArea(selPanel_);
    const bool on = !selPanel_->isHidden() && !selPanel_->isFloating() && !fs_.active
                    && (area == Qt::RightDockWidgetArea || area == Qt::LeftDockWidgetArea);
    panelGrip_->setVisible(on);
    if (!on) {
      panelGrip_->setHot(false);
      panelGripDrag_ = false;
      return;
    }
    const QRect pr = selPanel_->geometry();
    constexpr int kSepW = kDockSeparatorPx;   // the QSS QMainWindow::separator width
    panelGrip_->setGeometry(area == Qt::LeftDockWidgetArea
                                ? QRect(pr.right() + 1, pr.top(), kSepW, pr.height())
                                : QRect(pr.left() - kSepW, pr.top(), kSepW, pr.height()));
    panelGrip_->raise();
  }

  // Place the tint over the chat dock's resize separator, whichever area the dock is in
  // (browser .chat-resizer). Horizontal separators are a hairline, so the band is drawn to
  // kMinThickness while the hit rect stays the strip Qt really resizes on.
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
    constexpr int kSepW = kDockSeparatorPx;   // QSS QMainWindow::separator width
    constexpr int kSepH = 1;                  // …and its height (theme.cpp)
    switch (area) {
      case Qt::LeftDockWidgetArea:   chatEdgeHit_ = QRect(r.right() + 1, r.top(), kSepW, r.height()); break;
      case Qt::RightDockWidgetArea:  chatEdgeHit_ = QRect(r.left() - kSepW, r.top(), kSepW, r.height()); break;
      case Qt::TopDockWidgetArea:    chatEdgeHit_ = QRect(r.left(), r.bottom() + 1, r.width(), kSepH); break;
      case Qt::BottomDockWidgetArea: chatEdgeHit_ = QRect(r.left(), r.top() - kSepH, r.width(), kSepH); break;
      default: chatEdgeHit_ = QRect(); chatEdge_->hide(); return;
    }
    QRect band = chatEdgeHit_;
    const int grow = DockEdgeOverlay::kMinThickness;
    if (band.height() < grow && band.width() > band.height())
      band.adjust(0, -(grow - band.height()) / 2, 0, (grow - band.height() + 1) / 2);
    else if (band.width() < grow && band.height() > band.width())
      band.adjust(-(grow - band.width()) / 2, 0, (grow - band.width() + 1) / 2, 0);
    chatEdge_->setGeometry(band);
    chatEdge_->raise();
  }

  // The toast stack hangs off the WINDOW's bottom-left, which puts the status bar's coord
  // readout under it — so it is told to clear whatever that bar currently occupies (nothing
  // in fullscreen, where the bar is hidden). Refreshed from positionOverlayArrows, which
  // already runs on every resize, theme change and toolbar fold.
  void MainWindow::syncToastInset() {
    // Late dock signals during ~MainWindow land after the layout (and statusBar) died.
    if (tearingDown_ || !notify_) return;
    // findChild, not statusBar() — the accessor lazily CREATES a real QStatusBar on first
    // call, which is exactly the empty strip this window deliberately doesn't keep
    // (browser parity: nothing below .drop-hint).
    const QWidget* bar = findChild<QStatusBar*>();
    int bottom = bar && bar->isVisible() ? bar->height() : 0;
    int left = 0;
    // A docked chat panel owns its corner: the stack moves beside (left dock) or
    // above (bottom dock) it instead of overlapping the composer.
    if (chatDock_ && chatDock_->isVisible() && !chatDock_->isFloating()) {
      const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
      if (area == Qt::LeftDockWidgetArea) left = chatDock_->width();   // the gap is the stack's own
      else if (area == Qt::BottomDockWidgetArea) bottom += chatDock_->height() + 8;
    }
    notify_->setLeftInset(left);
    notify_->setBottomInset(bottom);
  }

}  // namespace stencil::gui
