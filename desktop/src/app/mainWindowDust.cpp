#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/modalChrome.hpp"
#include "../support/hoverSlide.hpp"

#include <QLayout>
#include <QPalette>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QToolBar>

// The dust flights that ride the chat/panel/toolbar extent slides.

namespace stencil::gui {

  namespace {
    // Parented to the veil, so an interrupted flight (stopChatAnim deletes the effect) takes the fade with it.
    QPropertyAnimation* fadeVeilUp(QGraphicsOpacityEffect* veil, int ms) {
      auto* fade = new QPropertyAnimation(veil, "opacity", veil);
      holdFadeKeys(fade, ms);
      fade->start(QAbstractAnimation::DeleteWhenStopped);
      return fade;
    }
  }  // namespace

  std::function<void(int)> MainWindow::chatExtentPin(bool horiz) {
    return [this, horiz](int v) {
      if (horiz) chatDock_->setFixedWidth(v);
      else chatDock_->setFixedHeight(v);
    };
  }

  QPointer<gui::DisintegrateOverlay> MainWindow::chatSurfaceFlight(
      Qt::DockWidgetArea area, bool gather, int ms, const std::function<void(int)>& pin, int full) {
    if (!chatDock_) return nullptr;
    QWidget* dustHost = chatDock_->window();
    if (!dustHost || !support::dustMotionOk()) return nullptr;
    pin(full);
    const QPixmap snap = chatDock_->grab();
    const QRect picture(chatDock_->mapTo(dustHost, QPoint(0, 0)), chatDock_->size());
    pin(gather ? 0 : full);   // gather starts empty; a scatter leaves from the settled size
    const QPoint target = dockAwayPoint(picture, area);
    QPointer<gui::DisintegrateOverlay> fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, dustHost, target, gather, ms, chatDock_->palette().color(QPalette::WindowText));
    if (fx) {
      auto* veil = new QGraphicsOpacityEffect(chatDock_);
      veil->setOpacity(0.0);
      chatDock_->setGraphicsEffect(veil);
      chatVeil_ = veil;
      if (gather) fadeVeilUp(veil, ms > 0 ? ms : gui::DisintegrateOverlay::kSurfaceInMs);
    }
    return fx;
  }

  // The points panel's flight: photographed at its OPEN width whichever way the slide runs; the veil hides the real panel, so the motes ARE the panel.
  QPointer<gui::DisintegrateOverlay> MainWindow::panelSurfaceFlight(bool gather, int ms, int full) {
    if (!selPanel_) return nullptr;
    QWidget* dustHost = selPanel_->window();
    if (!dustHost || !support::dustMotionOk()) return nullptr;
    selPanel_->setFixedWidth(full);
    if (QLayout* l = layout()) l->activate();   // the grab must see the open panel, not the rail
    const QPixmap snap = selPanel_->grab();
    const QRect picture(selPanel_->mapTo(dustHost, QPoint(0, 0)), selPanel_->size());
    const Qt::DockWidgetArea area = dockWidgetArea(selPanel_) == Qt::LeftDockWidgetArea
                                        ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
    QPointer<gui::DisintegrateOverlay> fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, dustHost, dockAwayPoint(picture, area), gather, ms,
        selPanel_->palette().color(QPalette::WindowText));
    if (fx) {
      auto* veil = new QGraphicsOpacityEffect(selPanel_);
      veil->setOpacity(0.0);
      selPanel_->setGraphicsEffect(veil);
      panelVeil_ = veil;
    }
    return fx;
  }

  // The tool rows collapse as ONE block: their union rect is the picture and the motes stream past the top edge (browser dockAwayPoint(box, 'top')).
  QPointer<gui::DisintegrateOverlay> MainWindow::barsSurfaceFlight(
      const QList<QToolBar*>& bars, bool gather, int ms) {
    if (!support::dustMotionOk()) return nullptr;
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

  // Where the bar's motes land. `closing` predicts imageInfoBar_'s post-close position — dustSelectedLineBarOut() runs before the dock hides.
  QPoint MainWindow::selectedLineBarDustPoint(const QRect& barPicture, bool closing) {
    if (imageInfoBar_ && imageInfoBar_->isVisible()) {
      const QRect infoRect(imageInfoBar_->mapTo(this, QPoint(0, 0)), imageInfoBar_->size());
      if (infoRect.width() >= 8 && infoRect.height() >= 8) {
        const int dockTop = selectedLineDock_ ? selectedLineDock_->mapTo(this, QPoint(0, 0)).y()
                                              : barPicture.top();
        const int y = closing ? dockTop + infoRect.height() : infoRect.bottom();
        return QPoint(barPicture.center().x(), y);
      }
    }
    return dockAwayPoint(barPicture, Qt::BottomDockWidgetArea);
  }

  // Entrance (browser selectionPanel.js surfaceIn). Call AFTER setVisible(true) + layout()->activate(), so the grab sees the populated bar.
  void MainWindow::dustSelectedLineBarIn() {
    if (!selectedLineBar_) return;
    if (!support::dustMotionOk()) return;
    const QPixmap snap = selectedLineBar_->grab();
    const QRect picture(selectedLineBar_->mapTo(this, QPoint(0, 0)), selectedLineBar_->size());
    auto* fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, this, selectedLineBarDustPoint(picture, /*closing=*/false), /*gather=*/true, 0,
        palette().color(QPalette::WindowText));
    if (!fx) return;
    // The veil hides the real bar for the gather; released once landed.
    auto* veil = new QGraphicsOpacityEffect(selectedLineBar_);
    veil->setOpacity(0.0);
    selectedLineBar_->setGraphicsEffect(veil);
    auto* fade = fadeVeilUp(veil, gui::DisintegrateOverlay::kSurfaceInMs);
    QPointer<SelectedLineBar> bar = selectedLineBar_;
    connect(fade, &QPropertyAnimation::finished, this, [bar, veil] {
      if (bar && bar->graphicsEffect() == veil) bar->setGraphicsEffect(nullptr);
    });
  }

  // Exit (surfaceOut). Call BEFORE setVisible(false), so the snapshot still shows the real bar.
  void MainWindow::dustSelectedLineBarOut() {
    if (!selectedLineBar_) return;
    if (!support::dustMotionOk()) return;
    const QPixmap snap = selectedLineBar_->grab();
    const QRect picture(selectedLineBar_->mapTo(this, QPoint(0, 0)), selectedLineBar_->size());
    gui::DisintegrateOverlay::overSurface(
        snap, picture, this, selectedLineBarDustPoint(picture, /*closing=*/true), /*gather=*/false, 0,
        palette().color(QPalette::WindowText));
  }

}  // namespace stencil::gui
