#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/modalChrome.hpp"
#include "../support/HoverSlide.hpp"

#include <QLayout>
#include <QPalette>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QToolBar>

// The dust flights that ride the chat/panel/toolbar extent slides.

namespace stencil::gui {

  namespace {
    // Parented to the veil, so an interrupted flight takes the fade with it. Only the
    // selection bar uses it: a surface that SLIDES must hand over at the end instead.
    QPropertyAnimation* fadeVeilUp(QGraphicsOpacityEffect* veil, int ms) {
      auto* fade = new QPropertyAnimation(veil, "opacity", veil);
      holdFadeKeys(fade, ms);
      fade->start(QAbstractAnimation::DeleteWhenStopped);
      return fade;
    }
  }  // namespace

  std::function<void(int)> MainWindow::chatExtentPin(bool horiz) {
    return [this, horiz](int v) {
      if (horiz) chatDock->setFixedWidth(v);
      else chatDock->setFixedHeight(v);
    };
  }

  QPointer<gui::DisintegrateOverlay> MainWindow::chatSurfaceFlight(
      Qt::DockWidgetArea area, bool gather, int ms, const std::function<void(int)>& pin, int full) {
    if (!chatDock) return nullptr;
    QWidget* dustHost = chatDock->window();
    if (!dustHost || !support::isDustMotionOk()) return nullptr;
    pin(full);
    // A dock is placed by the WINDOW's layout: without this the picture was measured at the
    // hidden dock's stale geometry, and the motes assembled a whole chat below where it lands.
    if (QLayout* l = layout()) l->activate();
    const QPixmap snap = chatDock->grab();
    const QRect picture(chatDock->mapTo(dustHost, QPoint(0, 0)), chatDock->size());
    pin(gather ? 0 : full);   // gather starts empty; a scatter leaves from the settled size
    const QPoint target = dockAwayPoint(picture, area);
    QPointer<gui::DisintegrateOverlay> fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, dustHost, target, gather, ms, chatDock->palette().color(QPalette::WindowText));
    /* The dock stays fully veiled for the WHOLE flight, and stopChatAnim hands over in one
     * beat at the end. It is still sliding while the motes settle at the rest position, so
     * anything that reveals it early shows the panel's own text twice, one copy off the other. */
    if (fx) {
      auto* veil = new QGraphicsOpacityEffect(chatDock);
      veil->setOpacity(0.0);
      chatDock->setGraphicsEffect(veil);
      chatVeil = veil;
      // The hand-over is the overlay's own end, never the slide's: the two clocks start a
      // beat apart, and that beat drew neither the motes nor the dock.
      connect(fx, &QObject::destroyed, this, [this] { dropChatVeil(); });
    }
    return fx;
  }

  // The points panel's flight: photographed at its OPEN width whichever way the slide runs; the veil hides the real panel, so the motes ARE the panel.
  QPointer<gui::DisintegrateOverlay> MainWindow::panelSurfaceFlight(bool gather, int ms, int full) {
    if (!selPanel) return nullptr;
    QWidget* dustHost = selPanel->window();
    if (!dustHost || !support::isDustMotionOk()) return nullptr;
    selPanel->setFixedWidth(full);
    if (QLayout* l = layout()) l->activate();   // the grab must see the open panel, not the rail
    const QPixmap snap = selPanel->grab();
    const QRect picture(selPanel->mapTo(dustHost, QPoint(0, 0)), selPanel->size());
    const Qt::DockWidgetArea area = dockWidgetArea(selPanel) == Qt::LeftDockWidgetArea
                                        ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
    QPointer<gui::DisintegrateOverlay> fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, dustHost, dockAwayPoint(picture, area), gather, ms,
        selPanel->palette().color(QPalette::WindowText));
    if (fx) {
      auto* veil = new QGraphicsOpacityEffect(selPanel);
      veil->setOpacity(0.0);
      selPanel->setGraphicsEffect(veil);
      panelVeil = veil;
    }
    return fx;
  }

  // The tool rows collapse as ONE block: their union rect is the picture and the motes stream past the top edge (browser dockAwayPoint(box, 'top')).
  QPointer<gui::DisintegrateOverlay> MainWindow::barsSurfaceFlight(
      const QList<QToolBar*>& bars, bool gather, int ms) {
    if (!support::isDustMotionOk()) return nullptr;
    QRect picture;
    for (QToolBar* b : bars) {
      if (!b->isVisible() || b->width() < 8 || b->height() < 8) continue;
      picture |= QRect(b->mapTo(this, QPoint(0, 0)), b->size());
    }
    if (picture.width() < 8 || picture.height() < 8) return nullptr;
    const qreal dpr = devicePixelRatioF();
    QPixmap snap(picture.size() * dpr);
    snap.setDevicePixelRatio(dpr);
    snap.fill(Qt::transparent);
    for (QToolBar* b : bars) {
      if (!b->isVisible() || b->width() < 8 || b->height() < 8) continue;
      b->render(&snap, b->mapTo(this, QPoint(0, 0)) - picture.topLeft(), QRegion(),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);
    }
    return gui::DisintegrateOverlay::overSurface(
        snap, picture, this, dockAwayPoint(picture, Qt::TopDockWidgetArea), gather, ms,
        palette().color(QPalette::WindowText));
  }

  // Where the bar's motes land. `closing` predicts imageInfoBar's post-close position — dustSelectedLineBarOut() runs before the dock hides.
  QPoint MainWindow::selectedLineBarDustPoint(const QRect& barPicture, bool closing) {
    if (imageInfoBar && imageInfoBar->isVisible()) {
      const QRect infoRect(imageInfoBar->mapTo(this, QPoint(0, 0)), imageInfoBar->size());
      if (infoRect.width() >= 8 && infoRect.height() >= 8) {
        const int dockTop = selectedLineDock ? selectedLineDock->mapTo(this, QPoint(0, 0)).y()
                                              : barPicture.top();
        const int y = closing ? dockTop + infoRect.height() : infoRect.bottom();
        return QPoint(barPicture.center().x(), y);
      }
    }
    return dockAwayPoint(barPicture, Qt::BottomDockWidgetArea);
  }

  // Entrance (browser selectionPanel.js surfaceIn). Call AFTER setVisible(true) + layout()->activate(), so the grab sees the populated bar.
  void MainWindow::dustSelectedLineBarIn() {
    if (!selectedLineBar) return;
    if (!support::isDustMotionOk()) return;
    const QPixmap snap = selectedLineBar->grab();
    const QRect picture(selectedLineBar->mapTo(this, QPoint(0, 0)), selectedLineBar->size());
    auto* fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, this, selectedLineBarDustPoint(picture, /*closing=*/false), /*gather=*/true, 0,
        palette().color(QPalette::WindowText));
    if (!fx) return;
    // The veil hides the real bar for the gather; released once landed.
    auto* veil = new QGraphicsOpacityEffect(selectedLineBar);
    veil->setOpacity(0.0);
    selectedLineBar->setGraphicsEffect(veil);
    auto* fade = fadeVeilUp(veil, gui::DisintegrateOverlay::SURFACE_IN_MS);
    QPointer<SelectedLineBar> bar = selectedLineBar;
    connect(fade, &QPropertyAnimation::finished, this, [bar, veil] {
      if (bar && bar->graphicsEffect() == veil) bar->setGraphicsEffect(nullptr);
    });
  }

  // Exit (surfaceOut). Call BEFORE setVisible(false), so the snapshot still shows the real bar.
  void MainWindow::dustSelectedLineBarOut() {
    if (!selectedLineBar) return;
    if (!support::isDustMotionOk()) return;
    const QPixmap snap = selectedLineBar->grab();
    const QRect picture(selectedLineBar->mapTo(this, QPoint(0, 0)), selectedLineBar->size());
    gui::DisintegrateOverlay::overSurface(
        snap, picture, this, selectedLineBarDustPoint(picture, /*closing=*/true), /*gather=*/false, 0,
        palette().color(QPalette::WindowText));
  }

}  // namespace stencil::gui
